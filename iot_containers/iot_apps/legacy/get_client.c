/**
 * get_client.c - Cliente IoT que prueba la operación GET de CORECONF
 *
 * OBJETIVO: Probar la operación GET del RFC draft-ietf-core-comi §3.3:
 *   1. STORE: POST /c → sube datos del sensor al gateway
 *   2. GET:   GET  /c → pide el datastore completo del gateway
 *
 * COMPILAR (local macOS):
 *   cd iot_containers/iot_apps && make get_client
 *
 * EJECUTAR:
 *   Terminal 1: ./get_server
 *   Terminal 2: ./get_client temperature sensor-temp-001
 *
 * FLUJO CoAP:
 *
 *   [PASO 1 - STORE]
 *   Cliente (este)                    Servidor (get_server)
 *     |  CON [POST /c]                    |
 *     |  Content-Format: 60 (CBOR)        |
 *     |  Payload: {1:id, 10:tipo, 20:val} |
 *     |──────────────────────────────────>|
 *     |  ACK [2.04 Changed]               |
 *     |<──────────────────────────────────|
 *
 *   [PASO 2 - GET]
 *     |  CON [GET /c]                     |
 *     |  (sin payload, sin Content-Format) |
 *     |──────────────────────────────────>|
 *     |  ACK [2.05 Content]               |
 *     |  Content-Format: 142              |
 *     |  Payload: { SID: val, SID: val }  |
 *     |<──────────────────────────────────|
 *
 * Content-Formats (RFC §2.3):
 *   GET request:  (ninguno — GET no lleva payload)
 *   GET response: CF=142 application/yang-data+cbor; id=sid
 *
 * Función CORECONF clave:
 *   parse_get_response()  → decodifica el mapa CBOR en CoreconfValueT
 */

#include <coap3/coap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <arpa/inet.h>

#include "../../include/coreconfTypes.h"
#include "../../include/serialization.h"
#include "../../include/get.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096
#define SEND_INTERVAL 2

/* ── Estado compartido con response_handler ── */
static int            response_received  = 0;
static uint8_t        last_response[BUFFER_SIZE];
static size_t         last_response_len  = 0;
static coap_pdu_code_t last_response_code;

static void print_cbor_hex(const uint8_t *data, size_t len) {
    printf("   ");
    for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i < len - 1) printf("\n   ");
    }
    if (len > 64) printf("... ");
    printf("\n");
}

/* ── Crear datos del sensor (igual que iot_client.c) ── */
static CoreconfValueT *create_sensor_data(const char *device_type, const char *device_id) {
    CoreconfValueT *data = createCoreconfHashmap();
    CoreconfHashMapT *map = data->data.map_value;

    insertCoreconfHashMap(map, 1,  createCoreconfString(device_id));
    insertCoreconfHashMap(map, 2,  createCoreconfString("store"));
    insertCoreconfHashMap(map, 10, createCoreconfString(device_type));
    insertCoreconfHashMap(map, 11, createCoreconfUint64((uint64_t)time(NULL)));

    if (strcmp(device_type, "temperature") == 0) {
        double temp = 15.0 + (rand() % 1500) / 100.0;
        insertCoreconfHashMap(map, 20, createCoreconfReal(temp));
        insertCoreconfHashMap(map, 21, createCoreconfString("celsius"));
    } else if (strcmp(device_type, "humidity") == 0) {
        double hum = 30.0 + (rand() % 6000) / 100.0;
        insertCoreconfHashMap(map, 20, createCoreconfReal(hum));
        insertCoreconfHashMap(map, 21, createCoreconfString("percent"));
    } else if (strcmp(device_type, "actuator") == 0) {
        insertCoreconfHashMap(map, 20, createCoreconfBoolean(rand() % 2));
    } else {
        insertCoreconfHashMap(map, 20, createCoreconfReal(22.5 + (rand() % 300) / 100.0));
        insertCoreconfHashMap(map, 21, createCoreconfReal(60.0 + (rand() % 2000) / 100.0));
    }

    return data;
}

/* ── Handler de respuesta CoAP ── */
static coap_response_t response_handler(coap_session_t *session,
                                         const coap_pdu_t *sent,
                                         const coap_pdu_t *received,
                                         const coap_mid_t mid) {
    (void)session; (void)sent; (void)mid;

    last_response_code = coap_pdu_get_code(received);
    printf("   📨 Respuesta: %d.%02d\n",
           COAP_RESPONSE_CLASS(last_response_code),
           last_response_code & 0x1F);

    size_t len;
    const uint8_t *data;
    if (coap_get_data(received, &len, &data)) {
        memcpy(last_response, data, len);
        last_response_len = len;
        printf("   📦 Payload recibido (%zu bytes)\n", len);
        print_cbor_hex(data, len);
    } else {
        last_response_len = 0;
        printf("   📦 Sin payload en la respuesta\n");
    }

    response_received = 1;
    return COAP_RESPONSE_OK;
}

/* ── Enviar PDU y esperar respuesta (hasta 5s) ── */
static int send_and_wait(coap_context_t *ctx, coap_session_t *session, coap_pdu_t *pdu) {
    response_received = 0;
    last_response_len = 0;

    coap_mid_t mid = coap_send(session, pdu);
    if (mid == COAP_INVALID_MID) {
        fprintf(stderr, "   ❌ Error enviando PDU\n");
        return 0;
    }

    int waited = 0;
    while (!response_received && waited < 5000) {
        coap_io_process(ctx, 100);
        waited += 100;
    }
    return response_received;
}

/* ============================================================================
 * PASO 1: STORE — POST /c con los datos del sensor
 * =========================================================================*/
static int do_store(coap_context_t *ctx, coap_session_t *session,
                    const char *device_type, const char *device_id) {
    printf("\n📤 PASO 1: STORE — enviando datos al gateway\n");

    CoreconfValueT *sensor_data = create_sensor_data(device_type, device_id);

    /* Serializar datos del sensor → CBOR */
    uint8_t buffer[BUFFER_SIZE];
    zcbor_state_t enc_states[5];
    zcbor_new_encode_state(enc_states, 5, buffer, BUFFER_SIZE, 0);

    if (!coreconfToCBOR(sensor_data, enc_states)) {
        printf("   ❌ Error codificando CBOR\n");
        freeCoreconf(sensor_data, true);
        return -1;
    }

    size_t cbor_len = (size_t)(enc_states[0].payload - buffer);
    printf("   📦 CBOR generado (%zu bytes):\n", cbor_len);
    print_cbor_hex(buffer, cbor_len);
    freeCoreconf(sensor_data, true);

    /* Crear PDU POST /c */
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST, session);
    if (!pdu) { printf("   ❌ Error creando PDU\n"); return -1; }

    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");

    uint8_t cf_buf[4];
    size_t cf_len = coap_encode_var_safe(cf_buf, sizeof(cf_buf), 60);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cf_len, cf_buf);

    coap_add_data(pdu, cbor_len, buffer);

    printf("   📤 Enviando POST /c...\n");
    if (!send_and_wait(ctx, session, pdu)) {
        printf("   ❌ Timeout: sin respuesta del gateway\n");
        return -1;
    }

    if (COAP_RESPONSE_CLASS(last_response_code) == 2) {
        printf("   ✅ STORE aceptado por el gateway (2.04 Changed)\n");
        return 0;
    } else {
        printf("   ❌ Gateway rechazó el STORE\n");
        return -1;
    }
}

/* ============================================================================
 * PASO 2: GET — GET /c sin payload → recibir datastore completo (CF=142)
 *
 * RFC §3.3:
 *   REQ:  GET /c          (sin Content-Format, sin payload)
 *   RES:  2.05 Content
 *         Content-Format: 142 (yang-data+cbor; id=sid)
 *         Payload: { SID: value, SID: value, ... }
 *
 * Se usa parse_get_response() para decodificar el mapa CBOR recibido.
 * =========================================================================*/
static int do_get(coap_context_t *ctx, coap_session_t *session) {
    printf("\n🗄️  PASO 2: GET — pidiendo datastore completo\n");

    /* ── GET no lleva payload ni Content-Format en el request ── */
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, session);
    if (!pdu) { printf("   ❌ Error creando PDU\n"); return -1; }

    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    /* Sin Content-Format (GET no lleva payload) */

    printf("   📤 Enviando GET /c (sin payload)...\n");
    if (!send_and_wait(ctx, session, pdu)) {
        printf("   ❌ Timeout: sin respuesta del gateway\n");
        return -1;
    }

    if (COAP_RESPONSE_CLASS(last_response_code) != 2 || last_response_len == 0) {
        printf("   ❌ Error en respuesta GET\n");
        return -1;
    }

    /* ── Decodificar mapa CBOR con parse_get_response() ── */
    CoreconfValueT *datastore = parse_get_response(last_response, last_response_len);

    if (!datastore) {
        printf("   ❌ Error decodificando respuesta GET\n");
        return -1;
    }

    if (datastore->type != CORECONF_HASHMAP) {
        printf("   ❌ Respuesta GET no es un mapa CBOR\n");
        freeCoreconf(datastore, true);
        return -1;
    }

    /* ── Mostrar todos los SIDs recibidos ── */
    printf("\n   ✅ Datastore completo recibido:\n");
    printf("   ┌─────────────────────────────────────────┐\n");

    CoreconfHashMapT *map = datastore->data.map_value;
    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
        CoreconfObjectT *obj = map->table[t];
        while (obj) {
            printf("   │  SID %-6" PRIu64 " → ", obj->key);
            if (obj->value) {
                switch (obj->value->type) {
                    case CORECONF_REAL:
                        printf("%.2f", obj->value->data.real_value);
                        break;
                    case CORECONF_STRING:
                        printf("\"%s\"", obj->value->data.string_value);
                        break;
                    case CORECONF_UINT_64:
                        printf("%" PRIu64, obj->value->data.u64);
                        break;
                    case CORECONF_INT_64:
                        printf("%" PRId64, obj->value->data.i64);
                        break;
                    case CORECONF_UINT_32:
                        printf("%" PRIu32, obj->value->data.u32);
                        break;
                    case CORECONF_TRUE:
                        printf("true");
                        break;
                    case CORECONF_FALSE:
                        printf("false");
                        break;
                    case CORECONF_NULL:
                        printf("null");
                        break;
                    default:
                        printf("(tipo %d)", obj->value->type);
                        break;
                }
            } else {
                printf("null");
            }
            printf("\n");
            obj = obj->next;
        }
    }

    printf("   └─────────────────────────────────────────┘\n");
    printf("   📊 Total nodos: %zu\n", map->size);

    freeCoreconf(datastore, true);
    printf("\n   ✅ GET completado (CF=142 yang-data+cbor)\n");
    return 0;
}

/* ============================================================================
 * MAIN
 * =========================================================================*/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <tipo_dispositivo> [device_id]\n", argv[0]);
        fprintf(stderr, "Tipos: temperature, humidity, actuator, edge\n");
        fprintf(stderr, "Ejemplo: %s temperature sensor-abc\n", argv[0]);
        return 1;
    }

    const char *device_type  = argv[1];
    const char *device_id    = (argc >= 3) ? argv[2] : getenv("DEVICE_ID");
    const char *gateway_host = getenv("GATEWAY_HOST");
    const char *gw_port_str  = getenv("GATEWAY_PORT");

    if (!device_id)    device_id    = "sensor-get-001";
    if (!gateway_host) gateway_host = "127.0.0.1";
    int gateway_port = gw_port_str ? atoi(gw_port_str) : 5683;

    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║    GET CLIENT - CORECONF GET sobre CoAP (libcoap)        ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    printf("🔧 Device ID:   %s\n", device_id);
    printf("🔧 Device Type: %s\n", device_type);
    printf("📡 Gateway:     %s:%d\n\n", gateway_host, gateway_port);

    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

    coap_startup();

    coap_address_t dst_addr;
    coap_address_init(&dst_addr);
    dst_addr.size = sizeof(struct sockaddr_in);
    dst_addr.addr.sin.sin_family = AF_INET;
    dst_addr.addr.sin.sin_port   = htons(gateway_port);
    if (inet_pton(AF_INET, gateway_host, &dst_addr.addr.sin.sin_addr) != 1) {
        fprintf(stderr, "❌ IP del gateway inválida: %s\n", gateway_host);
        coap_cleanup();
        return 1;
    }

    coap_context_t *ctx     = coap_new_context(NULL);
    coap_session_t *session = NULL;
    if (ctx) session = coap_new_client_session(ctx, NULL, &dst_addr, COAP_PROTO_UDP);

    if (!ctx || !session) {
        fprintf(stderr, "❌ Error creando contexto/sesión\n");
        if (ctx) coap_free_context(ctx);
        coap_cleanup();
        return 1;
    }

    coap_register_response_handler(ctx, response_handler);

    /* ── Dos ciclos STORE → GET ── */
    int count = 0;
    while (count < 2) {
        count++;
        printf("\n╔═══════════════════════════════════════════════════════════╗\n");
        printf("║ CICLO %d                                                   ║\n", count);
        printf("╚═══════════════════════════════════════════════════════════╝\n");

        int ok = 0;
        if (do_store(ctx, session, device_type, device_id) == 0) {
            ok = (do_get(ctx, session) == 0);
        }

        printf(ok ? "\n✅ Ciclo completado\n" : "\n❌ Error en el ciclo\n");

        if (count < 2) {
            printf("\n⏳ Esperando %d segundos...\n", SEND_INTERVAL);
            sleep(SEND_INTERVAL);
        }
    }

    printf("\n🧹 Liberando recursos...\n");
    coap_session_release(session);
    coap_free_context(ctx);
    coap_cleanup();
    printf("✨ Cliente terminado\n");
    return 0;
}

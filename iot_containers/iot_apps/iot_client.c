/**
 * iot_client.c - Cliente IoT (sensor/actuador) con FETCH operations sobre CoAP
 *
 * OBJETIVO: Dispositivo IoT que se comunica con el gateway usando CoAP (libcoap):
 *   1. STORE: POST /c  → envía datos del sensor (CBOR hashmap)
 *   2. FETCH: FETCH /c → pide SIDs específicos (CBOR sequence de instance-identifiers)
 *
 * COMPILAR:
 *   gcc iot_client.c ccoreconf.a -lcoap-3 -I../include -I../coreconf_zcbor_generated -o iot_client
 *
 * FLUJO CoAP:
 *
 *   [PASO 1 - STORE]
 *   Cliente (este)                 Servidor (gateway)
 *   ──────────────                 ──────────────────
 *     |  CON [POST /c]               |
 *     |  Content-Format: 60          |
 *     |  Payload: {1:id, 20:valor}   |
 *     |─────────────────────────────>|
 *     |  ACK [2.04 Changed]          |
 *     |<─────────────────────────────|
 *
 *   [PASO 2 - FETCH]
 *     |  CON [FETCH /c]              |
 *     |  Content-Format: 60          |
 *     |  Payload: CBOR sequence iids |
 *     |─────────────────────────────>|
 *     |  ACK [2.05 Content]          |
 *     |  Payload: CBOR sequence vals |
 *     |<─────────────────────────────|
 *
 * LÓGICA CORECONF (ya implementada):
 *   - create_sensor_data()            → genera el hashmap con datos del sensor
 *   - create_fetch_request_with_iids()→ genera la CBOR sequence de SIDs
 *   - cborToCoreconfValue()           → decodifica la respuesta CBOR
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
#include "../../include/fetch.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096
#define SEND_INTERVAL 3

/* ── Estado compartido entre main y el response_handler ── */
static int response_received = 0;  /* flag: llegó respuesta */
static uint8_t last_response[BUFFER_SIZE];
static size_t  last_response_len = 0;
static coap_pdu_code_t last_response_code;

void print_cbor_hex(const uint8_t *data, size_t len) {
    printf("   ");
    for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i < len - 1) printf("\n   ");
    }
    if (len > 64) printf("... ");
    printf("\n");
}

/*
 * ============================================================================
 * LÓGICA CORECONF: Crear datos de sensor (ya implementado, no tocar)
 * ============================================================================
 */
CoreconfValueT* create_sensor_data(const char *device_type, const char *device_id) {
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
    } else if (strcmp(device_type, "edge") == 0) {
        insertCoreconfHashMap(map, 20, createCoreconfReal(22.5 + (rand() % 300) / 100.0));
        insertCoreconfHashMap(map, 21, createCoreconfReal(60.0 + (rand() % 2000) / 100.0));
    }

    return data;
}

/*
 * ============================================================================
 * HANDLER de respuesta CoAP
 * ============================================================================
 * libcoap llama a esta función cuando llega la respuesta del servidor.
 * Guarda el payload en last_response[] para que main() lo procese.
 *
 * PARÁMETROS:
 *   session  → sesión por la que llegó la respuesta
 *   sent     → PDU que enviamos nosotros (la petición)
 *   received → PDU de respuesta del servidor
 *   mid      → message ID
 */
static coap_response_t response_handler(coap_session_t *session,
                                        const coap_pdu_t *sent,
                                        const coap_pdu_t *received,
                                        const coap_mid_t mid) {
    (void)session; (void)sent; (void)mid;

    /*
     * ========================================================================
     * TODO 1: Obtener el código de respuesta
     * ========================================================================
     * FUNCIÓN: coap_pdu_get_code(pdu)
     * Guárdalo en last_response_code para que main() sepa si fue ok o error.
     */
    // ESCRIBE AQUÍ:
    last_response_code=coap_pdu_get_code(received);

    printf("   📨 Respuesta: %d.%02d\n",
           COAP_RESPONSE_CLASS(last_response_code),
           last_response_code & 0x1F);

    /*
     * ========================================================================
     * TODO 2: Extraer el payload de la respuesta
     * ========================================================================
     * FUNCIÓN: coap_get_data(pdu, &longitud, &datos)
     * Si hay payload (retorna 1):
     *   - Copia los datos en last_response[] con memcpy()
     *   - Guarda la longitud en last_response_len
     */
    size_t len;
    const uint8_t *data;
    
    // ESCRIBE AQUÍ:
    if(coap_get_data(received, &len, &data)) {
        memcpy(last_response, data, len);
        last_response_len = len;
        printf("   📦 Payload recibido (%zu bytes)\n", len);
        print_cbor_hex(data, len);
    } else {
        last_response_len = 0;
        printf("   📦 Sin payload en la respuesta\n");
    }

    response_received = 1;  /* señalizar a main() que ya llegó */
    return COAP_RESPONSE_OK;
}

/*
 * ============================================================================
 * Enviar petición CoAP y esperar respuesta (helper interno)
 * ============================================================================
 * Envía el PDU dado y espera hasta 5 segundos la respuesta.
 * Retorna: 1 si llegó respuesta, 0 si timeout
 */
static int send_and_wait(coap_context_t *ctx, coap_session_t *session, coap_pdu_t *pdu) {
    response_received = 0;
    last_response_len = 0;

    coap_mid_t mid = coap_send(session, pdu);
    if (mid == COAP_INVALID_MID) {
        fprintf(stderr, "   ❌ Error enviando PDU\n");
        return 0;
    }

    /* Esperar hasta 5s a que llegue la respuesta */
    int waited = 0;
    while (!response_received && waited < 5000) {
        coap_io_process(ctx, 100);
        waited += 100;
    }

    return response_received;
}

/*
 * ============================================================================
 * PASO 1: STORE — enviar datos del sensor al gateway (POST /c)
 * ============================================================================
 */
static int do_store(coap_context_t *ctx, coap_session_t *session,
                    const char *device_type, const char *device_id) {

    printf("\n📤 PASO 1: STORE — enviando datos al gateway\n");

    /* ── Lógica CORECONF: crear CBOR del sensor (ya implementado) ── */
    CoreconfValueT *sensor_data = create_sensor_data(device_type, device_id);

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
    /* ── Fin lógica CORECONF ── */

    /*
     * ========================================================================
     * TODO 3: Crear PDU para POST /c
     * ========================================================================
     * Igual que en los ejercicios de aprendizaje:
     *   coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST, session);
     */
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST, session);
    if (!pdu) {
        printf("   ❌ Error creando PDU\n");
        return -1;
    }


    /*
     * ========================================================================
     * TODO 4: Añadir URI-Path "c"
     * ========================================================================
     * FUNCIÓN: coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t*)"c")
     */
    // ESCRIBE AQUÍ:
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t*)"c");

    /*
     * ========================================================================
     * TODO 5: Añadir Content-Format: 60 (CBOR)
     * ========================================================================
     * uint8_t cf_buf[4];
     * size_t cf_len = coap_encode_var_safe(cf_buf, sizeof(cf_buf), 60);
     * coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cf_len, cf_buf);
     */
    // ESCRIBE AQUÍ:
    uint8_t cf_buf[4];
    size_t cf_len = coap_encode_var_safe(cf_buf, sizeof(cf_buf), 60);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cf_len, cf_buf);

    /*
     * ========================================================================
     * TODO 6: Añadir payload CBOR al PDU
     * ========================================================================
     * FUNCIÓN: coap_add_data(pdu, cbor_len, buffer)
     */
    // ESCRIBE AQUÍ:
    coap_add_data(pdu, cbor_len, buffer);

    printf("   📤 Enviando POST /c...\n");
    if (!send_and_wait(ctx, session, pdu)) {
        printf("   ❌ Timeout: sin respuesta del gateway\n");
        return -1;
    }

    /* Verificar código de respuesta: esperamos 2.04 Changed */
    if (COAP_RESPONSE_CLASS(last_response_code) == 2) {
        printf("   ✅ STORE aceptado por el gateway\n");
        return 0;
    } else {
        printf("   ❌ Gateway rechazó el STORE\n");
        return -1;
    }
}

/*
 * ============================================================================
 * PASO 2: FETCH — pedir datos específicos al gateway (FETCH /c)
 * ============================================================================
 */
static int do_fetch(coap_context_t *ctx, coap_session_t *session) {

    printf("\n🔍 PASO 2: FETCH — pidiendo datos específicos (RFC 9254)\n");

    /* ── Lógica CORECONF: crear CBOR sequence de instance-identifiers ── */
    InstanceIdentifier fetch_ids[] = {
        {IID_SIMPLE,        20, {.str_key = NULL}},
        {IID_SIMPLE,        21, {.str_key = NULL}},
        {IID_WITH_STR_KEY,  10, {.str_key = "temperature"}}
    };
    size_t fetch_count = 3;

    printf("   Identificadores solicitados:\n");
    for (size_t i = 0; i < fetch_count; i++) {
        if (fetch_ids[i].type == IID_SIMPLE)
            printf("     - SID %" PRIu64 "\n", fetch_ids[i].sid);
        else
            printf("     - [%" PRIu64 ", \"%s\"]\n", fetch_ids[i].sid, fetch_ids[i].key.str_key);
    }

    uint8_t fetch_buf[BUFFER_SIZE];
    size_t fetch_len = create_fetch_request_with_iids(fetch_buf, BUFFER_SIZE, fetch_ids, fetch_count);
    if (fetch_len == 0) {
        printf("   ❌ Error creando FETCH sequence\n");
        return -1;
    }
    printf("   📦 CBOR sequence (%zu bytes):\n", fetch_len);
    print_cbor_hex(fetch_buf, fetch_len);
    /* ── Fin lógica CORECONF ── */

    /*
     * ========================================================================
     * TODO 7: Crear PDU para FETCH /c
     * ========================================================================
     * IGUAL que POST pero con COAP_REQUEST_CODE_FETCH como código.
     * El método CoAP FETCH (RFC 8132) lleva payload igual que POST
     * pero semánticamente significa "dame los datos de estos SIDs".
     *
     * coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_FETCH, session);
     */
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_FETCH, session);
    if (!pdu) {
        printf("   ❌ Error creando PDU para FETCH\n");
        return -1;
    }

    /*
     * ========================================================================
     * TODO 8: Añadir URI-Path "c" y Content-Format: 60
     * ========================================================================
     * Igual que en el STORE (TODOs 4 y 5).
     */
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t*)"c");

    // Content-Format: 141 = application/yang-identifiers+cbor-seq (RFC §2.3)
    uint8_t cf_buf[4];
    size_t cf_len = coap_encode_var_safe(cf_buf, sizeof(cf_buf), 141);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cf_len, cf_buf);

    /* RFC §4.1: indicar Accept: 142 (yang-data+cbor; id=sid) para la respuesta */
    uint8_t acc_buf[4];
    size_t acc_len = coap_encode_var_safe(acc_buf, sizeof(acc_buf), 142);
    coap_add_option(pdu, COAP_OPTION_ACCEPT, acc_len, acc_buf);

    /*
     * ========================================================================
     * TODO 9: Añadir el payload CBOR sequence al PDU
     * ========================================================================
     * FUNCIÓN: coap_add_data(pdu, fetch_len, fetch_buf)
     */
    coap_add_data(pdu, fetch_len, fetch_buf);


    printf("   📤 Enviando FETCH /c...\n");
    if (!send_and_wait(ctx, session, pdu)) {
        printf("   ❌ Timeout: sin respuesta del gateway\n");
        return -1;
    }

    if (COAP_RESPONSE_CLASS(last_response_code) != 2 || last_response_len == 0) {
        printf("   ❌ Error en respuesta FETCH\n");
        return -1;
    }

    printf("   📥 Resultado FETCH (%zu bytes):\n", last_response_len);
    print_cbor_hex(last_response, last_response_len);

    /* ── Lógica CORECONF: decodificar CBOR sequence de respuestas ── */
    zcbor_state_t dec_states[5];
    size_t offset = 0;
    size_t result_idx = 0;

    printf("   ✅ Resultados:\n\n");
    while (offset < last_response_len && result_idx < fetch_count) {
        zcbor_new_decode_state(dec_states, 5,
                               last_response + offset,
                               last_response_len - offset, 1, NULL, 0);
        CoreconfValueT *item = cborToCoreconfValue(dec_states, 0);
        if (!item) break;

        uint64_t sid = fetch_ids[result_idx].sid;
        printf("   🎯 SID %" PRIu64 ": ", sid);

        if (item->type == CORECONF_HASHMAP) {
            CoreconfValueT *val = getCoreconfHashMap(item->data.map_value, sid);
            if (val) {
                switch (val->type) {
                    case CORECONF_REAL:    printf("%.2f\n",       val->data.real_value); break;
                    case CORECONF_INT_64:  printf("%" PRId64 "\n",val->data.i64);        break;
                    case CORECONF_UINT_64: printf("%" PRIu64 "\n",val->data.u64);        break;
                    case CORECONF_STRING:  printf("\"%s\"\n",      val->data.string_value); break;
                    case CORECONF_TRUE:    printf("true\n");                              break;
                    case CORECONF_FALSE:   printf("false\n");                             break;
                    default:               printf("null\n");                              break;
                }
            } else { printf("null (no encontrado)\n"); }
        } else { printf("null\n"); }

        offset += (size_t)(dec_states[0].payload - (last_response + offset));
        freeCoreconf(item, true);
        result_idx++;
    }
    printf("\n   ✅ FETCH completado\n");
    /* ── Fin lógica CORECONF ── */

    return 0;
}

/*
 * ============================================================================
 * MAIN
 * ============================================================================
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <tipo_dispositivo>\n", argv[0]);
        fprintf(stderr, "Tipos: temperature, humidity, actuator, edge\n");
        return 1;
    }

    const char *device_type = argv[1];
    const char *device_id   = getenv("DEVICE_ID");
    const char *gateway_host = getenv("GATEWAY_HOST");
    const char *gw_port_str  = getenv("GATEWAY_PORT");

    if (!device_id)    device_id    = "unknown-device";
    if (!gateway_host) gateway_host = "172.20.0.10";
    int gateway_port = gw_port_str ? atoi(gw_port_str) : 5683;

    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║    IoT CLIENT - CORECONF/CBOR sobre CoAP (libcoap)       ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    printf("🔧 Device ID:   %s\n", device_id);
    printf("🔧 Device Type: %s\n", device_type);
    printf("📡 Gateway:     %s:%d\n\n", gateway_host, gateway_port);

    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

    /*
     * ========================================================================
     * TODO 10: Inicializar libcoap
     * ========================================================================
     * FUNCIÓN: coap_startup()
     */
    // ESCRIBE AQUÍ:
    coap_startup();

    /*
     * ========================================================================
     * TODO 11: Configurar dirección del gateway
     * ========================================================================
     * Usar coap_address_init() + AF_INET + htons(gateway_port) + inet_pton()
     * PISTA: Igual que en los ejercicios de aprendizaje.
     *        En Linux no hace falta el bloque __APPLE__.
     */
    coap_address_t dst_addr;
    coap_address_init(&dst_addr);
    dst_addr.size = sizeof(struct sockaddr_in);
    dst_addr.addr.sin.sin_family = AF_INET;
    dst_addr.addr.sin.sin_port = htons(gateway_port);
    if (inet_pton(AF_INET, gateway_host, &dst_addr.addr.sin.sin_addr) != 1) {
        fprintf(stderr, "❌ Dirección IP del gateway inválida\n");
        coap_cleanup();
        return 1;
    }


    /*
     * ========================================================================
     * TODO 12: Crear contexto y sesión cliente
     * ========================================================================
     * ctx     = coap_new_context(NULL)
     * session = coap_new_client_session(ctx, NULL, &dst_addr, COAP_PROTO_UDP)
     */
    coap_context_t *ctx = NULL;
    coap_session_t *session = NULL;
    // ESCRIBE AQUÍ:
    ctx = coap_new_context(NULL);
    if (ctx) {
        session = coap_new_client_session(ctx, NULL, &dst_addr, COAP_PROTO_UDP);
    }


    if (!ctx || !session) {
        fprintf(stderr, "❌ Error creando contexto/sesión\n");
        if (ctx) coap_free_context(ctx);
        coap_cleanup();
        return 1;
    }

    /*
     * ========================================================================
     * TODO 13: Registrar handler de respuesta
     * ========================================================================
     * FUNCIÓN: coap_register_response_handler(ctx, response_handler)
     */
    // ESCRIBE AQUÍ:
    coap_register_response_handler(ctx, response_handler);


    /* Ciclos de comunicación */
    int count = 0;
    while (count < 2) {
        count++;
        printf("\n╔═══════════════════════════════════════════════════════════╗\n");
        printf("║ CICLO %d\n", count);
        printf("╚═══════════════════════════════════════════════════════════╝\n");

        int ok = 0;
        if (do_store(ctx, session, device_type, device_id) == 0) {
            ok = (do_fetch(ctx, session) == 0);
        }

        printf(ok ? "\n✅ Ciclo completado\n" : "\n❌ Error en el ciclo\n");

        if (count < 2) {
            printf("\n⏳ Esperando %d segundos...\n", SEND_INTERVAL);
            sleep(SEND_INTERVAL);
        }
    }

    /*
     * ========================================================================
     * TODO 14: Limpieza
     * ========================================================================
     * coap_session_release(session)
     * coap_free_context(ctx)
     * coap_cleanup()
     */
    printf("\n🧹 Liberando recursos...\n");
    // ESCRIBE AQUÍ:
    coap_session_release(session);
    coap_free_context(ctx);
    coap_cleanup();


    printf("✨ Cliente terminado\n");
    return 0;
}

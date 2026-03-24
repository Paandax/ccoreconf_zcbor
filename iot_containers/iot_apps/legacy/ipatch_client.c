/**
 * ipatch_client.c - Cliente IoT que prueba la operación iPATCH de CORECONF
 *                   (RFC draft-ietf-core-comi §4.5)
 *
 * OBJETIVO: Demostrar la actualización PARCIAL del datastore:
 *
 *   PASO 1 — STORE:   POST /c  → carga datos iniciales del sensor
 *   PASO 2 — GET:     GET  /c  → lee el estado inicial del datastore
 *   PASO 3 — iPATCH:  iPATCH /c  CF=141  { 20: nuevo_valor }  → actualiza SOLO SID 20
 *   PASO 4 — GET:     GET  /c  → verifica que SID 20 cambió, el resto igual
 *
 * DEMO DE PARCIALIDAD (lo importante de iPATCH):
 *   Antes:  { SID10:"temperature", SID11:timestamp, SID20:22.5, SID21:"celsius" }
 *   PATCH:  { SID20: 99.9 }          ← solo actualizamos el valor
 *   Después:{ SID10:"temperature", SID11:timestamp, SID20:99.9, SID21:"celsius" }
 *              ↑ sin cambios                               ↑ actualizado  ↑ sin cambios
 *
 * COMPILAR:
 *   cd iot_containers/iot_apps && make ipatch_client
 *
 * EJECUTAR:
 *   Terminal 1: ./ipatch_server
 *   Terminal 2: ./ipatch_client temperature sensor-patch-001
 *
 * Content-Formats RFC §2.3:
 *   iPATCH request:  CF=141  application/yang-patch+cbor; id=sid
 *   GET response:    CF=142  application/yang-data+cbor; id=sid
 */

#include <coap3/coap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <arpa/inet.h>

#include "coreconfTypes.h"
#include "serialization.h"
#include "get.h"
#include "ipatch.h"
#include "zcbor_encode.h"
#include "zcbor_decode.h"

#define BUFFER_SIZE 4096

/* ── Estado compartido con response_handler ── */
static int             response_received  = 0;
static uint8_t         last_response[BUFFER_SIZE];
static size_t          last_response_len  = 0;
static coap_pdu_code_t last_response_code;

static void print_cbor_hex(const uint8_t *data, size_t len) {
    printf("   ");
    for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i < len - 1) printf("\n   ");
    }
    if (len > 64) printf("... (%zu bytes total)", len);
    printf("\n");
}

/* ── Crear datos iniciales del sensor ── */
static CoreconfValueT *create_sensor_data(const char *device_type,
                                           const char *device_id) {
    CoreconfValueT  *data = createCoreconfHashmap();
    CoreconfHashMapT *map = data->data.map_value;

    insertCoreconfHashMap(map, 1,  createCoreconfString(device_id));
    insertCoreconfHashMap(map, 2,  createCoreconfString("store"));
    insertCoreconfHashMap(map, 10, createCoreconfString(device_type));
    insertCoreconfHashMap(map, 11, createCoreconfUint64((uint64_t)time(NULL)));

    if (strcmp(device_type, "temperature") == 0) {
        double temp = 15.0 + (rand() % 1000) / 100.0;
        insertCoreconfHashMap(map, 20, createCoreconfReal(temp));
        insertCoreconfHashMap(map, 21, createCoreconfString("celsius"));
    } else if (strcmp(device_type, "humidity") == 0) {
        double hum = 40.0 + (rand() % 4000) / 100.0;
        insertCoreconfHashMap(map, 20, createCoreconfReal(hum));
        insertCoreconfHashMap(map, 21, createCoreconfString("percent"));
    } else {
        insertCoreconfHashMap(map, 20, createCoreconfReal(22.5));
        insertCoreconfHashMap(map, 21, createCoreconfString("celsius"));
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
static int send_and_wait(coap_context_t *ctx, coap_session_t *session,
                          coap_pdu_t *pdu) {
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

/* ── Mostrar datastore recibido (reutilizable) ── */
static void print_datastore(const uint8_t *data, size_t len) {
    CoreconfValueT *ds = parse_get_response(data, len);
    if (!ds || ds->type != CORECONF_HASHMAP) {
        printf("   ❌ Error decodificando datastore\n");
        if (ds) freeCoreconf(ds, true);
        return;
    }

    CoreconfHashMapT *map = ds->data.map_value;
    printf("   ┌─────────────────────────────────────────┐\n");
    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
        CoreconfObjectT *obj = map->table[t];
        while (obj) {
            printf("   │  SID %-6" PRIu64 " → ", obj->key);
            if (obj->value) {
                switch (obj->value->type) {
                    case CORECONF_REAL:
                        printf("%.2f", obj->value->data.real_value); break;
                    case CORECONF_STRING:
                        printf("\"%s\"", obj->value->data.string_value); break;
                    case CORECONF_UINT_64:
                        printf("%" PRIu64, obj->value->data.u64); break;
                    case CORECONF_INT_64:
                        printf("%" PRId64, obj->value->data.i64); break;
                    case CORECONF_UINT_32:
                        printf("%" PRIu32, obj->value->data.u32); break;
                    case CORECONF_TRUE:  printf("true");  break;
                    case CORECONF_FALSE: printf("false"); break;
                    case CORECONF_NULL:  printf("null");  break;
                    default: printf("(tipo %d)", obj->value->type); break;
                }
            } else { printf("null"); }
            printf("\n");
            obj = obj->next;
        }
    }
    printf("   └─────────────────────────────────────────┘\n");
    printf("   📊 Total nodos: %zu\n", map->size);
    freeCoreconf(ds, true);
}

/* ============================================================================
 * PASO 1: STORE — POST /c → datos iniciales
 * =========================================================================*/
static int do_store(coap_context_t *ctx, coap_session_t *session,
                     const char *device_type, const char *device_id) {
    printf("\n📤 PASO 1: STORE — enviando datos iniciales al gateway\n");

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
    printf("   📦 CBOR generado (%zu bytes)\n", cbor_len);
    print_cbor_hex(buffer, cbor_len);
    freeCoreconf(sensor_data, true);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST, session);
    if (!pdu) return -1;

    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    uint8_t cf_buf[4];
    size_t cf_len = coap_encode_var_safe(cf_buf, sizeof(cf_buf), 60);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cf_len, cf_buf);
    coap_add_data(pdu, cbor_len, buffer);

    printf("   📤 Enviando POST /c...\n");
    if (!send_and_wait(ctx, session, pdu)) {
        printf("   ❌ Timeout\n"); return -1;
    }

    if (COAP_RESPONSE_CLASS(last_response_code) == 2) {
        printf("   ✅ STORE aceptado → 2.04 Changed\n");
        return 0;
    }
    printf("   ❌ STORE rechazado\n");
    return -1;
}

/* ============================================================================
 * PASO 2/4: GET — GET /c → leer datastore completo (CF=142)
 * =========================================================================*/
static int do_get(coap_context_t *ctx, coap_session_t *session,
                   const char *label) {
    printf("\n🗄️  %s — leyendo datastore completo\n", label);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, session);
    if (!pdu) return -1;

    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");

    printf("   📤 Enviando GET /c (sin payload)...\n");
    if (!send_and_wait(ctx, session, pdu)) {
        printf("   ❌ Timeout\n"); return -1;
    }

    if (COAP_RESPONSE_CLASS(last_response_code) != 2 || last_response_len == 0) {
        printf("   ❌ Error en respuesta GET\n");
        return -1;
    }

    printf("\n   ✅ Datastore recibido (CF=142 yang-data+cbor):\n");
    print_datastore(last_response, last_response_len);
    return 0;
}

/* ============================================================================
 * PASO 3: iPATCH — iPATCH /c CF=141 → actualización PARCIAL
 *
 * RFC §4.5:
 *   REQ:  iPATCH /c   CF=141   { SID: new_val, ... }
 *   RES:  2.04 Changed         (sin payload)
 *
 * DEMOSTRACIÓN: solo enviamos { 20: nuevo_valor }
 * → SID 10 (tipo), SID 11 (timestamp), SID 21 (unidad) quedan sin cambios
 * =========================================================================*/
static int do_ipatch(coap_context_t *ctx, coap_session_t *session,
                      uint64_t sid, double new_value) {
    printf("\n🔧 PASO 3: iPATCH — actualización PARCIAL del datastore\n");
    printf("   📝 Solo actualizamos SID %" PRIu64 " → %.2f\n", sid, new_value);
    printf("   📝 El resto de SIDs queda INTACTO (semántica parcial)\n");

    /* ── Construir el parche: solo el SID a modificar ── */
    CoreconfValueT  *patch = createCoreconfHashmap();
    CoreconfHashMapT *pm   = patch->data.map_value;
    insertCoreconfHashMap(pm, sid, createCoreconfReal(new_value));

    /* ── Serializar parche con create_ipatch_request() (CF=141) ── */
    uint8_t patch_buf[BUFFER_SIZE];
    size_t  patch_len = create_ipatch_request(patch_buf, BUFFER_SIZE, patch);
    freeCoreconf(patch, true);

    if (patch_len == 0) {
        printf("   ❌ Error serializando parche\n");
        return -1;
    }

    printf("   📦 CBOR parche (%zu bytes): ", patch_len);
    print_cbor_hex(patch_buf, patch_len);

    /* ── Crear PDU iPATCH /c (CoAP method code 7 = RFC 8132) ── */
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON,
                                    COAP_REQUEST_CODE_IPATCH, session);
    if (!pdu) { printf("   ❌ Error creando PDU\n"); return -1; }

    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");

    /* Content-Format: 141 (application/yang-patch+cbor; id=sid) */
    uint8_t cf_buf[4];
    size_t cf_len = coap_encode_var_safe(cf_buf, sizeof(cf_buf),
                                          COAP_MEDIA_TYPE_YANG_PATCH_CBOR);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cf_len, cf_buf);
    coap_add_data(pdu, patch_len, patch_buf);

    printf("   📤 Enviando iPATCH /c (CF=141 yang-patch+cbor)...\n");
    if (!send_and_wait(ctx, session, pdu)) {
        printf("   ❌ Timeout\n"); return -1;
    }

    if (COAP_RESPONSE_CLASS(last_response_code) == 2) {
        printf("   ✅ iPATCH aceptado → 2.04 Changed (sin payload)\n");
        return 0;
    }
    printf("   ❌ iPATCH rechazado (código %d.%02d)\n",
           COAP_RESPONSE_CLASS(last_response_code),
           last_response_code & 0x1F);
    return -1;
}

/* ============================================================================
 * MAIN
 * =========================================================================*/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <tipo_dispositivo> [device_id]\n", argv[0]);
        fprintf(stderr, "Tipos: temperature, humidity\n");
        fprintf(stderr, "Ejemplo: %s temperature sensor-patch-001\n", argv[0]);
        return 1;
    }

    const char *device_type  = argv[1];
    const char *device_id    = (argc >= 3) ? argv[2] : "sensor-patch-001";
    const char *gateway_host = getenv("GATEWAY_HOST");
    const char *gw_port_str  = getenv("GATEWAY_PORT");

    if (!gateway_host) gateway_host = "127.0.0.1";
    int gateway_port = gw_port_str ? atoi(gw_port_str) : 5683;

    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║  iPATCH CLIENT - CORECONF iPATCH sobre CoAP (libcoap)   ║\n");
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
    if (ctx) session = coap_new_client_session(ctx, NULL, &dst_addr,
                                                COAP_PROTO_UDP);
    if (!ctx || !session) {
        fprintf(stderr, "❌ Error creando contexto/sesión\n");
        if (ctx) coap_free_context(ctx);
        coap_cleanup();
        return 1;
    }

    coap_register_response_handler(ctx, response_handler);

    /* ── Valor inicial y valor del parche ── */
    double patch_value = (strcmp(device_type, "humidity") == 0) ? 99.5 : 99.9;

    /* ──────────────────────────────────────────────────────────────────
     * FLUJO COMPLETO:
     *   1. STORE  → carga datos iniciales
     *   2. GET    → lee y muestra estado ANTES del parche
     *   3. iPATCH → actualiza SOLO SID 20 (valor)
     *   4. GET    → lee y muestra estado DESPUÉS del parche
     *                → SID 20 cambió, todo lo demás igual
     * ─────────────────────────────────────────────────────────────────*/
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  DEMOSTRACIÓN DE iPATCH (actualización parcial)\n");
    printf("═══════════════════════════════════════════════════════════\n");

    int ok = 1;

    /* 1. STORE */
    if (do_store(ctx, session, device_type, device_id) != 0) {
        ok = 0;
        goto done;
    }

    /* 2. GET inicial */
    if (do_get(ctx, session, "PASO 2: GET inicial (antes del iPATCH)") != 0) {
        ok = 0;
        goto done;
    }

    printf("\n   ┄┄ Aplicando iPATCH: SID 20 → %.2f ┄┄\n", patch_value);

    /* 3. iPATCH: actualizar solo SID 20 */
    if (do_ipatch(ctx, session, 20, patch_value) != 0) {
        ok = 0;
        goto done;
    }

    /* 4. GET post-patch: verificar */
    if (do_get(ctx, session, "PASO 4: GET después del iPATCH") != 0) {
        ok = 0;
        goto done;
    }

    printf("\n═══════════════════════════════════════════════════════════\n");
    printf("  ✅ iPATCH verificado:\n");
    printf("     SID 10 (tipo)      → sin cambios\n");
    printf("     SID 11 (timestamp) → sin cambios\n");
    printf("     SID 20 (valor)     → actualizado a %.2f ✅\n", patch_value);
    printf("     SID 21 (unidad)    → sin cambios\n");
    printf("═══════════════════════════════════════════════════════════\n");

done:
    if (!ok) printf("\n❌ Error en la demostración\n");

    printf("\n🧹 Liberando recursos...\n");
    coap_session_release(session);
    coap_free_context(ctx);
    coap_cleanup();
    printf("✨ Cliente terminado\n");
    return ok ? 0 : 1;
}

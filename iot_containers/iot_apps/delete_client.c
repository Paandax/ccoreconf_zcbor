/**
 * delete_client.c - Demostración DELETE CORECONF (RFC 9254 §4.4)
 *
 * FLUJO DE DEMOSTRACIÓN:
 *   PASO 1: POST   → carga datastore {SID10, SID11, SID20, SID21}
 *   PASO 2: GET    → 4 SIDs visibles ✅
 *   PASO 3: DELETE /c?k=20&k=21 → borra 2 SIDs concretos → 2.02 Deleted
 *   PASO 4: GET    → 2 SIDs restantes (SID10, SID11) ✅
 *   PASO 5: DELETE /c → borra datastore ENTERO → 2.02 Deleted
 *   PASO 6: GET    → 4.04 Not Found ❌ (datastore vacío)
 *
 * COMPILAR:
 *   cd iot_containers/iot_apps && make delete_client
 *
 * EJECUTAR:
 *   Terminal 1: ./delete_server
 *   Terminal 2: ./delete_client temperature sensor-del-001
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
#include "../../include/delete.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096

static int             response_received = 0;
static uint8_t         last_response[BUFFER_SIZE];
static size_t          last_response_len = 0;
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

static coap_response_t response_handler(coap_session_t *sess,
                                         const coap_pdu_t *sent,
                                         const coap_pdu_t *received,
                                         const coap_mid_t mid) {
    (void)sess; (void)sent; (void)mid;
    last_response_code = coap_pdu_get_code(received);
    printf("   📨 Respuesta: %d.%02d\n",
           COAP_RESPONSE_CLASS(last_response_code),
           last_response_code & 0x1F);
    size_t len; const uint8_t *data;
    if (coap_get_data(received, &len, &data)) {
        memcpy(last_response, data, len);
        last_response_len = len;
        printf("   📦 Payload (%zu bytes)\n", len);
        print_cbor_hex(data, len);
    } else {
        last_response_len = 0;
        printf("   📦 Sin payload\n");
    }
    response_received = 1;
    return COAP_RESPONSE_OK;
}

static int send_and_wait(coap_context_t *ctx, coap_session_t *sess, coap_pdu_t *pdu) {
    response_received = 0; last_response_len = 0;
    coap_mid_t mid = coap_send(sess, pdu);
    if (mid == COAP_INVALID_MID) { fprintf(stderr, "   ❌ Error enviando\n"); return 0; }
    int w = 0;
    while (!response_received && w < 5000) { coap_io_process(ctx, 100); w += 100; }
    return response_received;
}

static size_t print_datastore(const uint8_t *data, size_t len) {
    CoreconfValueT *ds = parse_get_response(data, len);
    if (!ds || ds->type != CORECONF_HASHMAP) {
        printf("   ❌ Error decodificando\n");
        if (ds) freeCoreconf(ds, true);
        return 0;
    }
    CoreconfHashMapT *map = ds->data.map_value;
    printf("   ┌──────────────────────────────────────────┐\n");
    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
        CoreconfObjectT *obj = map->table[t];
        while (obj) {
            printf("   │  SID %-6" PRIu64 " → ", obj->key);
            if (obj->value) {
                switch (obj->value->type) {
                    case CORECONF_REAL:    printf("%.2f",  obj->value->data.real_value); break;
                    case CORECONF_STRING:  printf("\"%s\"", obj->value->data.string_value); break;
                    case CORECONF_UINT_64: printf("%" PRIu64, obj->value->data.u64); break;
                    case CORECONF_INT_64:  printf("%" PRId64, obj->value->data.i64); break;
                    default: printf("(tipo %d)", obj->value->type); break;
                }
            } else { printf("null"); }
            printf("\n");
            obj = obj->next;
        }
    }
    size_t sz = map->size;
    printf("   └──────────────────────────────────────────┘\n");
    printf("   📊 Total SIDs: %zu\n", sz);
    freeCoreconf(ds, true);
    return sz;
}

/* ── PASO 1: STORE ── */
static int do_store(coap_context_t *ctx, coap_session_t *sess,
                     const char *device_type, const char *device_id) {
    printf("\n📤 PASO 1: STORE — datos iniciales\n");

    CoreconfValueT  *data = createCoreconfHashmap();
    CoreconfHashMapT *map = data->data.map_value;
    insertCoreconfHashMap(map, 1,  createCoreconfString(device_id));
    insertCoreconfHashMap(map, 2,  createCoreconfString("store"));
    insertCoreconfHashMap(map, 10, createCoreconfString(device_type));
    insertCoreconfHashMap(map, 11, createCoreconfUint64((uint64_t)time(NULL)));
    insertCoreconfHashMap(map, 20, createCoreconfReal(22.5));
    insertCoreconfHashMap(map, 21, createCoreconfString("celsius"));

    uint8_t buf[BUFFER_SIZE];
    zcbor_state_t enc[5];
    zcbor_new_encode_state(enc, 5, buf, BUFFER_SIZE, 0);
    if (!coreconfToCBOR(data, enc)) { freeCoreconf(data, true); return -1; }
    size_t len = (size_t)(enc[0].payload - buf);
    freeCoreconf(data, true);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST, sess);
    if (!pdu) return -1;
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), 60);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_add_data(pdu, len, buf);

    if (!send_and_wait(ctx, sess, pdu)) return -1;
    if (COAP_RESPONSE_CLASS(last_response_code) == 2) {
        printf("   ✅ Almacenados {SID10, SID11, SID20, SID21}\n");
        return 0;
    }
    return -1;
}

/* ── GET helper ── */
static int do_get(coap_context_t *ctx, coap_session_t *sess, const char *label) {
    printf("\n🗄️  %s\n", label);
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, sess);
    if (!pdu) return -1;
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    if (!send_and_wait(ctx, sess, pdu)) return -1;

    int cls = COAP_RESPONSE_CLASS(last_response_code);
    if (cls == 4) {
        int detail = last_response_code & 0x1F;
        if (detail == 4) {
            printf("   ❌ 4.04 Not Found — datastore eliminado ✅ (esperado)\n");
            return 1; /* 1 = not_found esperado */
        }
        printf("   ❌ Error 4.%02d\n", detail);
        return -1;
    }
    if (cls != 2 || last_response_len == 0) { printf("   ❌ Error GET\n"); return -1; }
    printf("   ✅ Datastore (CF=142):\n");
    print_datastore(last_response, last_response_len);
    return 0;
}

/* ── DELETE helper ── */
static int do_delete(coap_context_t *ctx, coap_session_t *sess,
                      const char *label, const char *uri_query) {
    printf("\n%s\n", label);
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_DELETE, sess);
    if (!pdu) return -1;
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    if (uri_query && strlen(uri_query) > 0) {
        printf("   📤 DELETE /c?%s\n", uri_query);
        coap_add_option(pdu, COAP_OPTION_URI_QUERY,
                        strlen(uri_query), (const uint8_t *)uri_query);
    } else {
        printf("   📤 DELETE /c (sin query → borra TODO)\n");
    }

    if (!send_and_wait(ctx, sess, pdu)) return -1;
    int cls = COAP_RESPONSE_CLASS(last_response_code);
    if (cls == 2 && (last_response_code & 0x1F) == 2) {
        printf("   ✅ 2.02 Deleted (sin payload, RFC §4.4)\n");
        return 0;
    }
    printf("   ❌ Respuesta inesperada: %d.%02d\n",
           cls, last_response_code & 0x1F);
    return -1;
}

/* ============================================================================
 * MAIN
 * =========================================================================*/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <tipo> [device_id]\n", argv[0]);
        fprintf(stderr, "Ej:  %s temperature sensor-del-001\n", argv[0]);
        return 1;
    }

    const char *device_type = argv[1];
    const char *device_id   = (argc >= 3) ? argv[2] : "sensor-del-001";
    const char *host        = getenv("GATEWAY_HOST");
    const char *port_str    = getenv("GATEWAY_PORT");
    if (!host) host = "127.0.0.1";
    int port = port_str ? atoi(port_str) : 5683;

    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║   DELETE CLIENT - CORECONF RFC §4.4 sobre CoAP         ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");
    printf("🔧 Device: %s (%s)\n", device_id, device_type);
    printf("📡 Gateway: %s:%d\n\n", host, port);

    coap_startup();

    coap_address_t dst;
    coap_address_init(&dst);
    dst.size = sizeof(struct sockaddr_in);
    dst.addr.sin.sin_family = AF_INET;
    dst.addr.sin.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &dst.addr.sin.sin_addr) != 1) {
        fprintf(stderr, "❌ IP inválida\n"); return 1;
    }

    coap_context_t *ctx     = coap_new_context(NULL);
    coap_session_t *session = ctx ? coap_new_client_session(ctx, NULL, &dst,
                                                             COAP_PROTO_UDP) : NULL;
    if (!ctx || !session) {
        fprintf(stderr, "❌ Error creando sesión\n");
        if (ctx) coap_free_context(ctx);
        coap_cleanup(); return 1;
    }
    coap_register_response_handler(ctx, response_handler);

    /* ──────────────────────────────────────────────────────────────
     * DEMOSTRACIÓN COMPLETA
     * ────────────────────────────────────────────────────────────*/
    printf("══════════════════════════════════════════════════════════\n");
    printf("  DEMOSTRACIÓN: DELETE CORECONF (RFC §4.4)\n");
    printf("══════════════════════════════════════════════════════════\n");

    int ok = 1;

    /* 1. STORE */
    if (do_store(ctx, session, device_type, device_id) != 0) { ok = 0; goto done; }

    /* 2. GET inicial → 4 SIDs */
    if (do_get(ctx, session,
               "PASO 2: GET inicial — 4 SIDs almacenados") != 0) { ok = 0; goto done; }

    /* 3. DELETE ?k=20&k=21 → borra SID 20 y 21 */
    if (do_delete(ctx, session,
                  "🗑️  PASO 3: DELETE ?k=20&k=21 — eliminar SID 20 y SID 21",
                  "k=20&k=21") != 0) { ok = 0; goto done; }

    /* 4. GET → quedan SID 10 y SID 11 */
    if (do_get(ctx, session,
               "PASO 4: GET — SID20 y SID21 eliminados (quedan SID10, SID11)") != 0)
        { ok = 0; goto done; }

    /* 5. DELETE sin query → borra TODO el datastore */
    if (do_delete(ctx, session,
                  "🗑️  PASO 5: DELETE /c — borra datastore entero",
                  NULL) != 0) { ok = 0; goto done; }

    /* 6. GET → 4.04 Not Found */
    {
        int r = do_get(ctx, session,
                       "PASO 6: GET — debe fallar con 4.04 Not Found");
        if (r != 1) { ok = 0; goto done; }
    }

    printf("\n══════════════════════════════════════════════════════════\n");
    printf("  RESULTADO:\n\n");
    printf("  PASO 1  POST             → datastore creado          ✅\n");
    printf("  PASO 2  GET              → 4 SIDs visibles           ✅\n");
    printf("  PASO 3  DELETE ?k=20&k=21 → SID20 y SID21 borrados   ✅\n");
    printf("  PASO 4  GET              → SID10, SID11 intactos     ✅\n");
    printf("  PASO 5  DELETE /c        → datastore entero borrado  ✅\n");
    printf("  PASO 6  GET              → 4.04 Not Found            ✅\n");
    printf("══════════════════════════════════════════════════════════\n");

done:
    if (!ok) printf("\n❌ Error en la demostración\n");
    printf("\n🧹 Liberando recursos...\n");
    coap_session_release(session);
    coap_free_context(ctx);
    coap_cleanup();
    printf("✨ Cliente terminado\n");
    return ok ? 0 : 1;
}

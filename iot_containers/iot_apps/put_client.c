/**
 * put_client.c - Cliente IoT que demuestra PUT vs iPATCH (CORECONF RFC §4.3)
 *
 * DEMOSTRACIÓN CLAVE - diferencia PUT vs iPATCH:
 *
 *   Datastore inicial: { SID10:"temperature", SID11:ts, SID20:22.5, SID21:"celsius" }
 *
 *   PASO 3a — iPATCH { 20: 99.9 }
 *     Resultado: { SID10:"temperature", SID11:ts, SID20:99.9, SID21:"celsius" }
 *                  SID10/11/21 INTACTOS ─────────────────────────────────────────
 *
 *   PASO 3b — PUT { 30: "new-sensor", 31: 0.0 }
 *     Resultado: { SID30:"new-sensor", SID31:0.0 }
 *                  SID10, SID11, SID20, SID21 ELIMINADOS ──────────────────────
 *
 * FLUJO COMPLETO:
 *   1. STORE   → POST /c CF=60   → datos iniciales (4 SIDs)
 *   2. GET     → leer datastore  → 4 SIDs
 *   3. iPATCH  → CF=141 { 20:99.9 }  → solo SID 20 cambia
 *   4. GET     → 4 SIDs (iPATCH preservó los otros)
 *   5. PUT     → CF=142 { 30:"new-sensor", 31:0.0 }  → REEMPLAZO TOTAL
 *   6. GET     → 2 SIDs (SID10/11/20/21 desaparecieron)
 *
 * COMPILAR:
 *   cd iot_containers/iot_apps && make put_client
 *
 * EJECUTAR:
 *   Terminal 1: ./put_server
 *   Terminal 2: ./put_client temperature sensor-put-001
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
#include "../../include/ipatch.h"
#include "../../include/put.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096

/* ── Estado compartido ── */
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
        printf("   📦 Payload (%zu bytes)\n", len);
        print_cbor_hex(data, len);
    } else {
        last_response_len = 0;
        printf("   📦 Sin payload\n");
    }
    response_received = 1;
    return COAP_RESPONSE_OK;
}

static int send_and_wait(coap_context_t *ctx, coap_session_t *session,
                          coap_pdu_t *pdu) {
    response_received = 0;
    last_response_len = 0;
    coap_mid_t mid = coap_send(session, pdu);
    if (mid == COAP_INVALID_MID) { fprintf(stderr, "   ❌ Error enviando\n"); return 0; }
    int waited = 0;
    while (!response_received && waited < 5000) {
        coap_io_process(ctx, 100); waited += 100;
    }
    return response_received;
}

/* ── Mostrar datastore decodificado ── */
static size_t print_datastore(const uint8_t *data, size_t len) {
    CoreconfValueT *ds = parse_get_response(data, len);
    if (!ds || ds->type != CORECONF_HASHMAP) {
        printf("   ❌ Error decodificando datastore\n");
        if (ds) freeCoreconf(ds, true);
        return 0;
    }
    CoreconfHashMapT *map = ds->data.map_value;
    printf("   ┌─────────────────────────────────────────┐\n");
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
                    case CORECONF_UINT_32: printf("%" PRIu32, obj->value->data.u32); break;
                    case CORECONF_TRUE:    printf("true");  break;
                    case CORECONF_FALSE:   printf("false"); break;
                    case CORECONF_NULL:    printf("null");  break;
                    default: printf("(tipo %d)", obj->value->type); break;
                }
            } else { printf("null"); }
            printf("\n");
            obj = obj->next;
        }
    }
    size_t sz = map->size;
    printf("   └─────────────────────────────────────────┘\n");
    printf("   📊 Total SIDs: %zu\n", sz);
    freeCoreconf(ds, true);
    return sz;
}

/* ── PASO 1: STORE ── */
static int do_store(coap_context_t *ctx, coap_session_t *session,
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
    if (!coreconfToCBOR(data, enc)) {
        freeCoreconf(data, true); return -1;
    }
    size_t len = (size_t)(enc[0].payload - buf);
    printf("   📦 CBOR (%zu bytes)\n", len);
    print_cbor_hex(buf, len);
    freeCoreconf(data, true);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST, session);
    if (!pdu) return -1;
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), 60);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_add_data(pdu, len, buf);
    printf("   📤 POST /c...\n");
    if (!send_and_wait(ctx, session, pdu)) return -1;
    if (COAP_RESPONSE_CLASS(last_response_code) == 2) {
        printf("   ✅ STORE → 2.04 Changed\n"); return 0;
    }
    return -1;
}

/* ── GET helper ── */
static int do_get(coap_context_t *ctx, coap_session_t *session,
                   const char *label) {
    printf("\n🗄️  %s\n", label);
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, session);
    if (!pdu) return -1;
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    printf("   📤 GET /c...\n");
    if (!send_and_wait(ctx, session, pdu)) return -1;
    if (COAP_RESPONSE_CLASS(last_response_code) != 2 || last_response_len == 0) {
        printf("   ❌ Error GET\n"); return -1;
    }
    printf("\n   ✅ Datastore (CF=142):\n");
    print_datastore(last_response, last_response_len);
    return 0;
}

/* ── PASO 3a: iPATCH — actualización PARCIAL ── */
static int do_ipatch(coap_context_t *ctx, coap_session_t *session) {
    printf("\n🔧 PASO 3a: iPATCH — actualización PARCIAL\n");
    printf("   📝 Enviamos SOLO { SID20: 99.9 }\n");
    printf("   📝 SID10, SID11, SID21 → deben quedar INTACTOS\n");

    CoreconfValueT  *patch = createCoreconfHashmap();
    insertCoreconfHashMap(patch->data.map_value, 20, createCoreconfReal(99.9));

    uint8_t buf[BUFFER_SIZE];
    size_t len = create_ipatch_request(buf, BUFFER_SIZE, patch);
    freeCoreconf(patch, true);
    if (len == 0) { printf("   ❌ Error serializando parche\n"); return -1; }

    printf("   📦 CBOR parche (%zu bytes): ", len);
    print_cbor_hex(buf, len);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_IPATCH, session);
    if (!pdu) return -1;
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf),
                                                      COAP_MEDIA_TYPE_YANG_PATCH_CBOR);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_add_data(pdu, len, buf);
    printf("   📤 iPATCH /c (CF=141)...\n");
    if (!send_and_wait(ctx, session, pdu)) return -1;
    if (COAP_RESPONSE_CLASS(last_response_code) == 2) {
        printf("   ✅ iPATCH → 2.04 Changed\n"); return 0;
    }
    return -1;
}

/* ── PASO 5: PUT — REEMPLAZO TOTAL ── */
static int do_put(coap_context_t *ctx, coap_session_t *session) {
    printf("\n🔄 PASO 5: PUT — REEMPLAZO TOTAL del datastore\n");
    printf("   📝 Enviamos { SID30:\"new-sensor\", SID31:0.0 } — SIDs COMPLETAMENTE DISTINTOS\n");
    printf("   📝 SID10, SID11, SID20, SID21 → deben DESAPARECER\n");

    /* Nuevo datastore con SIDs completamente distintos */
    CoreconfValueT  *new_ds = createCoreconfHashmap();
    CoreconfHashMapT *pm    = new_ds->data.map_value;
    insertCoreconfHashMap(pm, 30, createCoreconfString("new-sensor"));
    insertCoreconfHashMap(pm, 31, createCoreconfReal(0.0));

    uint8_t buf[BUFFER_SIZE];
    size_t len = create_put_request(buf, BUFFER_SIZE, new_ds);
    freeCoreconf(new_ds, true);
    if (len == 0) { printf("   ❌ Error serializando datastore\n"); return -1; }

    printf("   📦 CBOR nuevo datastore (%zu bytes): ", len);
    print_cbor_hex(buf, len);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_PUT, session);
    if (!pdu) return -1;
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf),
                                                      COAP_MEDIA_TYPE_YANG_DATA_CBOR);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_add_data(pdu, len, buf);
    printf("   📤 PUT /c (CF=142)...\n");
    if (!send_and_wait(ctx, session, pdu)) return -1;

    int cls = COAP_RESPONSE_CLASS(last_response_code);
    if (cls == 2) {
        int code_detail = last_response_code & 0x1F;
        if (code_detail == 1)
            printf("   ✅ PUT → 2.01 Created (recurso nuevo)\n");
        else
            printf("   ✅ PUT → 2.04 Changed (recurso existía)\n");
        return 0;
    }
    return -1;
}

/* ============================================================================
 * MAIN
 * =========================================================================*/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <tipo_dispositivo> [device_id]\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s temperature sensor-put-001\n", argv[0]);
        return 1;
    }

    const char *device_type  = argv[1];
    const char *device_id    = (argc >= 3) ? argv[2] : "sensor-put-001";
    const char *gateway_host = getenv("GATEWAY_HOST");
    const char *gw_port_str  = getenv("GATEWAY_PORT");
    if (!gateway_host) gateway_host = "127.0.0.1";
    int gateway_port = gw_port_str ? atoi(gw_port_str) : 5683;

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   PUT CLIENT - CORECONF PUT vs iPATCH sobre CoAP (libcoap) ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    printf("🔧 Device: %s (%s)\n", device_id, device_type);
    printf("📡 Gateway: %s:%d\n\n", gateway_host, gateway_port);

    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    coap_startup();

    coap_address_t dst;
    coap_address_init(&dst);
    dst.size = sizeof(struct sockaddr_in);
    dst.addr.sin.sin_family = AF_INET;
    dst.addr.sin.sin_port   = htons(gateway_port);
    if (inet_pton(AF_INET, gateway_host, &dst.addr.sin.sin_addr) != 1) {
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

    /* ────────────────────────────────────────────────────────────
     * DEMOSTRACIÓN COMPLETA: PUT vs iPATCH
     * ──────────────────────────────────────────────────────────*/
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  DEMOSTRACIÓN: PUT (reemplazo total) vs iPATCH (parcial)\n");
    printf("══════════════════════════════════════════════════════════════\n");

    int ok = 1;

    /* 1. STORE inicial */
    if (do_store(ctx, session, device_type, device_id) != 0) { ok = 0; goto done; }

    /* 2. GET inicial → 4 SIDs */
    if (do_get(ctx, session, "PASO 2: GET inicial (4 SIDs: 10,11,20,21)") != 0)
        { ok = 0; goto done; }

    /* 3a. iPATCH → solo SID 20 cambia */
    if (do_ipatch(ctx, session) != 0) { ok = 0; goto done; }

    /* 4. GET tras iPATCH → siguen 4 SIDs, SID 20 actualizado */
    if (do_get(ctx, session, "PASO 4: GET tras iPATCH (siguen 4 SIDs, SID20=99.9)") != 0)
        { ok = 0; goto done; }

    printf("\n   ┄┄ Ahora aplicamos PUT con SIDs COMPLETAMENTE DISTINTOS ┄┄\n");

    /* 5. PUT → reemplaza TODO con { SID30, SID31 } */
    if (do_put(ctx, session) != 0) { ok = 0; goto done; }

    /* 6. GET tras PUT → solo 2 SIDs, los anteriores desaparecieron */
    if (do_get(ctx, session, "PASO 6: GET tras PUT (solo 2 SIDs: 30,31)") != 0)
        { ok = 0; goto done; }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf("  RESULTADO — diferencia demostrada:\n\n");
    printf("  iPATCH { SID20: 99.9 }           PUT { SID30:\"new-sensor\", SID31:0.0 }\n");
    printf("  ──────────────────────────────    ─────────────────────────────────────\n");
    printf("  SID10 \"temperature\" → INTACTO    SID10 \"temperature\" → ELIMINADO ❌\n");
    printf("  SID11  timestamp    → INTACTO    SID11  timestamp    → ELIMINADO ❌\n");
    printf("  SID20  22.5→99.9    → CAMBIA  ✅  SID20  99.9         → ELIMINADO ❌\n");
    printf("  SID21 \"celsius\"     → INTACTO    SID21 \"celsius\"     → ELIMINADO ❌\n");
    printf("                                   SID30 \"new-sensor\"  → CREADO    ✅\n");
    printf("                                   SID31  0.0           → CREADO    ✅\n");
    printf("══════════════════════════════════════════════════════════════\n");

done:
    if (!ok) printf("\n❌ Error en la demostración\n");
    printf("\n🧹 Liberando recursos...\n");
    coap_session_release(session);
    coap_free_context(ctx);
    coap_cleanup();
    printf("✨ Cliente terminado\n");
    return ok ? 0 : 1;
}

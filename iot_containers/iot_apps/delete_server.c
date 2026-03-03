/**
 * delete_server.c - Servidor CORECONF con DELETE (RFC 9254 §4.4)
 *
 * OPERACIONES:
 *   POST   /c  → STORE: carga datos iniciales (CF=60)
 *   GET    /c  → LEE datastore (CF=142); 4.04 si está vacío
 *   DELETE /c?k=SID[&k=SID] → elimina SIDs concretos → 2.02 Deleted
 *   DELETE /c  → borra el datastore entero → 2.02 Deleted
 *
 * RFC COMPLIANCE:
 *   §4.4   DELETE → 2.02 Deleted (sin payload)
 *   §4.4   DELETE recurso inexistente → 4.04 Not Found
 *   §4     Block-wise: COAP_BLOCK_USE_LIBCOAP
 *   §5     Discovery: rt="core.c.ds"
 *   §6     Errores CBOR CF=142
 */

#include <coap3/coap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <signal.h>

#include "../../include/coreconfTypes.h"
#include "../../include/serialization.h"
#include "../../include/get.h"
#include "../../include/delete.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096
static int quit = 0;

/* ── Datastore global (servidor único) ── */
static CoreconfValueT *g_datastore = NULL;

/* ── Helpers ── */
static void print_cbor_hex(const uint8_t *data, size_t len) {
    printf("   ");
    for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i < len - 1) printf("\n   ");
    }
    if (len > 64) printf("... (%zu bytes total)", len);
    printf("\n");
}

static size_t encode_cbor_error(uint8_t *buf, size_t buf_size,
                                 uint64_t error_tag_sid, const char *message) {
    size_t msg_len = strlen(message);
    if (buf_size < 16 + msg_len) return 0;
    size_t pos = 0;
    buf[pos++] = 0xa1;
    buf[pos++] = 0x19; buf[pos++] = 0x04; buf[pos++] = 0x00;
    buf[pos++] = 0xa2;
    buf[pos++] = 0x04;
    if (error_tag_sid < 24) {
        buf[pos++] = (uint8_t)error_tag_sid;
    } else if (error_tag_sid < 256) {
        buf[pos++] = 0x18; buf[pos++] = (uint8_t)error_tag_sid;
    } else {
        buf[pos++] = 0x19;
        buf[pos++] = (uint8_t)(error_tag_sid >> 8);
        buf[pos++] = (uint8_t)(error_tag_sid & 0xFF);
    }
    buf[pos++] = 0x03;
    if (msg_len < 24) {
        buf[pos++] = (uint8_t)(0x60 | msg_len);
    } else {
        buf[pos++] = 0x78; buf[pos++] = (uint8_t)msg_len;
    }
    memcpy(buf + pos, message, msg_len);
    return pos + msg_len;
}

static void send_cbor_error(coap_pdu_t *response, coap_pdu_code_t code,
                              uint64_t esid, const char *msg) {
    uint8_t buf[512];
    size_t len = encode_cbor_error(buf, sizeof(buf), esid, msg);
    coap_pdu_set_code(response, code);
    if (len > 0) {
        uint8_t cf[4];
        size_t cfl = coap_encode_var_safe(cf, sizeof(cf), COAP_MEDIA_TYPE_YANG_DATA_CBOR);
        coap_add_option(response, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
        coap_add_data(response, len, buf);
    }
}

typedef struct { CoreconfHashMapT *target_map; } CopyCtx;
static void copy_sid(CoreconfObjectT *obj, void *udata) {
    CopyCtx *ctx = (CopyCtx *)udata;
    if (obj->key >= 10 && obj->value) {
        CoreconfValueT *cp = malloc(sizeof(CoreconfValueT));
        memcpy(cp, obj->value, sizeof(CoreconfValueT));
        if (obj->value->type == CORECONF_STRING && obj->value->data.string_value)
            cp->data.string_value = strdup(obj->value->data.string_value);
        insertCoreconfHashMap(ctx->target_map, obj->key, cp);
    }
}

/* ============================================================================
 * POST /c → STORE
 * =========================================================================*/
static void handle_post_store(coap_resource_t *rsrc, coap_session_t *sess,
                               const coap_pdu_t *req, const coap_string_t *query,
                               coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)query;
    printf("\n📥 POST /c (STORE)\n");

    size_t len; const uint8_t *data;
    if (!coap_get_data(req, &len, &data)) {
        send_cbor_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "missing payload");
        return;
    }
    print_cbor_hex(data, len);

    zcbor_state_t st[5];
    zcbor_new_decode_state(st, 5, data, len, 1, NULL, 0);
    CoreconfValueT *recv = cborToCoreconfValue(st, 0);
    if (!recv || recv->type != CORECONF_HASHMAP) {
        send_cbor_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "bad payload");
        if (recv) freeCoreconf(recv, true);
        return;
    }

    CoreconfValueT *id_val = getCoreconfHashMap(recv->data.map_value, 1);
    const char *device_id = (id_val && id_val->type == CORECONF_STRING)
                             ? id_val->data.string_value : "unknown";
    printf("   📟 Device: %s\n", device_id);

    if (g_datastore) { freeCoreconf(g_datastore, true); g_datastore = NULL; }
    g_datastore = createCoreconfHashmap();
    CopyCtx ctx = { g_datastore->data.map_value };
    iterateCoreconfHashMap(recv->data.map_value, &ctx, copy_sid);
    freeCoreconf(recv, true);

    printf("   ✅ Almacenados %zu SIDs\n", g_datastore->data.map_value->size);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CHANGED);
}

/* ============================================================================
 * GET /c → leer datastore; 4.04 si está vacío
 * =========================================================================*/
static void handle_get_coreconf(coap_resource_t *rsrc, coap_session_t *sess,
                                 const coap_pdu_t *req, const coap_string_t *query,
                                 coap_pdu_t *resp) {
    (void)req;
    printf("\n📤 GET /c → ");

    if (!g_datastore || g_datastore->data.map_value->size == 0) {
        printf("datastore vacío → 4.04 Not Found\n");
        send_cbor_error(resp, COAP_RESPONSE_CODE_NOT_FOUND, 1013,
                        "datastore is empty");
        return;
    }

    uint8_t buf[BUFFER_SIZE];
    size_t len = create_get_response(buf, BUFFER_SIZE, g_datastore);
    if (len == 0) {
        send_cbor_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1013,
                        "serialization error");
        return;
    }

    /* RFC §4: block-wise via COAP_BLOCK_USE_LIBCOAP */
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data_large_response(rsrc, sess, req, resp, query,
                                  COAP_MEDIA_TYPE_YANG_DATA_CBOR,
                                  -1, 0, len, buf, NULL, NULL);
    printf("2.05 Content (%zu bytes, %zu SIDs)\n",
           len, g_datastore->data.map_value->size);
}

/* ============================================================================
 * DELETE /c[?k=SID[&k=SID]] → elimina SIDs o el datastore entero
 * =========================================================================*/
static void handle_delete_coreconf(coap_resource_t *rsrc, coap_session_t *sess,
                                    const coap_pdu_t *req, const coap_string_t *query,
                                    coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)req;
    printf("\n🗑️  DELETE /c");

    /* ── Datastore no existe → 4.04 ── */
    if (!g_datastore || g_datastore->data.map_value->size == 0) {
        printf(" → 4.04 Not Found (ya vacío)\n");
        send_cbor_error(resp, COAP_RESPONSE_CODE_NOT_FOUND, 1013,
                        "datastore not found");
        return;
    }

    /* ── Sin query → borrar TODO el datastore ── */
    if (!query || query->length == 0) {
        size_t old_size = g_datastore->data.map_value->size;
        freeCoreconf(g_datastore, true);
        g_datastore = NULL;
        printf(" (sin query)\n");
        printf("   🗑️  Datastore eliminado (%zu SIDs borrados)\n", old_size);
        coap_pdu_set_code(resp, COAP_RESPONSE_CODE_DELETED);
        return;
    }

    /* ── Query ?k=SID[&k=SID] → borrar SIDs concretos ── */
    printf("?%.*s\n", (int)query->length, (const char *)query->s);
    uint64_t sids[DELETE_MAX_SIDS];
    int n = parse_delete_query(query->s, query->length, sids, DELETE_MAX_SIDS);
    if (n == 0) {
        send_cbor_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012,
                        "invalid query: use k=<SID>");
        return;
    }

    int deleted = apply_delete(g_datastore->data.map_value, sids, n);
    printf("   🗑️  %d/%d SID(s) eliminados | %zu restantes\n",
           deleted, n, g_datastore->data.map_value->size);

    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_DELETED);
    /* RFC §4.4: 2.02 Deleted, sin payload */
}

/* ============================================================================
 * MAIN
 * =========================================================================*/
static void sig_handler(int sig) { (void)sig; quit = 1; }

int main(void) {
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║   DELETE SERVER - CORECONF RFC §4.4 (libcoap)          ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");
    printf("🌐 Escuchando en coap://0.0.0.0:5683\n");
    printf("📋 Endpoints:\n");
    printf("   POST   /c           → STORE (CF=60)\n");
    printf("   GET    /c           → READ  (CF=142) | 4.04 si vacío\n");
    printf("   DELETE /c?k=SID     → elimina SIDs específicos → 2.02\n");
    printf("   DELETE /c           → borra datastore entero   → 2.02\n\n");

    coap_startup();
    coap_set_log_level(COAP_LOG_WARN);

    coap_address_t addr;
    coap_address_init(&addr);
    addr.addr.sin.sin_family      = AF_INET;
    addr.addr.sin.sin_addr.s_addr = INADDR_ANY;
    addr.addr.sin.sin_port        = htons(5683);
    addr.size                     = sizeof(struct sockaddr_in);

    coap_context_t *ctx = coap_new_context(&addr);
    if (!ctx) { fprintf(stderr, "❌ Error creando contexto\n"); return 1; }

    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);

    coap_resource_t *res = coap_resource_init(coap_make_str_const("c"),
                                               COAP_RESOURCE_FLAGS_NOTIFY_CON);
    coap_register_handler(res, COAP_REQUEST_POST,   handle_post_store);
    coap_register_handler(res, COAP_REQUEST_GET,    handle_get_coreconf);
    coap_register_handler(res, COAP_REQUEST_DELETE, handle_delete_coreconf);
    coap_resource_set_get_observable(res, 0);

    coap_attr_t *attr;
    attr = coap_add_attr(res, coap_make_str_const("rt"),
                         coap_make_str_const("\"core.c.ds\""), 0);
    (void)attr;

    coap_add_resource(ctx, res);

    printf("✅ Servidor listo\n\n");
    while (!quit) { coap_io_process(ctx, 1000); }

    printf("\n🧹 Liberando recursos...\n");
    if (g_datastore) freeCoreconf(g_datastore, true);
    coap_free_context(ctx);
    coap_cleanup();
    printf("✨ Servidor terminado\n");
    return 0;
}

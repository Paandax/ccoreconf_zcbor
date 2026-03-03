/**
 * coreconf_server.c - Servidor CORECONF unificado (RFC 9254)
 *
 * Soporta TODOS los métodos CORECONF en /c:
 *   POST   /c  → STORE: carga datos iniciales (CF=60)
 *   FETCH  /c  → lee SIDs específicos        (CF=141 req, CF=142 resp)
 *   GET    /c  → lee datastore completo      (CF=142 resp)
 *   iPATCH /c  → actualización parcial       (CF=141 req, sin payload resp)
 *   PUT    /c  → reemplazo total             (CF=142 req, sin payload resp)
 *   DELETE /c[?k=SID] → elimina SID(s)      (sin CF)
 *
 * Datastore multi-dispositivo: cada device_id tiene su propio datastore.
 */

#include <coap3/coap.h>
/* Compat: libcoap3-dev de Ubuntu 22.04 puede usar nombres distintos */
#ifndef COAP_LOG_WARN
#define COAP_LOG_WARN 4
#endif
#ifndef COAP_LOG_ERR
#define COAP_LOG_ERR  3
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <signal.h>

#include "../../include/coreconfTypes.h"
#include "../../include/serialization.h"
#include "../../include/fetch.h"
#include "../../include/get.h"
#include "../../include/ipatch.h"
#include "../../include/put.h"
#include "../../include/delete.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096
#define MAX_DEVICES 32

static int quit = 0;

/* ── Datastore por dispositivo ── */
typedef struct {
    char           device_id[64];
    CoreconfValueT *data;
    int            exists;
} Device;

static Device devices[MAX_DEVICES];
static int    device_count = 0;

/* ────────────────────────────────────────────────────────────
 * Helpers
 * ──────────────────────────────────────────────────────────*/
static void print_cbor_hex(const char *prefix, const uint8_t *d, size_t len) {
    printf("%s(%zu bytes): ", prefix, len);
    for (size_t i = 0; i < len && i < 32; i++) printf("%02x ", d[i]);
    if (len > 32) printf("...");
    printf("\n");
}

static uint16_t get_content_format(const coap_pdu_t *pdu) {
    coap_opt_iterator_t oi;
    coap_opt_t *opt = coap_check_option(pdu, COAP_OPTION_CONTENT_FORMAT, &oi);
    if (!opt) return 0xFFFF;
    return (uint16_t)coap_decode_var_bytes(coap_opt_value(opt), coap_opt_length(opt));
}

/* RFC §6 error payload: { 1024: { 4: error_sid, 3: "msg" } } */
static size_t encode_error(uint8_t *buf, size_t sz, uint64_t esid, const char *msg) {
    size_t ml = strlen(msg);
    if (sz < 16 + ml) return 0;
    size_t p = 0;
    buf[p++] = 0xa1;
    buf[p++] = 0x19; buf[p++] = 0x04; buf[p++] = 0x00;
    buf[p++] = 0xa2;
    buf[p++] = 0x04;
    if (esid < 24)        { buf[p++] = (uint8_t)esid; }
    else if (esid < 256)  { buf[p++] = 0x18; buf[p++] = (uint8_t)esid; }
    else                  { buf[p++] = 0x19; buf[p++] = (uint8_t)(esid>>8); buf[p++] = (uint8_t)(esid&0xFF); }
    buf[p++] = 0x03;
    if (ml < 24)  { buf[p++] = (uint8_t)(0x60 | ml); }
    else          { buf[p++] = 0x78; buf[p++] = (uint8_t)ml; }
    memcpy(buf + p, msg, ml);
    return p + ml;
}

static void send_error(coap_pdu_t *resp, coap_pdu_code_t code,
                        uint64_t esid, const char *msg) {
    uint8_t buf[512];
    size_t  len = encode_error(buf, sizeof(buf), esid, msg);
    coap_pdu_set_code(resp, code);
    if (len > 0) {
        uint8_t cf[4];
        size_t cfl = coap_encode_var_safe(cf, sizeof(cf), COAP_MEDIA_TYPE_YANG_DATA_CBOR);
        coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
        coap_add_data(resp, len, buf);
    }
}

/* ── Buscar / crear dispositivo por device_id ── */
static Device *find_device(const char *id) {
    for (int i = 0; i < device_count; i++)
        if (strcmp(devices[i].device_id, id) == 0)
            return &devices[i];
    return NULL;
}

static Device *find_or_create_device(const char *id) {
    Device *d = find_device(id);
    if (d) return d;
    if (device_count >= MAX_DEVICES) return NULL;
    strncpy(devices[device_count].device_id, id, 63);
    devices[device_count].data   = NULL;
    devices[device_count].exists = 0;
    return &devices[device_count++];
}

/* Copia SIDs ≥ 10 del mapa src → mapa dst */
typedef struct { CoreconfHashMapT *dst; } CopyCtx;
static void copy_sid(CoreconfObjectT *obj, void *udata) {
    CopyCtx *ctx = (CopyCtx *)udata;
    if (obj->key >= 10 && obj->value) {
        CoreconfValueT *cp = malloc(sizeof(CoreconfValueT));
        memcpy(cp, obj->value, sizeof(CoreconfValueT));
        if (obj->value->type == CORECONF_STRING && obj->value->data.string_value)
            cp->data.string_value = strdup(obj->value->data.string_value);
        insertCoreconfHashMap(ctx->dst, obj->key, cp);
    }
}

/* ── Obtener device_id de la query "id=<val>" ── */
static int get_query_device_id(const coap_string_t *q, char *out, size_t out_sz) {
    if (!q || q->length == 0) return 0;
    const char *s = (const char *)q->s;
    size_t      l = q->length;
    const char *p = s;
    while ((size_t)(p - s) < l) {
        if (strncmp(p, "id=", 3) == 0) {
            p += 3;
            const char *end = memchr(p, '&', l - (size_t)(p - s));
            size_t vl = end ? (size_t)(end - p) : l - (size_t)(p - s);
            if (vl >= out_sz) vl = out_sz - 1;
            memcpy(out, p, vl);
            out[vl] = '\0';
            return 1;
        }
        const char *amp = memchr(p, '&', l - (size_t)(p - s));
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

/* ════════════════════════════════════════════════════════════
 * POST /c  — STORE
 * ══════════════════════════════════════════════════════════*/
static void handle_post(coap_resource_t *rsrc, coap_session_t *sess,
                         const coap_pdu_t *req, const coap_string_t *query,
                         coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)query;
    size_t len; const uint8_t *data;
    if (!coap_get_data(req, &len, &data)) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "missing payload");
        return;
    }
    print_cbor_hex("[STORE] payload", data, len);

    zcbor_state_t st[5];
    zcbor_new_decode_state(st, 5, data, len, 1, NULL, 0);
    CoreconfValueT *recv = cborToCoreconfValue(st, 0);
    if (!recv || recv->type != CORECONF_HASHMAP) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "bad CBOR");
        if (recv) freeCoreconf(recv, true);
        return;
    }

    CoreconfValueT *id_v = getCoreconfHashMap(recv->data.map_value, 1);
    const char *device_id = (id_v && id_v->type == CORECONF_STRING)
                             ? id_v->data.string_value : "unknown";

    Device *dev = find_or_create_device(device_id);
    if (!dev) {
        send_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1019, "too many devices");
        freeCoreconf(recv, true); return;
    }

    if (dev->data) { freeCoreconf(dev->data, true); dev->data = NULL; }
    dev->data = createCoreconfHashmap();
    CopyCtx ctx = { dev->data->data.map_value };
    iterateCoreconfHashMap(recv->data.map_value, &ctx, copy_sid);
    freeCoreconf(recv, true);
    dev->exists = 1;

    printf("[STORE] device=%s  SIDs=%zu\n", device_id, dev->data->data.map_value->size);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CHANGED);
}

/* ════════════════════════════════════════════════════════════
 * FETCH /c  — leer SIDs específicos (CF=141 req, CF=142 resp)
 * ══════════════════════════════════════════════════════════*/
static void handle_fetch(coap_resource_t *rsrc, coap_session_t *sess,
                          const coap_pdu_t *req, const coap_string_t *query,
                          coap_pdu_t *resp) {
    (void)rsrc; (void)sess;
    /* CF=141 requerido */
    uint16_t cf = get_content_format(req);
    if (cf != COAP_MEDIA_TYPE_YANG_PATCH_CBOR) {
        send_error(resp, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                   1012, "FETCH requires CF=141");
        return;
    }

    size_t len; const uint8_t *data;
    if (!coap_get_data(req, &len, &data) || len == 0) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "missing SID list");
        return;
    }
    print_cbor_hex("[FETCH] req", data, len);

    /* Elegir dispositivo */
    char dev_id[64] = "unknown";
    get_query_device_id(query, dev_id, sizeof(dev_id));
    Device *dev = find_device(dev_id);
    if (!dev || !dev->data) {
        /* Usar primer dispositivo si no se especificó */
        if (device_count > 0 && devices[0].data) dev = &devices[0];
        else {
            send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND,
                       1019, "no datastore — POST first");
            return;
        }
    }

    /* Parsear lista de SIDs */
    zcbor_state_t st[5];
    zcbor_new_decode_state(st, 5, data, len, len, NULL, 0);
    uint64_t sids[64]; int n = 0;
    while (n < 64) {
        uint64_t sid;
        if (!zcbor_uint64_decode(st, &sid)) break;
        sids[n++] = sid;
    }
    if (n == 0) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "no SIDs in request");
        return;
    }
    printf("[FETCH] device=%s  SIDs=%d\n", dev->device_id, n);

    /* Construir respuesta: mapa CBOR con los SIDs pedidos */
    uint8_t buf[BUFFER_SIZE];
    zcbor_state_t enc[5];
    zcbor_new_encode_state(enc, 5, buf, BUFFER_SIZE, 0);
    zcbor_map_start_encode(enc, n);
    for (int i = 0; i < n; i++) {
        CoreconfValueT *val = getCoreconfHashMap(dev->data->data.map_value, sids[i]);
        if (val) {
            zcbor_uint64_put(enc, sids[i]);
            coreconfToCBOR(val, enc);
        }
    }
    zcbor_map_end_encode(enc, n);
    size_t resp_len = (size_t)(enc[0].payload - buf);

    uint8_t cf_buf[4];
    size_t cfl = coap_encode_var_safe(cf_buf, sizeof(cf_buf), COAP_MEDIA_TYPE_YANG_DATA_CBOR);
    coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT, cfl, cf_buf);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data(resp, resp_len, buf);
    printf("[FETCH] resp %zu bytes\n", resp_len);
}

/* ════════════════════════════════════════════════════════════
 * GET /c  — leer datastore completo (CF=142)
 * ══════════════════════════════════════════════════════════*/
static void handle_get(coap_resource_t *rsrc, coap_session_t *sess,
                        const coap_pdu_t *req, const coap_string_t *query,
                        coap_pdu_t *resp) {
    /* Elegir dispositivo */
    char dev_id[64] = "";
    get_query_device_id(query, dev_id, sizeof(dev_id));
    Device *dev = (dev_id[0] != '\0') ? find_device(dev_id) : NULL;
    if (!dev && device_count > 0) dev = &devices[0];

    if (!dev || !dev->data || dev->data->data.map_value->size == 0) {
        send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND,
                   1013, "datastore empty or not found");
        return;
    }

    uint8_t buf[BUFFER_SIZE];
    size_t  len = create_get_response(buf, BUFFER_SIZE, dev->data);
    if (len == 0) {
        send_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1013, "serialize error");
        return;
    }

    printf("[GET] device=%s  %zu bytes  %zu SIDs\n",
           dev->device_id, len, dev->data->data.map_value->size);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data_large_response(rsrc, sess, req, resp, query,
                                  COAP_MEDIA_TYPE_YANG_DATA_CBOR, -1, 0,
                                  len, buf, NULL, NULL);
}

/* ════════════════════════════════════════════════════════════
 * iPATCH /c  — actualización parcial (CF=141)
 * ══════════════════════════════════════════════════════════*/
static void handle_ipatch(coap_resource_t *rsrc, coap_session_t *sess,
                           const coap_pdu_t *req, const coap_string_t *query,
                           coap_pdu_t *resp) {
    (void)rsrc; (void)sess;
    uint16_t cf = get_content_format(req);
    if (cf != COAP_MEDIA_TYPE_YANG_PATCH_CBOR) {
        send_error(resp, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                   1012, "iPATCH requires CF=141");
        return;
    }

    size_t len; const uint8_t *data;
    if (!coap_get_data(req, &len, &data) || len == 0) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "missing payload");
        return;
    }
    print_cbor_hex("[iPATCH] payload", data, len);

    char dev_id[64] = "";
    get_query_device_id(query, dev_id, sizeof(dev_id));
    Device *dev = (dev_id[0] != '\0') ? find_device(dev_id) : NULL;
    if (!dev && device_count > 0) dev = &devices[0];
    if (!dev || !dev->data) {
        send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND,
                   1019, "no datastore — POST first");
        return;
    }

    CoreconfValueT *patch = parse_ipatch_request(data, len);
    if (!patch || patch->type != CORECONF_HASHMAP) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "bad CBOR patch");
        if (patch) freeCoreconf(patch, true);
        return;
    }

    int n = apply_ipatch(dev->data, patch);
    freeCoreconf(patch, true);
    printf("[iPATCH] device=%s  %d SIDs actualizados\n", dev->device_id, n);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CHANGED);
}

/* ════════════════════════════════════════════════════════════
 * PUT /c  — reemplazo total (CF=142)
 * ══════════════════════════════════════════════════════════*/
static void handle_put(coap_resource_t *rsrc, coap_session_t *sess,
                        const coap_pdu_t *req, const coap_string_t *query,
                        coap_pdu_t *resp) {
    (void)rsrc; (void)sess;
    uint16_t cf = get_content_format(req);
    if (cf != COAP_MEDIA_TYPE_YANG_DATA_CBOR) {
        send_error(resp, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                   1012, "PUT requires CF=142");
        return;
    }

    size_t len; const uint8_t *data;
    if (!coap_get_data(req, &len, &data) || len == 0) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "missing payload");
        return;
    }
    print_cbor_hex("[PUT] payload", data, len);

    char dev_id[64] = "";
    get_query_device_id(query, dev_id, sizeof(dev_id));
    Device *dev = (dev_id[0] != '\0') ? find_device(dev_id) : NULL;
    if (!dev && device_count > 0) dev = &devices[0];
    if (!dev) {
        send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND,
                   1019, "no device — POST first");
        return;
    }

    CoreconfValueT *new_ds = parse_put_request(data, len);
    if (!new_ds || new_ds->type != CORECONF_HASHMAP) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "bad CBOR datastore");
        if (new_ds) freeCoreconf(new_ds, true);
        return;
    }

    int was_existing = dev->exists;
    if (dev->data) freeCoreconf(dev->data, true);
    dev->data   = new_ds;
    dev->exists = 1;

    printf("[PUT] device=%s  %zu SIDs  (%s)\n",
           dev->device_id, dev->data->data.map_value->size,
           was_existing ? "2.04 Changed" : "2.01 Created");
    coap_pdu_set_code(resp, was_existing ? COAP_RESPONSE_CODE_CHANGED
                                         : COAP_RESPONSE_CODE_CREATED);
}

/* ════════════════════════════════════════════════════════════
 * DELETE /c[?k=SID&k=SID]  — eliminar SIDs o datastore
 * ══════════════════════════════════════════════════════════*/
static void handle_delete(coap_resource_t *rsrc, coap_session_t *sess,
                           const coap_pdu_t *req, const coap_string_t *query,
                           coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)req;

    char dev_id[64] = "";
    get_query_device_id(query, dev_id, sizeof(dev_id));
    Device *dev = (dev_id[0] != '\0') ? find_device(dev_id) : NULL;
    if (!dev && device_count > 0) dev = &devices[0];

    if (!dev || !dev->data || dev->data->data.map_value->size == 0) {
        send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND,
                   1013, "datastore not found");
        return;
    }

    /* Extraer k=SID de la query (puede haber id=X además) */
    uint64_t sids[DELETE_MAX_SIDS]; int n = 0;
    if (query && query->length > 0) {
        /* Filtramos solo los parámetros k= */
        const uint8_t *q = query->s;
        size_t ql = query->length;
        n = parse_delete_query(q, ql, sids, DELETE_MAX_SIDS);
    }

    if (n == 0) {
        /* Sin k= → borrar datastore completo */
        size_t old = dev->data->data.map_value->size;
        freeCoreconf(dev->data, true);
        dev->data   = NULL;
        dev->exists = 0;
        printf("[DELETE] device=%s  datastore completo eliminado (%zu SIDs)\n",
               dev->device_id, old);
    } else {
        int del = apply_delete(dev->data->data.map_value, sids, n);
        printf("[DELETE] device=%s  %d/%d SIDs eliminados  %zu restantes\n",
               dev->device_id, del, n, dev->data->data.map_value->size);
    }
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_DELETED);
}

/* ════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════*/
static void sig_handler(int s) { (void)s; quit = 1; }

int main(void) {
    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   CORECONF SERVER — RFC 9254 unificado (libcoap)           ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  POST   /c           → STORE (CF=60)                       ║\n");
    printf("║  FETCH  /c           → leer SIDs (CF=141→142)              ║\n");
    printf("║  GET    /c[?id=X]    → datastore completo (CF=142)         ║\n");
    printf("║  iPATCH /c[?id=X]    → actualizar SIDs (CF=141)            ║\n");
    printf("║  PUT    /c[?id=X]    → reemplazar datastore (CF=142)       ║\n");
    printf("║  DELETE /c[?id=X][&k=SID] → eliminar (2.02 sin payload)   ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    printf("🌐 Escuchando en coap://0.0.0.0:5683\n\n");

    coap_startup();
    coap_set_log_level(COAP_LOG_WARN);

    coap_address_t addr;
    coap_address_init(&addr);
    addr.addr.sin.sin_family      = AF_INET;
    addr.addr.sin.sin_addr.s_addr = INADDR_ANY;
    addr.addr.sin.sin_port        = htons(5683);
    addr.size                     = sizeof(struct sockaddr_in);

    coap_context_t *ctx = coap_new_context(&addr);
    if (!ctx) { fprintf(stderr, "Error creando contexto\n"); return 1; }
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);

    coap_resource_t *res = coap_resource_init(coap_make_str_const("c"),
                                               COAP_RESOURCE_FLAGS_NOTIFY_CON);
    coap_register_handler(res, COAP_REQUEST_POST,    handle_post);
    coap_register_handler(res, COAP_REQUEST_FETCH,   handle_fetch);
    coap_register_handler(res, COAP_REQUEST_GET,     handle_get);
    coap_register_handler(res, COAP_REQUEST_IPATCH,  handle_ipatch);
    coap_register_handler(res, COAP_REQUEST_PUT,     handle_put);
    coap_register_handler(res, COAP_REQUEST_DELETE,  handle_delete);

    coap_attr_t *a;
    a = coap_add_attr(res, coap_make_str_const("rt"),
                      coap_make_str_const("\"core.c.ds\""), 0); (void)a;

    coap_add_resource(ctx, res);
    printf("✅ Servidor listo — esperando dispositivos...\n\n");

    while (!quit) coap_io_process(ctx, 1000);

    printf("\nApagando servidor...\n");
    for (int i = 0; i < device_count; i++)
        if (devices[i].data) freeCoreconf(devices[i].data, true);
    coap_free_context(ctx);
    coap_cleanup();
    return 0;
}

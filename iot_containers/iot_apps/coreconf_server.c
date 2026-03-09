/**
 * coreconf_server.c - Servidor CORECONF (draft-ietf-core-comi-20)
 *
 * UN ÚNICO DATASTORE en /c (draft §2.4: "A CORECONF server supports a
 * single unified datastore").  El datastore puede contener nodos de
 * cualquier tipo: temperatura, humedad, etc., cada uno con su propio SID.
 *
 * Métodos soportados:
 *   PUT    /c  → inicializar / reemplazar datastore  (CF=140)
 *   FETCH  /c  → leer SIDs concretos               (CF=141 req, CF=142 resp)
 *   GET    /c  → leer datastore completo            (CF=140 resp)
 *   iPATCH /c  → actualización parcial              (CF=142 req)
 *   DELETE /c  → borrar datastore completo
 *   POST   /c  → solo RPC/acciones (no para datos)
 *
 * No hay parámetros ?id= — el identificador de nodo va siempre en el
 * payload (draft §3.2.3 para iPATCH, §3.1.3 para FETCH).
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
#include "../../include/sids.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096

/* Content-Format values (draft-ietf-core-comi-20) */
#define CF_YANG_DATA_CBOR   140   /* application/yang-data+cbor;id=sid   */
#define CF_YANG_IDENTIFIERS 141   /* application/yang-identifiers+cbor-seq */
#define CF_YANG_INSTANCES   142   /* application/yang-instances+cbor-seq  */

static int quit = 0;

/* ── Datastore único (draft §2.4) ── */
static CoreconfValueT *g_datastore = NULL;
static int             g_ds_exists = 0;

/* ── Carga el datastore de los ejemplos del draft §3.3.1 ── */
static void init_ietf_example_datastore(void) {
    CoreconfValueT *ds = createCoreconfHashmap();

    /* clock container: SID_SYS_CLOCK → {DELTA_BOOT: boot-datetime, DELTA_CURRENT: current-datetime} */
    CoreconfValueT *clock_c = createCoreconfHashmap();
    insertCoreconfHashMap(clock_c->data.map_value, DELTA_CLOCK_BOOT_DATETIME,
        createCoreconfString("2014-10-05T09:00:00Z"));
    insertCoreconfHashMap(clock_c->data.map_value, DELTA_CLOCK_CURRENT_DATETIME,
        createCoreconfString("2016-10-26T12:16:31Z"));
    insertCoreconfHashMap(ds->data.map_value, SID_SYS_CLOCK, clock_c);

    /* interface list: SID_IF_INTERFACE → [{name:eth0, desc:..., type:ethernetCsmacd, ...}] */
    CoreconfValueT *iface = createCoreconfHashmap();
    insertCoreconfHashMap(iface->data.map_value, DELTA_IF_NAME,        createCoreconfString("eth0"));
    insertCoreconfHashMap(iface->data.map_value, DELTA_IF_DESCRIPTION, createCoreconfString("Ethernet adaptor"));
    insertCoreconfHashMap(iface->data.map_value, DELTA_IF_TYPE,        createCoreconfUint64(SID_IF_IDENTITY_ETHERNET_CSMACD));
    insertCoreconfHashMap(iface->data.map_value, DELTA_IF_ENABLED,     createCoreconfBoolean(true));
    insertCoreconfHashMap(iface->data.map_value, DELTA_IF_OPER_STATUS, createCoreconfUint64(3));
    CoreconfValueT *ifaces = createCoreconfArray();
    addToCoreconfArray(ifaces, iface);
    free(iface);
    insertCoreconfHashMap(ds->data.map_value, SID_IF_INTERFACE, ifaces);

    /* ntp/enabled: SID_SYS_NTP_ENABLED → false */
    insertCoreconfHashMap(ds->data.map_value, SID_SYS_NTP_ENABLED, createCoreconfBoolean(false));

    /* ntp/server list: SID_SYS_NTP_SERVER → [{name:tac.nrc.ca, prefer:false, udp:{address:...}}] */
    CoreconfValueT *udp = createCoreconfHashmap();
    insertCoreconfHashMap(udp->data.map_value, DELTA_NTP_UDP_ADDRESS, createCoreconfString("128.100.49.105"));
    CoreconfValueT *srv = createCoreconfHashmap();
    insertCoreconfHashMap(srv->data.map_value, DELTA_NTP_SERVER_NAME,   createCoreconfString("tac.nrc.ca"));
    insertCoreconfHashMap(srv->data.map_value, DELTA_NTP_SERVER_PREFER, createCoreconfBoolean(false));
    insertCoreconfHashMap(srv->data.map_value, DELTA_NTP_SERVER_UDP,    udp);
    CoreconfValueT *servers = createCoreconfArray();
    addToCoreconfArray(servers, srv);
    free(srv);
    insertCoreconfHashMap(ds->data.map_value, SID_SYS_NTP_SERVER, servers);

    if (g_datastore) freeCoreconf(g_datastore, true);
    g_datastore = ds;
    g_ds_exists = 1;
    printf("Datastore ietf-example cargado: %zu SIDs (clock %d, ifaces %d, ntp %d/%d)\n\n",
           g_datastore->data.map_value->size,
           SID_SYS_CLOCK, SID_IF_INTERFACE, SID_SYS_NTP_ENABLED, SID_SYS_NTP_SERVER);
    /* dummy reference to suppress unused-macro warnings if any */
    (void)SID_IETF_INTERFACES_MODULE; (void)SID_IETF_SYSTEM_MODULE;

}

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
        size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_YANG_DATA_CBOR);
        coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
        coap_add_data(resp, len, buf);
    }
}

/* ════════════════════════════════════════════════════════════
 * POST /c  — solo para RPC/acciones (draft §3.2.2)
 * No se usa para inicializar datos: eso es PUT.
 * ══════════════════════════════════════════════════════════*/
static void handle_post(coap_resource_t *rsrc, coap_session_t *sess,
                         const coap_pdu_t *req, const coap_string_t *query,
                         coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)req; (void)query;
    /* El draft reserva POST para RPC/acciones YANG, no para cargar datos.
     * Para inicializar el datastore usa PUT /c  (CF=140). */
    send_error(resp, COAP_RESPONSE_CODE_NOT_ALLOWED, 1012,
               "POST is for RPC/actions only. Use PUT to initialize datastore.");
    printf("[POST] rechazado — usar PUT para inicializar datastore\n");
}

/* ════════════════════════════════════════════════════════════
 * FETCH /c  — leer SIDs específicos (draft §3.1.3)
 * CF=141 (yang-identifiers+cbor-seq) en la petición: lista de SIDs
 * CF=142 (yang-instances+cbor-seq)  en la respuesta: mapa SID→valor
 * Sin parámetros de query.
 * ══════════════════════════════════════════════════════════*/
static void handle_fetch(coap_resource_t *rsrc, coap_session_t *sess,
                          const coap_pdu_t *req, const coap_string_t *query,
                          coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)query;

    uint16_t cf = get_content_format(req);
    if (cf != CF_YANG_IDENTIFIERS) {
        send_error(resp, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                   1012, "FETCH requires CF=141 (yang-identifiers+cbor-seq)");
        return;
    }

    size_t len; const uint8_t *data;
    if (!coap_get_data(req, &len, &data) || len == 0) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "missing SID list");
        return;
    }
    print_cbor_hex("[FETCH] req", data, len);

    if (!g_datastore || !g_ds_exists) {
        send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND,
                   1019, "datastore empty — use PUT /c first");
        return;
    }

    /* Parsear lista de SIDs del payload (cbor-seq de uint) */
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
    printf("[FETCH] %d SIDs pedidos\n", n);

    /* Respuesta: mapa CBOR {SID: valor, ...} con CF=142 */
    uint8_t buf[BUFFER_SIZE];
    zcbor_state_t enc[8];
    zcbor_new_encode_state(enc, 8, buf, BUFFER_SIZE, 1);
    zcbor_map_start_encode(enc, n);
    for (int i = 0; i < n; i++) {
        CoreconfValueT *val = getCoreconfHashMap(g_datastore->data.map_value, sids[i]);
        if (val) {
            zcbor_uint64_put(enc, sids[i]);
            coreconfToCBOR(val, enc);
        }
    }
    zcbor_map_end_encode(enc, n);
    size_t resp_len = (size_t)(enc[0].payload - buf);

    uint8_t cf_buf[4];
    size_t cfl = coap_encode_var_safe(cf_buf, sizeof(cf_buf), CF_YANG_INSTANCES);
    coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT, cfl, cf_buf);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data(resp, resp_len, buf);
    printf("[FETCH] resp %zu bytes (CF=142)\n", resp_len);
}

/* ════════════════════════════════════════════════════════════
 * GET /c  — leer datastore completo (draft §3.3)
 * Sin parámetros de query. Responde CF=140 con todo el datastore.
 * ══════════════════════════════════════════════════════════*/
static void handle_get(coap_resource_t *rsrc, coap_session_t *sess,
                        const coap_pdu_t *req, const coap_string_t *query,
                        coap_pdu_t *resp) {
    (void)query;

    if (!g_datastore || !g_ds_exists || g_datastore->data.map_value->size == 0) {
        send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND,
                   1013, "datastore empty — use PUT /c first");
        return;
    }

    uint8_t buf[BUFFER_SIZE];
    size_t  len = create_get_response(buf, BUFFER_SIZE, g_datastore);
    if (len == 0) {
        send_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1013, "serialize error");
        return;
    }

    printf("[GET] %zu bytes  %zu SIDs\n", len, g_datastore->data.map_value->size);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data_large_response(rsrc, sess, req, resp, query,
                                  CF_YANG_DATA_CBOR, -1, 0,
                                  len, buf, NULL, NULL);
}

/* ════════════════════════════════════════════════════════════
 * iPATCH /c  — actualización parcial (draft §3.2.3)
 * Sin parámetros de query. El identificador de nodo (SID) va en el PAYLOAD.
 * CF=142 (yang-instances+cbor-seq): mapa {SID: nuevo_valor}
 * Para borrar un nodo: valor = null (CBOR 0xf6)
 * ══════════════════════════════════════════════════════════*/
static void handle_ipatch(coap_resource_t *rsrc, coap_session_t *sess,
                           const coap_pdu_t *req, const coap_string_t *query,
                           coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)query;

    uint16_t cf = get_content_format(req);
    if (cf != CF_YANG_INSTANCES) {
        send_error(resp, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                   1012, "iPATCH requires CF=142 (yang-instances+cbor-seq)");
        return;
    }

    size_t len; const uint8_t *data;
    if (!coap_get_data(req, &len, &data) || len == 0) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "missing payload");
        return;
    }
    print_cbor_hex("[iPATCH] payload", data, len);

    if (!g_datastore || !g_ds_exists) {
        send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND,
                   1019, "no datastore — use PUT /c first");
        return;
    }

    int n = apply_ipatch_raw(g_datastore, data, len);
    printf("[iPATCH] %d SIDs actualizados  (%zu SIDs en datastore)\n",
           n, g_datastore->data.map_value->size);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CHANGED);
}

/* ════════════════════════════════════════════════════════════
 * PUT /c  — inicializar o reemplazar datastore (draft §3.3)
 * Sin parámetros de query. CF=140 (yang-data+cbor;id=sid).
 * 2.01 Created si era nuevo, 2.04 Changed si ya existía.
 * ══════════════════════════════════════════════════════════*/
static void handle_put(coap_resource_t *rsrc, coap_session_t *sess,
                        const coap_pdu_t *req, const coap_string_t *query,
                        coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)query;

    uint16_t cf = get_content_format(req);
    if (cf != CF_YANG_DATA_CBOR) {
        send_error(resp, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                   1012, "PUT requires CF=140 (yang-data+cbor;id=sid)");
        return;
    }

    size_t len; const uint8_t *data;
    if (!coap_get_data(req, &len, &data) || len == 0) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "missing payload");
        return;
    }
    print_cbor_hex("[PUT] payload", data, len);

    CoreconfValueT *new_ds = parse_put_request(data, len);
    if (!new_ds || new_ds->type != CORECONF_HASHMAP) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "bad CBOR datastore");
        if (new_ds) freeCoreconf(new_ds, true);
        return;
    }

    int was_existing = g_ds_exists;
    if (g_datastore) freeCoreconf(g_datastore, true);
    g_datastore = new_ds;
    g_ds_exists = 1;

    printf("[PUT] %zu SIDs  (%s)\n", g_datastore->data.map_value->size,
           was_existing ? "2.04 Changed" : "2.01 Created");
    coap_pdu_set_code(resp, was_existing ? COAP_RESPONSE_CODE_CHANGED
                                         : COAP_RESPONSE_CODE_CREATED);
}

/* ════════════════════════════════════════════════════════════
 * DELETE /c  — borrar datastore completo (draft §3.3)
 * Sin parámetros de query. Para borrar nodos concretos, usar iPATCH con null.
 * ══════════════════════════════════════════════════════════*/
static void handle_delete(coap_resource_t *rsrc, coap_session_t *sess,
                           const coap_pdu_t *req, const coap_string_t *query,
                           coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)req; (void)query;

    if (!g_datastore || !g_ds_exists) {
        send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND, 1013, "datastore not found");
        return;
    }

    size_t old = g_datastore->data.map_value->size;
    freeCoreconf(g_datastore, true);
    g_datastore = NULL;
    g_ds_exists = 0;
    printf("[DELETE] datastore eliminado (%zu SIDs)\n", old);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_DELETED);
}

/* ════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════*/
static void sig_handler(int s) { (void)s; quit = 1; }

int main(void) {
    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   CORECONF SERVER — draft-ietf-core-comi-20 (libcoap)      ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  PUT    /c  CF=140   → inicializar/reemplazar datastore     ║\n");
    printf("║  FETCH  /c  CF=141   → leer SIDs (lista en payload)        ║\n");
    printf("║  GET    /c           → datastore completo                  ║\n");
    printf("║  iPATCH /c  CF=142   → actualizar nodos (SID en payload)   ║\n");
    printf("║  DELETE /c           → borrar datastore completo           ║\n");
    printf("║  POST   /c           → RPC/acciones únicamente             ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Datastore único — temperatura, humedad, etc.              ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    printf("Escuchando en coap://0.0.0.0:5683/c\n\n");

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
    init_ietf_example_datastore();

    while (!quit) coap_io_process(ctx, 1000);

    printf("\nApagando servidor...\n");
    if (g_datastore) freeCoreconf(g_datastore, true);
    coap_free_context(ctx);
    coap_cleanup();
    return 0;
}

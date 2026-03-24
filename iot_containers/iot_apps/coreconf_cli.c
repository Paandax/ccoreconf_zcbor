// INCLUDES ESTÁNDAR Y LIBCOAP
#include <coap3/coap.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Compat: libcoap3-dev de Ubuntu 22.04 puede usar nombres distintos */
#ifndef COAP_LOG_WARN
#define COAP_LOG_WARN 4
#endif
#ifndef COAP_LOG_ERR
#define COAP_LOG_ERR  3
#endif
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <arpa/inet.h>
#include <ctype.h>

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
/**
 * coreconf_cli.c - Cliente CORECONF interactivo (draft-ietf-core-comi)
 *
 * REPL: escribe comandos y los ejecuta contra el servidor.
 *
 * COMANDOS:
 *   store  <tipo> [valor]     → PUT /c  — carga datastore inicial
 *   fetch  <SID> [SID...]     → FETCH /c — leer SIDs concretos
 *   sfetch <SID> [SID...]      → FETCH /s — leer stream filtrado por SID
 *   get                       → GET /c   — leer datastore completo
 *   ipatch <SID> <valor>      → iPATCH /c — actualizar un SID (ID en payload)
 *   put    <SID> <valor> ...  → PUT /c   — reemplazar datastore
 *   delete [SID [SID...]]     → DELETE /c — borrar datastore; SIDs via iPATCH null
 *   help                      → mostrar ayuda
 *   quit / exit               → salir
 *
 * COMPILAR:
 *   cd iot_containers/iot_apps && make coreconf_cli
 *
 * DOCKER (interactivo):
 *   docker attach coreconf_client
 *   > store temperature 22.5
 *   > get
 *   > ipatch 20 99.9
 *   > delete
 *
 * PROTOCOLO (draft-ietf-core-comi-20):
 *   - Un servidor CORECONF tiene UN ÚNICO datastore en /c
 *   - El datastore puede tener nodos para temperatura, humedad, etc.
 *   - PUT /c  → inicializar/reemplazar datastore completo (CF=140)
 *   - FETCH /c → seleccionar SIDs específicos (CF=141 req, CF=142 resp)
 *   - GET /c   → datastore completo SIN parámetros de query
 *   - iPATCH /c → actualizar SIDs; el identificador va en el PAYLOAD (CF=142)
 *   - POST /c  → solo para RPC/acciones (no para datos)
 *   - DELETE /c → borrar datastore completo
 */


// INCLUDES ESTÁNDAR AL PRINCIPIO
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <arpa/inet.h>
#include <ctype.h>

// COAP Y RESTO DE INCLUDES
#include <coap3/coap.h>
/* Compat: libcoap3-dev de Ubuntu 22.04 puede usar nombres distintos */
#ifndef COAP_LOG_WARN
#define COAP_LOG_WARN 4
#endif
#ifndef COAP_LOG_ERR
#define COAP_LOG_ERR  3
#endif

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
#define MAX_ARGS    32

/* Content-Format values (draft-ietf-core-comi-20)
 * 140 = application/yang-data+cbor;id=sid          (datastore completo)
 * 141 = application/yang-identifiers+cbor-seq      (lista de SIDs — FETCH req)
 * 142 = application/yang-instances+cbor-seq        (mapa SID→valor — FETCH resp, iPATCH req)
 */
#define CF_YANG_DATA_CBOR    140
#define CF_YANG_IDENTIFIERS  141
#define CF_YANG_INSTANCES    142

/* ── Estado global CoAP ── */
static coap_context_t  *g_ctx     = NULL;
static coap_session_t  *g_session = NULL;
static char             g_device_type[64] = "temperature";

/* ── Respuesta ── */
static int             g_response_received = 0;
static uint8_t         g_response_buf[BUFFER_SIZE];
static size_t          g_response_len      = 0;
static coap_pdu_code_t g_response_code;
static int             g_observe_mode      = 0;

static void print_datastore(const uint8_t *data, size_t len);

typedef struct RuntimeSystemSids {
    uint64_t module;
    uint64_t clock;
    uint64_t clock_boot;
    uint64_t clock_current;
    uint64_t ntp_enabled;
    uint64_t ntp_server;
    uint64_t ntp_server_name;
    uint64_t ntp_server_prefer;
    uint64_t ntp_server_udp;
    uint64_t ntp_server_udp_address;
    int loaded;
} RuntimeSystemSidsT;

typedef struct RuntimeInterfacesSids {
    uint64_t module;
    uint64_t iface;
    uint64_t iface_description;
    uint64_t iface_enabled;
    uint64_t iface_name;
    uint64_t iface_type;
    uint64_t iface_oper_status;
    uint64_t identity_ethernet_csmacd;
    int loaded;
} RuntimeInterfacesSidsT;

static RuntimeSystemSidsT g_sys_sids = {0};
static RuntimeInterfacesSidsT g_if_sids = {0};

static uint64_t sid_hash_mix_u64(uint64_t hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
    return hash;
}

static uint64_t compute_sid_fingerprint(const RuntimeSystemSidsT *sys,
                                        const RuntimeInterfacesSidsT *ifs) {
    uint64_t h = 1469598103934665603ULL;
    h = sid_hash_mix_u64(h, sys->module);
    h = sid_hash_mix_u64(h, sys->clock);
    h = sid_hash_mix_u64(h, sys->clock_boot);
    h = sid_hash_mix_u64(h, sys->clock_current);
    h = sid_hash_mix_u64(h, sys->ntp_enabled);
    h = sid_hash_mix_u64(h, sys->ntp_server);
    h = sid_hash_mix_u64(h, sys->ntp_server_name);
    h = sid_hash_mix_u64(h, sys->ntp_server_prefer);
    h = sid_hash_mix_u64(h, sys->ntp_server_udp);
    h = sid_hash_mix_u64(h, sys->ntp_server_udp_address);
    h = sid_hash_mix_u64(h, ifs->module);
    h = sid_hash_mix_u64(h, ifs->iface);
    h = sid_hash_mix_u64(h, ifs->iface_description);
    h = sid_hash_mix_u64(h, ifs->iface_enabled);
    h = sid_hash_mix_u64(h, ifs->iface_name);
    h = sid_hash_mix_u64(h, ifs->iface_type);
    h = sid_hash_mix_u64(h, ifs->iface_oper_status);
    h = sid_hash_mix_u64(h, ifs->identity_ethernet_csmacd);
    return h;
}

static int check_server_sid_compatibility(void) {
    uint64_t local_fp = compute_sid_fingerprint(&g_sys_sids, &g_if_sids);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 3, (uint8_t *)"sid");

    g_response_received = 0;
    g_response_len = 0;
    coap_mid_t mid = coap_send(g_session, pdu);
    if (mid == COAP_INVALID_MID) {
        fprintf(stderr, "[SID] CLI: error enviando GET /sid\n");
        return -1;
    }

    int waited = 0;
    while (!g_response_received && waited < 5000) {
        coap_io_process(g_ctx, 100);
        waited += 100;
    }
    if (!g_response_received || g_response_len == 0) {
        fprintf(stderr, "[SID] CLI: timeout/empty response en GET /sid\n");
        return -1;
    }

    char payload[128];
    size_t n = g_response_len < sizeof(payload) - 1 ? g_response_len : sizeof(payload) - 1;
    memcpy(payload, g_response_buf, n);
    payload[n] = '\0';

    uint64_t remote_fp = 0;
    if (sscanf(payload, "sid-fingerprint=%" SCNu64, &remote_fp) != 1) {
        fprintf(stderr, "[SID] CLI: formato inválido en /sid: %s\n", payload);
        return -1;
    }

    printf("[SID] CLI local fingerprint=%" PRIu64 "\n", local_fp);
    printf("[SID] Server fingerprint=%" PRIu64 "\n", remote_fp);

    if (local_fp != remote_fp) {
        fprintf(stderr, "[SID] CLI FATAL: diccionario SID incompatible con el servidor\n");
        return -1;
    }
    printf("[SID] compatibilidad cliente/servidor OK\n");
    return 0;
}

static int parse_sid_after_identifier(const char *text, const char *identifier, uint64_t *sid_out) {
    if (!text || !identifier || !sid_out) return -1;

    const char *id_pos = strstr(text, identifier);
    if (!id_pos) return -1;

    const char *sid_key = strstr(id_pos, "\"sid\"");
    if (!sid_key) return -1;

    const char *colon = strchr(sid_key, ':');
    if (!colon) return -1;

    const char *p = colon + 1;
    while (*p == ' ' || *p == '\t' || *p == '"') p++;

    char *end = NULL;
    unsigned long long value = strtoull(p, &end, 10);
    if (end == p) return -1;

    *sid_out = (uint64_t)value;
    return 0;
}

static int load_text_from_candidates(const char *const *candidates,
                                     size_t candidate_count,
                                     char **text_out,
                                     const char **chosen_out) {
    if (!candidates || candidate_count == 0 || !text_out) return -1;

    FILE *fp = NULL;
    const char *chosen = NULL;
    for (size_t i = 0; i < candidate_count; i++) {
        fp = fopen(candidates[i], "rb");
        if (fp) {
            chosen = candidates[i];
            break;
        }
    }
    if (!fp) return -1;

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    long sz = ftell(fp);
    if (sz <= 0) { fclose(fp); return -1; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return -1; }

    char *text = (char *)malloc((size_t)sz + 1);
    if (!text) { fclose(fp); return -1; }

    size_t nread = fread(text, 1, (size_t)sz, fp);
    fclose(fp);
    text[nread] = '\0';

    *text_out = text;
    if (chosen_out) *chosen_out = chosen;
    return 0;
}

static int load_runtime_system_sids(RuntimeSystemSidsT *out) {
    if (!out) return -1;

    const char *candidates[] = {
        "sid/ietf-system.sid",
        "../sid/ietf-system.sid",
        "../../sid/ietf-system.sid",
        "../../../sid/ietf-system.sid",
        "/app/sid/ietf-system.sid",
    };

    char *text = NULL;
    const char *chosen = NULL;
    if (load_text_from_candidates(candidates,
                                  sizeof(candidates) / sizeof(candidates[0]),
                                  &text, &chosen) != 0) {
        fprintf(stderr, "[SID] CLI: no se encontró ietf-system.sid\n");
        return -1;
    }

    int ok = 0;
    ok |= parse_sid_after_identifier(text, "\"identifier\": \"ietf-system\"", &out->module);
    ok |= parse_sid_after_identifier(text, "/ietf-system:system-state/clock\"", &out->clock);
    ok |= parse_sid_after_identifier(text, "/ietf-system:system-state/clock/boot-datetime\"", &out->clock_boot);
    ok |= parse_sid_after_identifier(text, "/ietf-system:system-state/clock/current-datetime\"", &out->clock_current);
    ok |= parse_sid_after_identifier(text, "/ietf-system:system/ntp/enabled\"", &out->ntp_enabled);
    ok |= parse_sid_after_identifier(text, "/ietf-system:system/ntp/server\"", &out->ntp_server);
    ok |= parse_sid_after_identifier(text, "/ietf-system:system/ntp/server/name\"", &out->ntp_server_name);
    ok |= parse_sid_after_identifier(text, "/ietf-system:system/ntp/server/prefer\"", &out->ntp_server_prefer);
    ok |= parse_sid_after_identifier(text, "/ietf-system:system/ntp/server/udp\"", &out->ntp_server_udp);
    ok |= parse_sid_after_identifier(text, "/ietf-system:system/ntp/server/udp/address\"", &out->ntp_server_udp_address);

    free(text);

    if (ok != 0) {
        fprintf(stderr, "[SID] CLI: error parseando SIDs críticos en %s\n", chosen);
        return -1;
    }

    out->loaded = 1;
    printf("[SID] CLI cargado %s\n", chosen);
    return 0;
}

static int load_runtime_interfaces_sids(RuntimeInterfacesSidsT *out) {
    if (!out) return -1;

    const char *candidates[] = {
        "sid/ietf-interfaces.sid",
        "../sid/ietf-interfaces.sid",
        "../../sid/ietf-interfaces.sid",
        "../../../sid/ietf-interfaces.sid",
        "/app/sid/ietf-interfaces.sid",
    };

    char *text = NULL;
    const char *chosen = NULL;
    if (load_text_from_candidates(candidates,
                                  sizeof(candidates) / sizeof(candidates[0]),
                                  &text, &chosen) != 0) {
        fprintf(stderr, "[SID] CLI: no se encontró ietf-interfaces.sid\n");
        return -1;
    }

    int ok = 0;
    ok |= parse_sid_after_identifier(text, "\"identifier\": \"ietf-interfaces\"", &out->module);
    ok |= parse_sid_after_identifier(text, "/ietf-interfaces:interfaces/interface\"", &out->iface);
    ok |= parse_sid_after_identifier(text, "/ietf-interfaces:interfaces/interface/description\"", &out->iface_description);
    ok |= parse_sid_after_identifier(text, "/ietf-interfaces:interfaces/interface/enabled\"", &out->iface_enabled);
    ok |= parse_sid_after_identifier(text, "/ietf-interfaces:interfaces/interface/name\"", &out->iface_name);
    ok |= parse_sid_after_identifier(text, "/ietf-interfaces:interfaces/interface/type\"", &out->iface_type);
    ok |= parse_sid_after_identifier(text, "/ietf-interfaces:interfaces/interface/oper-status\"", &out->iface_oper_status);
    ok |= parse_sid_after_identifier(text, "\"identifier\": \"ethernetCsmacd\"", &out->identity_ethernet_csmacd);

    free(text);

    if (ok != 0) {
        fprintf(stderr, "[SID] CLI: error parseando SIDs críticos en %s\n", chosen);
        return -1;
    }

    out->loaded = 1;
    printf("[SID] CLI cargado %s\n", chosen);
    return 0;
}

/* ══════════════════════════════════════════════════════════
 * Helpers CoAP
 * ════════════════════════════════════════════════════════*/
static coap_response_t response_handler(coap_session_t *sess,
                                          const coap_pdu_t *sent,
                                          const coap_pdu_t *recv,
                                          const coap_mid_t mid) {
    (void)sess; (void)sent; (void)mid; //No salten los warnings por parámetros no usados

    coap_opt_iterator_t oi;
    coap_opt_t *obs_opt = coap_check_option(recv, COAP_OPTION_OBSERVE, &oi);

    g_response_code = coap_pdu_get_code(recv);
    size_t len; 
    const uint8_t *data;

    if (coap_get_data(recv, &len, &data)) {
        memcpy(g_response_buf, data, len);
        g_response_len = len;
    } else {
        g_response_len = 0;
    }

    if (g_observe_mode && obs_opt) {
        uint32_t obs_seq = (uint32_t)coap_decode_var_bytes(coap_opt_value(obs_opt), coap_opt_length(obs_opt));
        printf("\n[OBSERVE] notif seq=%" PRIu32 " code=%d.%02d\n",
               obs_seq, COAP_RESPONSE_CLASS(g_response_code), g_response_code & 0x1F);
        if (g_response_len > 0) {
            print_datastore(g_response_buf, g_response_len);
        } else {
            printf("    (stream sin payload)\n");
        }
        printf("coreconf> ");
        fflush(stdout);
    }

    g_response_received = 1; //Avisar a send_and_wait que llegó la respuesta
    return COAP_RESPONSE_OK;
}

static int send_and_wait(coap_pdu_t *pdu) {
    g_response_received = 0; //Reiniciar flag y buffer
    g_response_len      = 0;
    coap_mid_t mid = coap_send(g_session, pdu);

    if (mid == COAP_INVALID_MID) { 
        printf(" Error enviando\n"); 
        return 0; 
    }
    int w = 0;
    while (!g_response_received && w < 5000) {
        coap_io_process(g_ctx, 100); 
        w += 100;
    }
    if (!g_response_received) { 
        printf("Timeout\n"); return 0; 
    }
    printf(" %d.%02d  ", COAP_RESPONSE_CLASS(g_response_code), g_response_code & 0x1F);
    return 1;
}

/* ══════════════════════════════════════════════════════════
 * Mostrar datastore decodificado (con soporte para mapas/arrays anidados)
 * ════════════════════════════════════════════════════════*/

/* Imprime cualquier CoreconfValueT de forma recursiva */
static void print_value_r(CoreconfValueT *val, int depth) {
    if (!val) { printf("null"); return; }
    char indent[64] = "";
    for (int i = 0; i < depth && i < 10; i++) strcat(indent, "  ");

    switch (val->type) {
        case CORECONF_REAL:    printf("%.6g",   val->data.real_value);    break;
        case CORECONF_STRING:  printf("\"%s\"",  val->data.string_value);   break;
        case CORECONF_UINT_64: printf("%" PRIu64, val->data.u64);           break;
        case CORECONF_INT_64:  printf("%" PRId64, val->data.i64);           break;
        case CORECONF_UINT_32: printf("%" PRIu32, val->data.u32);           break;
        case CORECONF_UINT_16: printf("%" PRIu16, val->data.u16);           break;
        case CORECONF_UINT_8:  printf("%" PRIu8,  val->data.u8);            break;
        case CORECONF_INT_32:  printf("%" PRId32, val->data.i32);           break;
        case CORECONF_INT_16:  printf("%" PRId16, val->data.i16);           break;
        case CORECONF_INT_8:   printf("%" PRId8,  val->data.i8);            break;
        case CORECONF_TRUE:    printf("true");                               break;
        case CORECONF_FALSE:   printf("false");                              break;
        case CORECONF_NULL:    printf("null");                               break;
        case CORECONF_HASHMAP: {
            printf("{\n");
            CoreconfHashMapT *m = val->data.map_value;
            for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
                for (CoreconfObjectT *o = m->table[t]; o; o = o->next) {
                    printf("%s  %" PRIu64 " : ", indent, o->key);
                    print_value_r(o->value, depth + 1);
                    printf("\n");
                }
            }
            printf("%s}", indent);
            break;
        }
        case CORECONF_ARRAY: {
            CoreconfArrayT *arr = val->data.array_value;
            printf("[\n");
            for (size_t i = 0; i < arr->size; i++) {
                printf("%s  ", indent);
                print_value_r(&arr->elements[i], depth + 1);
                printf("\n");
            }
            printf("%s]", indent);
            break;
        }
        default: printf("(tipo %d)", val->type); break;
    }
}

static void print_datastore(const uint8_t *data, size_t len) {
    CoreconfValueT *ds = parse_get_response(data, len);
    if (!ds || ds->type != CORECONF_HASHMAP) {
        printf("(error decodificando)\n");
        if (ds) freeCoreconf(ds, true);
        return;
    }
    printf("\n");
    CoreconfHashMapT *map = ds->data.map_value;
    size_t shown = 0;
    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
        CoreconfObjectT *obj = map->table[t];
        while (obj) {
            printf("    %-8" PRIu64 " : ", obj->key);
            print_value_r(obj->value, 1);
            printf("\n");
            obj = obj->next;
            shown++;
        }
    }
    printf("    ── %zu nodo(s) raíz ──\n", shown);
    freeCoreconf(ds, true);
}

/* ══════════════════════════════════════════════════════════
 * Parsear valor desde string al tipo más adecuado
 * ════════════════════════════════════════════════════════*/
/* parse_value_ptr — parser recursivo que avanza el puntero *p.
 * Soporta: null, true, false, números, strings y mapas {k:v,k:v,...} anidados.
 * Los mapas usan claves uint (deltas YANG).
 */
static CoreconfValueT *parse_value_ptr(const char **p);

static CoreconfValueT *parse_map_ptr(const char **p) {
    /* Esperamos estar justo después de '{' */
    CoreconfValueT *map = createCoreconfHashmap();
    while (**p && **p != '}') {
        /* saltar espacios y comas */
        while (**p == ' ' || **p == ',') (*p)++;
        if (**p == '}') break;
        /* leer clave uint */
        char *ep;
        uint64_t key = (uint64_t)strtoull(*p, &ep, 10);
        if (ep == *p) break;  /* no es número → error */
        *p = ep;
        /* saltar ':' */
        while (**p == ' ') (*p)++;
        if (**p == ':') (*p)++;
        while (**p == ' ') (*p)++;
        /* leer valor (recursivo) */
        CoreconfValueT *val = parse_value_ptr(p);
        if (val) insertCoreconfHashMap(map->data.map_value, key, val);
        while (**p == ' ') (*p)++;
    }
    if (**p == '}') (*p)++;  /* consumir '}' */
    return map;
}

static CoreconfValueT *parse_value_ptr(const char **p) {
    while (**p == ' ') (*p)++;
    if (**p == '{') {
        (*p)++;  /* consumir '{' */
        return parse_map_ptr(p);
    }
    /* leer hasta el próximo ',' '}' o fin */
    const char *start = *p;
    while (**p && **p != ',' && **p != '}') (*p)++;
    size_t len = (size_t)(*p - start);
    char buf[256];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    /* quitar espacios al final */
    while (len > 0 && buf[len-1] == ' ') buf[--len] = '\0';

    if (strcmp(buf, "null")  == 0) {
        CoreconfValueT *v = malloc(sizeof(CoreconfValueT));
        if (v) { v->type = CORECONF_NULL; v->data.u64 = 0; }
        return v;
    }
    if (strcmp(buf, "true")  == 0) return createCoreconfBoolean(true);
    if (strcmp(buf, "false") == 0) return createCoreconfBoolean(false);
    char *ep;
    double d = strtod(buf, &ep);
    if (ep != buf && *ep == '\0') {
        if (strchr(buf, '.') || strchr(buf, 'e') || strchr(buf, 'E'))
            return createCoreconfReal(d);
        if (buf[0] == '-') return createCoreconfInt64((int64_t)strtoll(buf, NULL, 10));
        return createCoreconfUint64((uint64_t)strtoull(buf, NULL, 10));
    }
    return createCoreconfString(buf);
}

static CoreconfValueT *parse_value(const char *s) {
    const char *p = s;
    return parse_value_ptr(&p);
}

/* ══════════════════════════════════════════════════════════
 * Comandos
 * ════════════════════════════════════════════════════════*/

/* store <tipo> [valor]  |  store ietf-example
 * Inicializa el datastore via PUT /c (draft §3.3 — reemplaza el datastore completo).
 *
 * store ietf-example → carga el datastore exacto de los ejemplos del draft:
 *   §3.3.1 GET:   clock (SID 1721) + interface list (SID 1533)
 *   §3.1.3.1 FETCH: current-datetime (SID 1723), interface eth0 (SID 1533)
 *   §3.2.3.1 iPATCH NTP: enabled (SID 1755), server list (SID 1756)
 *
 * Módulo ietf-system  (RFC 7317, SIDs ~1700-1799):
 *   SID 1721 = /ietf-system:system-state/clock
 *   SID 1722 = .../clock/boot-datetime      delta=1
 *   SID 1723 = .../clock/current-datetime   delta=2
 *   SID 1755 = .../ntp/enabled
 *   SID 1756 = .../ntp/server
 *   SID 1759 = .../ntp/server/name          delta=3
 *   SID 1760 = .../ntp/server/prefer        delta=4
 *   SID 1761 = .../ntp/server/udp           delta=5
 *   SID 1762 = .../ntp/server/udp/address   delta=1
 *
 * Módulo ietf-interfaces (RFC 8343, SIDs ~1500-1599):
 *   SID 1533 = /ietf-interfaces:interfaces/interface
 *   SID 1534 = .../interface/description    delta=1
 *   SID 1535 = .../interface/enabled        delta=2
 *   SID 1537 = .../interface/name           delta=4  (key)
 *   SID 1538 = .../interface/type           delta=5  (identity)
 *   SID 1544 = .../interface/oper-status    delta=11
 *   SID 1880 = identity ethernetCsmacd
 *
 * Encoding RFC 9254: claves en mapas anidados = delta relativo al SID padre.
 */
static void cmd_store(int argc, char **argv) {
    const char *tipo = (argc >= 1) ? argv[0] : g_device_type;
    double valor     = (argc >= 2) ? atof(argv[1]) : 22.5;

    strncpy(g_device_type, tipo, sizeof(g_device_type) - 1);

    CoreconfValueT  *data = createCoreconfHashmap();
    CoreconfHashMapT *map = data->data.map_value;

    if (strcmp(tipo, "ietf-example") == 0) {
        if (!g_sys_sids.loaded || !g_if_sids.loaded) {
            printf("  Error: diccionario SID runtime no cargado\n");
            freeCoreconf(data, true);
            return;
        }

        uint64_t delta_clock_boot    = g_sys_sids.clock_boot - g_sys_sids.clock;
        uint64_t delta_clock_current = g_sys_sids.clock_current - g_sys_sids.clock;
        uint64_t delta_if_desc       = g_if_sids.iface_description - g_if_sids.iface;
        uint64_t delta_if_enabled    = g_if_sids.iface_enabled - g_if_sids.iface;
        uint64_t delta_if_name       = g_if_sids.iface_name - g_if_sids.iface;
        uint64_t delta_if_type       = g_if_sids.iface_type - g_if_sids.iface;
        uint64_t delta_if_oper       = g_if_sids.iface_oper_status - g_if_sids.iface;
        uint64_t delta_ntp_name      = g_sys_sids.ntp_server_name - g_sys_sids.ntp_server;
        uint64_t delta_ntp_prefer    = g_sys_sids.ntp_server_prefer - g_sys_sids.ntp_server;
        uint64_t delta_ntp_udp       = g_sys_sids.ntp_server_udp - g_sys_sids.ntp_server;
        uint64_t delta_ntp_udp_addr  = g_sys_sids.ntp_server_udp_address - g_sys_sids.ntp_server_udp;

        /* ─────────────────────────────────────────────────────────
         * Datastore exacto del draft-ietf-core-comi-20 §3.3.1
         * ─────────────────────────────────────────────────────────
         * REF GET response:
         *  { 1721: { 2:"2016-10-26T12:16:31Z",  <- current-datetime (1723-1721=2)
         *            1:"2014-10-05T09:00:00Z" }, <- boot-datetime    (1722-1721=1)
         *    1533: [{ 4:"eth0",                  <- name    (1537-1533=4)
         *             1:"Ethernet adaptor",      <- description (1534-1533=1)
         *             5: 1880,                   <- type = ethernetCsmacd (1538-1533=5)
         *             2: true,                   <- enabled (1535-1533=2)
         *            11: 3 }] }                  <- oper-status=testing (1544-1533=11)
         * ─────────────────────────────────────────────────────────*/

        /* clock container: SID(runtime) → {DELTA_CURRENT, DELTA_BOOT} */
        CoreconfValueT *clock_c = createCoreconfHashmap();
        insertCoreconfHashMap(clock_c->data.map_value, delta_clock_current,
            createCoreconfString("2016-10-26T12:16:31Z")); /* current-datetime */
        insertCoreconfHashMap(clock_c->data.map_value, delta_clock_boot,
            createCoreconfString("2014-10-05T09:00:00Z")); /* boot-datetime */
        insertCoreconfHashMap(map, g_sys_sids.clock, clock_c);

        /* interface list: SID(runtime) → [{name,description,type,enabled,oper-status}] */
        CoreconfValueT *iface = createCoreconfHashmap();
        insertCoreconfHashMap(iface->data.map_value, delta_if_name,
            createCoreconfString("eth0"));              /* name */
        insertCoreconfHashMap(iface->data.map_value, delta_if_desc,
            createCoreconfString("Ethernet adaptor")); /* description */
        insertCoreconfHashMap(iface->data.map_value, delta_if_type,
            createCoreconfUint64(g_if_sids.identity_ethernet_csmacd)); /* type = ethernetCsmacd */
        insertCoreconfHashMap(iface->data.map_value, delta_if_enabled,
            createCoreconfBoolean(true));              /* enabled */
        insertCoreconfHashMap(iface->data.map_value, delta_if_oper,
            createCoreconfUint64(3));                  /* oper-status = 3 (testing) */
        CoreconfValueT *ifaces = createCoreconfArray();
        addToCoreconfArray(ifaces, iface);
        free(iface); /* addToCoreconfArray copia el struct; liberar la envoltura */
        insertCoreconfHashMap(map, g_if_sids.iface, ifaces);

        /* ntp/enabled: SID(runtime) → false  (para demo iPATCH §3.2.3.1) */
        insertCoreconfHashMap(map, g_sys_sids.ntp_enabled, createCoreconfBoolean(false));

        /* ntp/server list: SID(runtime) → [{name,prefer,udp:{address}}] */
        CoreconfValueT *udp = createCoreconfHashmap();
        insertCoreconfHashMap(udp->data.map_value, delta_ntp_udp_addr,
            createCoreconfString("128.100.49.105"));
        CoreconfValueT *srv = createCoreconfHashmap();
        insertCoreconfHashMap(srv->data.map_value, delta_ntp_name,
            createCoreconfString("tac.nrc.ca"));
        insertCoreconfHashMap(srv->data.map_value, delta_ntp_prefer,
            createCoreconfBoolean(false));
        insertCoreconfHashMap(srv->data.map_value, delta_ntp_udp, udp);
        CoreconfValueT *servers = createCoreconfArray();
        addToCoreconfArray(servers, srv);
        free(srv);
        insertCoreconfHashMap(map, g_sys_sids.ntp_server, servers);
    } else {
        /* Datastore simple con SIDs arbitrarios (sensor) */
        insertCoreconfHashMap(map, 10, createCoreconfString(tipo));
        insertCoreconfHashMap(map, 11, createCoreconfUint64((uint64_t)time(NULL)));
        insertCoreconfHashMap(map, 20, createCoreconfReal(valor));
        insertCoreconfHashMap(map, 21, createCoreconfString("unit"));
    }

    uint8_t buf[BUFFER_SIZE];
    zcbor_state_t enc[8];
    zcbor_new_encode_state(enc, 8, buf, BUFFER_SIZE, 1);
    coreconfToCBOR(data, enc);
    size_t len = (size_t)(enc[0].payload - buf);
    freeCoreconf(data, true);

    /* PUT /c — sin parámetros de query (draft §3.3)
     * CF=140 (application/yang-data+cbor;id=sid) */
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_PUT, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_YANG_DATA_CBOR);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_add_data(pdu, len, buf);

    if (strcmp(tipo, "ietf-example") == 0)
        printf("  PUT /c (ietf-example §3.3.1, %zu bytes)... ", len);
    else
        printf("  PUT /c (STORE, %zu bytes)... ", len);

    if (send_and_wait(pdu)) {
        if (strcmp(tipo, "ietf-example") == 0)
                 printf("datastore ietf-example cargado (clock %" PRIu64 ", ifaces %" PRIu64 ", ntp %" PRIu64 "/%" PRIu64 ")\n",
                     g_sys_sids.clock, g_if_sids.iface, g_sys_sids.ntp_enabled, g_sys_sids.ntp_server);
        else
            printf("datastore inicializado con SIDs 10,11,20,21\n");
    }
}

/* fetch <SID> [SID ...] [[SID,key] ...]
 * FETCH /c — sin parámetros de query (draft §3.1.3)
 * CF=141 (yang-identifiers+cbor-seq): lista de instance-identifiers en el payload
 * Accept=142 (yang-instances+cbor-seq): el servidor responde con mapa SID→valor
 *
 * Soporta instance-identifiers con clave de lista (draft §3.1.3.1):
 *   fetch 1723              → SID simple (leaf)
 *   fetch [1533,eth0]       → lista instance [SID,"key"] para list instance
 *   fetch 1723 [1533,eth0]  → combinación (ejemplo exacto del draft §3.1.3.1)
 */
static void cmd_fetch(int argc, char **argv) {
    if (argc == 0) {
        printf("  Uso: fetch <SID> [SID...] [[SID,clave]...]\n");
        printf("  Ej:  fetch 1723 [1533,eth0]  (ejemplo §3.1.3.1 del draft)\n");
        return;
    }

    /* Construir array de InstanceIdentifier (soporta SID simple y [SID,"key"]) */
    InstanceIdentifier iids[64]; int n = 0;
    for (int i = 0; i < argc && i < 64; i++) {
        if (argv[i][0] == '[') {
            /* Formato [SID,key] — modificar argv[i] in-place (es mutable) */
            char *p = argv[i] + 1;   /* saltar '[' */
            char *comma = strchr(p, ',');
            if (!comma) {
                printf("  Error: se esperaba [SID,clave], recibido: %s\n", argv[i]);
                return;
            }
            *comma = '\0';
            char *key = comma + 1;
            size_t kl = strlen(key);
            if (kl > 0 && key[kl - 1] == ']') key[kl - 1] = '\0';
            iids[n].type        = IID_WITH_STR_KEY;
            iids[n].sid         = strtoull(p, NULL, 10);
            iids[n].key.str_key = key;  /* apunta a argv[i], válido durante la llamada */
            n++;
        } else {
            iids[n].type        = IID_SIMPLE;
            iids[n].sid         = strtoull(argv[i], NULL, 10);
            iids[n].key.str_key = NULL;
            n++;
        }
    }

    uint8_t buf[BUFFER_SIZE];
    size_t  len = create_fetch_request_with_iids(buf, BUFFER_SIZE, iids, (size_t)n);

    /* FETCH /c — sin query params (draft §3.1.3) */
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_FETCH, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    /* CF=141: lista de identificadores de nodo */
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_YANG_IDENTIFIERS);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    /* Accept=142: instancias (mapa SID→valor) */
    uint8_t acc[4]; size_t accl = coap_encode_var_safe(acc, sizeof(acc), CF_YANG_INSTANCES);
    coap_add_option(pdu, COAP_OPTION_ACCEPT, accl, acc);
    coap_add_data(pdu, len, buf);

    printf("  FETCH /c (%d IID(s), %zu bytes req)... ", n, len);
    if (send_and_wait(pdu) && g_response_len > 0)
        print_datastore(g_response_buf, g_response_len);
    else if (g_response_len == 0)
        printf("sin payload\n");
    (void)accl;
}

/* sfetch <SID> [SID ...] [[SID,key] ...]
 * FETCH /s con filtro opcional de instance-identifiers (CF=141).
 * Respuesta CF=142 con cbor-seq de notificaciones (más nueva primero).
 */
static void cmd_sfetch(int argc, char **argv) {
    if (argc == 0) {
        printf("  Uso: sfetch <SID> [SID...] [[SID,clave]...]\n");
        return;
    }

    InstanceIdentifier iids[64]; int n = 0;
    for (int i = 0; i < argc && i < 64; i++) {
        if (argv[i][0] == '[') {
            char *p = argv[i] + 1;
            char *comma = strchr(p, ',');
            if (!comma) {
                printf("  Error: se esperaba [SID,clave], recibido: %s\n", argv[i]);
                return;
            }
            *comma = '\0';
            char *key = comma + 1;
            size_t kl = strlen(key);
            if (kl > 0 && key[kl - 1] == ']') key[kl - 1] = '\0';
            iids[n].type        = IID_WITH_STR_KEY;
            iids[n].sid         = strtoull(p, NULL, 10);
            iids[n].key.str_key = key;
            n++;
        } else {
            iids[n].type        = IID_SIMPLE;
            iids[n].sid         = strtoull(argv[i], NULL, 10);
            iids[n].key.str_key = NULL;
            n++;
        }
    }

    uint8_t buf[BUFFER_SIZE];
    size_t  len = create_fetch_request_with_iids(buf, BUFFER_SIZE, iids, (size_t)n);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_FETCH, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"s");
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_YANG_IDENTIFIERS);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    uint8_t acc[4]; size_t accl = coap_encode_var_safe(acc, sizeof(acc), CF_YANG_INSTANCES);
    coap_add_option(pdu, COAP_OPTION_ACCEPT, accl, acc);
    coap_add_data(pdu, len, buf);

    printf("  FETCH /s (%d IID(s), %zu bytes req)... ", n, len);
    if (send_and_wait(pdu) && g_response_len > 0)
        print_datastore(g_response_buf, g_response_len);
    else if (g_response_len == 0)
        printf("stream sin payload\n");
    (void)accl;
}

/* get
 * GET /c — sin parámetros de query (draft §3.3)
 * Recupera el datastore COMPLETO. No lleva identificadores.
 */
static void cmd_get(void) {
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    /* Sin query params — draft §3.3: GET recupera el datastore completo */

    printf("  GET /c... ");
    if (send_and_wait(pdu)) {
        if (g_response_len > 0)
            print_datastore(g_response_buf, g_response_len);
        else
            printf("sin payload\n");
    }
}

/* ipatch <SID> <valor>
 * ipatch [SID,clave] <valor>   ← instance-identifier (lista YANG)
 * iPATCH /c — sin parámetros de query (draft §3.2.3)
 * El identificador de nodo (SID) va en el PAYLOAD, no en la URL.
 * CF=142 (yang-instances+cbor-seq): mapa {SID: valor} o {[SID,"key"]: valor}
 * Para borrar un nodo: ipatch <SID> null
 * Para borrar una instancia de lista: ipatch [SID,clave] null
 */
static void cmd_ipatch(int argc, char **argv) {
    if (argc < 2) {
        printf("  Uso: ipatch <SID> <valor>\n");
        printf("  Uso: ipatch [SID,clave] null|<valor>\n");
        return;
    }

    uint8_t buf[BUFFER_SIZE];
    size_t  len = 0;
    coap_pdu_t *pdu = NULL;

    if (argv[0][0] == '[') {
        /* ── Instance-identifier: [SID,clave] ── */
        char tmp[256];
        strncpy(tmp, argv[0] + 1, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *closing = strchr(tmp, ']');
        if (closing) *closing = '\0';
        char *comma = strchr(tmp, ',');
        if (!comma) { printf("  Error: formato esperado [SID,clave]\n"); return; }
        *comma = '\0';
        uint64_t sid = strtoull(tmp, NULL, 10);
        const char *key_str = comma + 1;

        CoreconfValueT *val = (strcmp(argv[1], "null") == 0)
                              ? NULL : parse_value(argv[1]);

        len = create_ipatch_iid_request(buf, BUFFER_SIZE, sid, key_str, val);
        if (val) freeCoreconf(val, true);

        pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_IPATCH, g_session);
        coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
        uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_YANG_INSTANCES);
        coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
        coap_add_data(pdu, len, buf);
        printf("  iPATCH /c ([%"PRIu64",%s] = %s)... ", sid, key_str, argv[1]);

    } else {
        /* ── SID simple ── */
        uint64_t sid = strtoull(argv[0], NULL, 10);
        CoreconfValueT *val = parse_value(argv[1]);

        CoreconfValueT  *patch = createCoreconfHashmap();
        insertCoreconfHashMap(patch->data.map_value, sid, val);

        len = create_ipatch_request(buf, BUFFER_SIZE, patch);
        freeCoreconf(patch, true);

        pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_IPATCH, g_session);
        coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
        uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_YANG_INSTANCES);
        coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
        coap_add_data(pdu, len, buf);
        printf("  iPATCH /c (SID %"PRIu64" = %s)... ", sid, argv[1]);
    }

    if (send_and_wait(pdu)) printf("\n");
}

/* demo
 * Reproduce los ejemplos exactos del draft-ietf-core-comi-20:
 *   §3.3.1  GET/PUT  datastore  (clock + interface)
 *   §3.1.3.1 FETCH   con current-datetime y eth0
 *   §3.2.3.1 iPATCH  NTP  (3 entradas: set enabled, delete server, add server)
 */
static void cmd_demo(void) {
    if (!g_sys_sids.loaded || !g_if_sids.loaded) {
        printf("\n[SID] Error: diccionario SID runtime no cargado en CLI\n\n");
        return;
    }

    uint64_t delta_ntp_name     = g_sys_sids.ntp_server_name - g_sys_sids.ntp_server;
    uint64_t delta_ntp_prefer   = g_sys_sids.ntp_server_prefer - g_sys_sids.ntp_server;
    uint64_t delta_ntp_udp      = g_sys_sids.ntp_server_udp - g_sys_sids.ntp_server;
    uint64_t delta_ntp_udp_addr = g_sys_sids.ntp_server_udp_address - g_sys_sids.ntp_server_udp;

    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  DEMO: Ejemplos exactos draft-ietf-core-comi-20              ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    printf("── PASO 1: store ietf-example  (§3.3.1 PUT /c) ─────────────────\n");
    char *store_args[] = {"ietf-example"};
    cmd_store(1, store_args);
    printf("\n");

    printf("── PASO 2: GET /c — datastore completo  (§3.3.1) ───────────────\n");
    printf("  Respuesta servidor:\n");
    cmd_get();
    printf("\n");

    printf("── PASO 3: FETCH /c  (§3.1.3.1) ────────────────────────────────\n");
    printf("  REQ payload (yang-identifiers+cbor-seq):\n");
    printf("    %" PRIu64 ",             / current-datetime /\n", g_sys_sids.clock_current);
    printf("    [%" PRIu64 ", \"eth0\"]   / interface con name=\"eth0\" /\n", g_if_sids.iface);
    printf("  Respuesta servidor:\n");
    {
        char fa0[32];
        char fa1[64];
        snprintf(fa0, sizeof(fa0), "%" PRIu64, g_sys_sids.clock_current);
        snprintf(fa1, sizeof(fa1), "[%" PRIu64 ",eth0]", g_if_sids.iface);
        char *fetch_args[] = {fa0, fa1};
        cmd_fetch(2, fetch_args);
    }
    printf("\n");

    printf("── PASO 4: iPATCH /c  (§3.2.3.1 — NTP) ────────────────────────\n");
    printf("  REQ payload (yang-instances+cbor-seq, 3 mapas):\n");
    printf("    { %" PRIu64 ": true }                        set ntp/enabled\n", g_sys_sids.ntp_enabled);
    printf("    { [%" PRIu64 ", \"tac.nrc.ca\"]: null }       borrar servidor NTP\n", g_sys_sids.ntp_server);
    printf("    { %" PRIu64 ": { %" PRIu64 ":\"tic.nrc.ca\", %" PRIu64 ":true,\n",
           g_sys_sids.ntp_server, delta_ntp_name, delta_ntp_prefer);
    printf("               %" PRIu64 ":{ %" PRIu64 ":\"132.246.11.231\" } } añadir servidor NTP\n",
           delta_ntp_udp, delta_ntp_udp_addr);
    {
        uint8_t buf[BUFFER_SIZE];
        size_t offset = 0;
        zcbor_state_t enc[12];

        zcbor_new_encode_state(enc, 12, buf + offset, BUFFER_SIZE - offset, 0);
        zcbor_map_start_encode(enc, 1);
        zcbor_uint64_put(enc, g_sys_sids.ntp_enabled);
        zcbor_bool_put(enc, true);
        zcbor_map_end_encode(enc, 1);
        offset += (size_t)(enc[0].payload - (buf + offset));

        zcbor_new_encode_state(enc, 12, buf + offset, BUFFER_SIZE - offset, 0);
        zcbor_map_start_encode(enc, 1);
        zcbor_list_start_encode(enc, 2);
        zcbor_uint64_put(enc, g_sys_sids.ntp_server);
        {
            struct zcbor_string zs = { .value = (const uint8_t *)"tac.nrc.ca", .len = 10 };
            zcbor_tstr_encode(enc, &zs);
        }
        zcbor_list_end_encode(enc, 2);
        zcbor_nil_put(enc, NULL);
        zcbor_map_end_encode(enc, 1);
        offset += (size_t)(enc[0].payload - (buf + offset));

        zcbor_new_encode_state(enc, 12, buf + offset, BUFFER_SIZE - offset, 0);
        zcbor_map_start_encode(enc, 1);
        zcbor_uint64_put(enc, g_sys_sids.ntp_server);
        zcbor_map_start_encode(enc, 3);
        zcbor_uint64_put(enc, delta_ntp_name);
        { struct zcbor_string zs = { .value=(const uint8_t*)"tic.nrc.ca", .len=10 }; zcbor_tstr_encode(enc, &zs); }
        zcbor_uint64_put(enc, delta_ntp_prefer); zcbor_bool_put(enc, true);
        zcbor_uint64_put(enc, delta_ntp_udp);
        zcbor_map_start_encode(enc, 1);
        zcbor_uint64_put(enc, delta_ntp_udp_addr);
        { struct zcbor_string zs = { .value=(const uint8_t*)"132.246.11.231", .len=14 }; zcbor_tstr_encode(enc, &zs); }
        zcbor_map_end_encode(enc, 1);
        zcbor_map_end_encode(enc, 3);
        zcbor_map_end_encode(enc, 1);
        offset += (size_t)(enc[0].payload - (buf + offset));

        printf("  Payload total: %zu bytes (3 mapas concatenados en cbor-seq)\n", offset);
        printf("  iPATCH /c... ");
        coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_IPATCH, g_session);
        coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
        uint8_t cfb[4]; size_t cfbl = coap_encode_var_safe(cfb, sizeof(cfb), CF_YANG_INSTANCES);
        (void)cfbl;
        coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfbl, cfb);
        coap_add_data(pdu, offset, buf);
        if (send_and_wait(pdu)) printf("\n");
    }
    printf("\n");

    printf("── PASO 5: GET /c — verificación post-iPATCH ───────────────────\n");
    printf("  ntp/enabled (SID %" PRIu64 ") debe ser true\n", g_sys_sids.ntp_enabled);
    printf("  ntp/server  (SID %" PRIu64 ") debe contener tic.nrc.ca con address 132.246.11.231\n", g_sys_sids.ntp_server);
    cmd_get();
    printf("\nDemo completado.\n\n");
}

/* put <SID> <valor> [SID valor ...]
 * PUT /c — sin parámetros de query (draft §3.3)
 * Reemplaza el datastore completo.
 * CF=140 (yang-data+cbor;id=sid): mapa completo del datastore
 */
static void cmd_put(int argc, char **argv) {
    if (argc < 2 || argc % 2 != 0) {
        printf("  Uso: put <SID> <valor> [SID valor ...]\n"); return;
    }

    CoreconfValueT  *ds  = createCoreconfHashmap();
    CoreconfHashMapT *map = ds->data.map_value;
    for (int i = 0; i < argc; i += 2) {
        uint64_t sid = strtoull(argv[i], NULL, 10);
        insertCoreconfHashMap(map, sid, parse_value(argv[i + 1]));
    }

    uint8_t buf[BUFFER_SIZE];
    size_t  len = create_put_request(buf, BUFFER_SIZE, ds);
    freeCoreconf(ds, true);

    /* PUT /c — sin query params (draft §3.3)
     * CF=140: datastore completo */
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_PUT, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_YANG_DATA_CBOR);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_add_data(pdu, len, buf);

    printf("  PUT /c (%d SIDs)... ", argc / 2);
    if (send_and_wait(pdu)) printf("\n");
}

/* delete
 * DELETE /c — sin parámetros de query (draft §3.3)
 * Borra el datastore COMPLETO.
 * Para borrar un SID individual, usa:  ipatch <SID> null
 */
static void cmd_delete(int argc, char **argv) {
    if (argc > 0) {
        /* El draft no define DELETE por SID individual en la URI.
         * Usa iPATCH con valor null para borrar nodos específicos. */
        printf("  AVISO: DELETE borra el datastore completo.\n");
        printf("  Para borrar un SID concreto usa: ipatch <SID> null\n");
        printf("  Continuar con DELETE /c completo? (s/N): ");
        char resp[8];
        if (!fgets(resp, sizeof(resp), stdin)) return;
        if (resp[0] != 's' && resp[0] != 'S') { printf("  Cancelado.\n"); return; }
    }

    /* DELETE /c — sin query params (draft §3.3) */
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_DELETE, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");

    printf("  DELETE /c (datastore completo)... ");
    if (send_and_wait(pdu)) printf("\n");
    (void)argv; /* suprimir warning: argv no usado cuando argc==0 */
}

/* observe [segundos]
 * Suscribe el cliente al stream /s con GET Observe(0) y procesa notificaciones
 * durante N segundos (default 20).
 */
static void cmd_observe(int argc, char **argv) {
    int secs = 20;
    if (argc >= 1) {
        secs = atoi(argv[0]);
        if (secs <= 0) secs = 20;
    }

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"s");
    /* Observe register = 0 */
    coap_add_option(pdu, COAP_OPTION_OBSERVE, 0, NULL);
    /* Esperamos instancias (CF=142) */
    uint8_t acc[4]; size_t accl = coap_encode_var_safe(acc, sizeof(acc), CF_YANG_INSTANCES);
    coap_add_option(pdu, COAP_OPTION_ACCEPT, accl, acc);

    g_response_received = 0;
    g_response_len = 0;
    g_observe_mode = 1;

    printf("  OBSERVE /s (%d s)... ", secs);
    coap_mid_t mid = coap_send(g_session, pdu);
    if (mid == COAP_INVALID_MID) {
        g_observe_mode = 0;
        printf("Error enviando\n");
        return;
    }

    int waited = 0;
    while (!g_response_received && waited < 5000) {
        coap_io_process(g_ctx, 100);
        waited += 100;
    }
    if (!g_response_received) {
        g_observe_mode = 0;
        printf("Timeout inicial\n");
        return;
    }
    printf("%d.%02d\n", COAP_RESPONSE_CLASS(g_response_code), g_response_code & 0x1F);
    if (g_response_len > 0) print_datastore(g_response_buf, g_response_len);

    printf("  Escuchando notificaciones de /s durante %d s...\n", secs);
    int loops = secs * 10;
    while (loops-- > 0) {
        coap_io_process(g_ctx, 100);
    }

    g_observe_mode = 0;
    printf("  Observe finalizado.\n");
}

/* ══════════════════════════════════════════════════════════
 * REPL
 * ════════════════════════════════════════════════════════*/
static void print_help(void) {
    printf("\n  Comandos disponibles (draft-ietf-core-comi-20):\n");
    printf("  ───────────────────────────────────────────────────────────────────\n");
    printf("  store  [tipo] [valor]         PUT /c  — inicializar datastore\n");
    printf("  store  ietf-example           PUT /c  — ejemplo exacto del draft §3.3.1\n");
    printf("  fetch  <SID> [SID...]         FETCH /c — leer SIDs concretos\n");
    printf("  fetch  [SID,clave]            FETCH /c — list instance (draft §3.1.3.1)\n");
    printf("  sfetch <SID> [SID...]         FETCH /s — stream filtrado por SID\n");
    printf("  get                           GET /c  — datastore completo\n");
    printf("  ipatch <SID> <valor>          iPATCH /c — actualizar SID (en payload)\n");
    printf("  ipatch <SID> null             iPATCH /c — borrar un SID\n");
    printf("  ipatch [SID,clave] null       iPATCH /c — borrar instancia de lista\n");
    printf("  ipatch [SID,clave] <valor>    iPATCH /c — actualizar/añadir instancia\n");
    printf("  put    <SID> <v> [SID <v>]    PUT /c  — reemplazar datastore\n");
    printf("  delete                        DELETE /c — borrar datastore completo\n");
    printf("  observe [segundos]            GET /s Observe(0) — stream de eventos\n");
    printf("  demo                          Reproduce ejemplos exactos del draft\n");
    printf("  sidcheck                      Re-verificar compatibilidad SID con el servidor\n");
    printf("  info                          mostrar configuración actual\n");
    printf("  quit / exit                   salir\n");
    printf("  ───────────────────────────────────────────────────────────────────\n");
    printf("  Content-Formats: 140=yang-data+cbor;id=sid  141=yang-identifiers+cbor-seq\n");
    printf("                   142=yang-instances+cbor-seq\n");
    printf("  Ejemplos simples:\n");
    printf("    store temperature 22.5    → PUT datastore {10:temperature, 20:22.5}\n");
    printf("    get                       → GET /c (datastore completo)\n");
    printf("    fetch 10 20               → FETCH /c SIDs [10,20]\n");
    printf("    sfetch 1755               → FETCH /s filtrando por SID 1755\n");
    printf("    ipatch 20 99.9            → iPATCH /c {20:99.9}\n");
    printf("    ipatch 20 null            → borrar SID 20\n");
    printf("    observe 30               → escuchar /s durante 30 segundos\n");
    printf("    delete                    → DELETE /c (datastore completo)\n");
    printf("  Ejemplos draft (ietf-system + ietf-interfaces):\n");
    printf("    store ietf-example        → clock(1721)+ifaces(1533)+ntp(1755,1756)\n");
    printf("    get                       → ver §3.3.1 del draft\n");
    printf("    fetch 1723 [1533,eth0]    → §3.1.3.1: current-datetime + interface eth0\n");
    printf("    ipatch 1755 true          → ntp/enabled=true (§3.2.3.1)\n");
    printf("    demo                      → pasos 1-5 del draft completos\n\n");
}

static void tokenize(char *line, char **argv, int *argc) {
    *argc = 0;
    char *p = line;
    while (*p && *argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n') break;
        argv[(*argc)++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if (*p) *p++ = '\0';
    }
}

/* ════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════*/
int main(int argc, char *argv[]) {
    if (argc >= 2) strncpy(g_device_type, argv[1], sizeof(g_device_type) - 1);

    if (load_runtime_system_sids(&g_sys_sids) != 0) {
        fprintf(stderr, "[SID] CLI FATAL: no se pudo cargar sid/ietf-system.sid\n");
        return 1;
    }
    if (load_runtime_interfaces_sids(&g_if_sids) != 0) {
        fprintf(stderr, "[SID] CLI FATAL: no se pudo cargar sid/ietf-interfaces.sid\n");
        return 1;
    }

    const char *host      = getenv("SERVER_HOST");
    const char *port_str  = getenv("SERVER_PORT");
    const char *dtls_env  = getenv("CORECONF_DTLS");   /* "0" → forzar UDP */
    const char *tls_mode  = getenv("CORECONF_TLS_MODE");
    const char *profile = getenv("CORECONF_CERT_PROFILE");
    const char *ca_file, *cert_file, *key_file;
    if (profile && strcmp(profile, "local") == 0) {
        ca_file   = getenv("CORECONF_CA_CERT")     ? getenv("CORECONF_CA_CERT")     : "certs/ca-local.crt";
        cert_file = getenv("CORECONF_CLIENT_CERT") ? getenv("CORECONF_CLIENT_CERT") : "certs/client-local.crt";
        key_file  = getenv("CORECONF_CLIENT_KEY")  ? getenv("CORECONF_CLIENT_KEY")  : "certs/client-local.key";
    } else {
        ca_file   = getenv("CORECONF_CA_CERT")     ? getenv("CORECONF_CA_CERT")     : "certs/ca.crt";
        cert_file = getenv("CORECONF_CLIENT_CERT") ? getenv("CORECONF_CLIENT_CERT") : "certs/client.crt";
        key_file  = getenv("CORECONF_CLIENT_KEY")  ? getenv("CORECONF_CLIENT_KEY")  : "certs/client.key";
    }
    const char *psk_id    = getenv("CORECONF_PSK_ID")      ? getenv("CORECONF_PSK_ID")      : "coreconf-client";
    const char *psk_key   = getenv("CORECONF_PSK_KEY")     ? getenv("CORECONF_PSK_KEY")     : "coreconf-secret";
    if (!host) host = "127.0.0.1";
    if (!tls_mode || tls_mode[0] == '\0') tls_mode = "cert";

    /* Por defecto DTLS (5684). CORECONF_DTLS=0 → UDP (5683). */
    int use_dtls = (dtls_env && dtls_env[0] == '0') ? 0 :
                   coap_dtls_is_supported()          ? 1 : 0;
    int port = port_str ? atoi(port_str) : (use_dtls ? 5684 : 5683);

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   CORECONF CLI — draft-ietf-core-comi-20                   ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Datastore único en /c — temperatura, humedad, etc.        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("  Tipo inicial: %s\n", g_device_type);
    printf("  Servidor:     %s://%s:%d/c  %s\n",
           use_dtls ? "coaps" : "coap", host, port,
           use_dtls ? "(DTLS)" : "(UDP sin cifrar)");
    if (use_dtls && strcmp(tls_mode, "psk") == 0)
        printf("  Seguridad:    PSK (id=%s)\n", psk_id);
    else if (use_dtls)
        printf("  Seguridad:    CERT (ca=%s, cert=%s, key=%s)\n", ca_file, cert_file, key_file);
    printf("  Escribe 'help' para ver los comandos.\n\n");

    coap_startup();
    coap_set_log_level(COAP_LOG_ERR);

    coap_address_t dst;
    coap_address_init(&dst);
    dst.size = sizeof(struct sockaddr_in);
    dst.addr.sin.sin_family = AF_INET;
    dst.addr.sin.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &dst.addr.sin.sin_addr) != 1) {
        fprintf(stderr, "IP inválida: %s\n", host); return 1;
    }

    g_ctx = coap_new_context(NULL);
    if (!g_ctx) { fprintf(stderr, "Error creando contexto CoAP\n"); return 1; }

    if (use_dtls && strcmp(tls_mode, "psk") == 0) {
        coap_dtls_cpsk_t cpsk;
        memset(&cpsk, 0, sizeof(cpsk));
        cpsk.version                   = COAP_DTLS_CPSK_SETUP_VERSION;
        cpsk.client_sni                = NULL;
        cpsk.psk_info.identity.s       = (const uint8_t *)psk_id;
        cpsk.psk_info.identity.length  = strlen(psk_id);
        cpsk.psk_info.key.s            = (const uint8_t *)psk_key;
        cpsk.psk_info.key.length       = strlen(psk_key);
        g_session = coap_new_client_session_psk2(g_ctx, NULL, &dst, COAP_PROTO_DTLS, &cpsk);
    } else if (use_dtls) {
        coap_dtls_pki_t pki;
        memset(&pki, 0, sizeof(pki));
        pki.version                  = COAP_DTLS_PKI_SETUP_VERSION;
        pki.verify_peer_cert         = 1;
        pki.check_common_ca          = 1;
        pki.allow_self_signed        = 0;
        pki.allow_expired_certs      = 0;
        pki.cert_chain_validation    = 1;
        pki.cert_chain_verify_depth  = 3;
        pki.check_cert_revocation    = 0;
        pki.allow_no_crl             = 1;
        pki.allow_expired_crl        = 1;
        pki.client_sni               = (char *)host;
        pki.pki_key.key_type         = COAP_PKI_KEY_PEM;
        pki.pki_key.key.pem.ca_file     = ca_file;
        pki.pki_key.key.pem.public_cert = cert_file;
        pki.pki_key.key.pem.private_key = key_file;
        g_session = coap_new_client_session_pki(g_ctx, NULL, &dst, COAP_PROTO_DTLS, &pki);
    } else {
        g_session = coap_new_client_session(g_ctx, NULL, &dst, COAP_PROTO_UDP);
    }
    if (!g_session) {
        fprintf(stderr, "Error creando sesión %s\n", use_dtls ? "DTLS" : "CoAP");
        coap_free_context(g_ctx); return 1;
    }
    coap_register_response_handler(g_ctx, response_handler);

    if (check_server_sid_compatibility() != 0) {
        coap_session_release(g_session);
        coap_free_context(g_ctx);
        coap_cleanup();
        return 1;
    }

    /* ── REPL ── */
    char line[512];
    while (1) {
        printf("coreconf> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break; /* EOF / Ctrl-D */

        /* Trim */
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r')) line[--ll] = '\0';
        if (ll == 0) continue;

        char *args[MAX_ARGS]; int nargs = 0;
        tokenize(line, args, &nargs);
        if (nargs == 0) continue;

        const char *cmd = args[0];

        if      (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
            print_help();
        else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0)
            break;
        else if (strcmp(cmd, "info") == 0)
             printf("  tipo=%s  servidor=%s://%s:%d/c  tls-mode=%s\n",
                 g_device_type,
                 use_dtls ? "coaps" : "coap", host, port,
                 use_dtls ? tls_mode : "none");
        else if (strcmp(cmd, "store")  == 0) cmd_store (nargs-1, args+1);
        else if (strcmp(cmd, "fetch")  == 0) cmd_fetch (nargs-1, args+1);
        else if (strcmp(cmd, "sfetch") == 0) cmd_sfetch(nargs-1, args+1);
        else if (strcmp(cmd, "get")    == 0) cmd_get   ();
        else if (strcmp(cmd, "ipatch") == 0) cmd_ipatch(nargs-1, args+1);
        else if (strcmp(cmd, "put")    == 0) cmd_put   (nargs-1, args+1);
        else if (strcmp(cmd, "delete") == 0) cmd_delete(nargs-1, args+1);
        else if (strcmp(cmd, "observe") == 0) cmd_observe(nargs-1, args+1);
        else if (strcmp(cmd, "demo")     == 0) cmd_demo  ();
        else if (strcmp(cmd, "sidcheck") == 0) check_server_sid_compatibility();
        else
            printf("  Comando desconocido: '%s'  (escribe 'help')\n", cmd);
    }

    printf("\nSaliendo...\n");
    coap_session_release(g_session);
    coap_free_context(g_ctx);
    coap_cleanup();
    return 0;
}

/* Interactive CORECONF CLI client over CoAP/CoAPS using libcoap. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <coap3/coap.h>
#include <netdb.h>

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
#include "../../include/post.h"
#include "../../include/put.h"
#include "../../include/delete.h"
#include "../../include/sids.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096
#define MAX_ARGS    32


#define CF_YANG_DATA_CBOR    140
#define CF_YANG_IDENTIFIERS  141
#define CF_YANG_INSTANCES    142


static coap_context_t  *g_ctx     = NULL;
static coap_session_t  *g_session = NULL;
static char             g_device_type[64] = "temperature";


static int             g_response_received = 0;
static uint8_t         g_response_buf[BUFFER_SIZE];
static size_t          g_response_len      = 0;
static coap_pdu_code_t g_response_code;
static int             g_observe_mode      = 0;

static void print_datastore(const uint8_t *data, size_t len);
static void print_post_response(const uint8_t *data, size_t len);

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

typedef struct RuntimeOperationSids {
    uint64_t reboot_rpc_sid;
    uint64_t reboot_input_delay;
    uint64_t reset_action_sid;
    uint64_t reset_input_reset_at;
    uint64_t reset_output_finished_at;
    int loaded;
} RuntimeOperationSidsT;

static RuntimeOperationSidsT g_ops_sids = {0};

#define DEFAULT_RPC_REBOOT_SID              61000ULL
#define DEFAULT_RPC_REBOOT_INPUT_DELAY      1ULL
#define DEFAULT_ACTION_RESET_SID            60002ULL
#define DEFAULT_ACTION_RESET_INPUT_RESET_AT 1ULL
#define DEFAULT_ACTION_RESET_OUTPUT_FINISHED_AT 2ULL

static uint64_t sid_hash_mix_u64(uint64_t hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
    return hash;
}

static uint64_t compute_sid_fingerprint(const RuntimeSystemSidsT *sys,
                                        const RuntimeInterfacesSidsT *ifs,
                                        const RuntimeOperationSidsT *ops) {
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
    h = sid_hash_mix_u64(h, ops->reboot_rpc_sid);
    h = sid_hash_mix_u64(h, ops->reboot_input_delay);
    h = sid_hash_mix_u64(h, ops->reset_action_sid);
    h = sid_hash_mix_u64(h, ops->reset_input_reset_at);
    h = sid_hash_mix_u64(h, ops->reset_output_finished_at);
    return h;
}

static int check_server_sid_compatibility(void) {
    uint64_t local_fp = compute_sid_fingerprint(&g_sys_sids, &g_if_sids, &g_ops_sids);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 3, (uint8_t *)"sid");

    g_response_received = 0;
    g_response_len = 0;
    coap_mid_t mid = coap_send(g_session, pdu);
    if (mid == COAP_INVALID_MID) {
        fprintf(stderr, "[SID] CLI: error sending GET /sid. (Check if DTLS failed)\n");
        return -1;
    }

    int waited = 0;
    while (!g_response_received && waited < 5000) {
        coap_io_process(g_ctx, 100);
        waited += 100;
    }
    if (!g_response_received || g_response_len == 0) {
        fprintf(stderr, "[SID] CLI: timeout/empty response on GET /sid\n");
        return -1;
    }

    char payload[128];
    size_t n = g_response_len < sizeof(payload) - 1 ? g_response_len : sizeof(payload) - 1;
    memcpy(payload, g_response_buf, n);
    payload[n] = '\0';

    uint64_t remote_fp = 0;
    if (sscanf(payload, "sid-fingerprint=%" SCNu64, &remote_fp) != 1) {
        fprintf(stderr, "[SID] CLI: invalid format on /sid: %s\n", payload);
        return -1;
    }

    printf("[SID] CLI local fingerprint=%" PRIu64 "\n", local_fp);
    printf("[SID] Server fingerprint=%" PRIu64 "\n", remote_fp);

    if (local_fp != remote_fp) {
        fprintf(stderr, "[SID] CLI FATAL: SID dictionary incompatible with server\n");
        return -1;
    }
    printf("[SID] client/server SID compatibility OK\n");
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
        fprintf(stderr, "[SID] CLI: ietf-system.sid not found\n");
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
        fprintf(stderr, "[SID] CLI: error parsing critical SIDs in %s\n", chosen);
        return -1;
    }

    out->loaded = 1;
    printf("[SID] CLI loaded %s\n", chosen);
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
        fprintf(stderr, "[SID] CLI: ietf-interfaces.sid not found\n");
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
        fprintf(stderr, "[SID] CLI: error parsing critical SIDs in %s\n", chosen);
        return -1;
    }

    out->loaded = 1;
    printf("[SID] CLI loaded %s\n", chosen);
    return 0;
}

static int load_runtime_operation_sids(RuntimeOperationSidsT *out) {
    if (!out) return -1;

    const char *candidates[] = {
        "sid/ietf-coreconf-actions.sid",
        "../sid/ietf-coreconf-actions.sid",
        "../../sid/ietf-coreconf-actions.sid",
        "../../../sid/ietf-coreconf-actions.sid",
        "/app/sid/ietf-coreconf-actions.sid",
    };

    char *text = NULL;
    const char *chosen = NULL;
    if (load_text_from_candidates(candidates,
                                  sizeof(candidates) / sizeof(candidates[0]),
                                  &text, &chosen) != 0) {
        fprintf(stderr, "[SID] CLI warning: ietf-coreconf-actions.sid not found, using built-in POST SIDs\n");
        out->reboot_rpc_sid = DEFAULT_RPC_REBOOT_SID;
        out->reboot_input_delay = DEFAULT_RPC_REBOOT_INPUT_DELAY;
        out->reset_action_sid = DEFAULT_ACTION_RESET_SID;
        out->reset_input_reset_at = DEFAULT_ACTION_RESET_INPUT_RESET_AT;
        out->reset_output_finished_at = DEFAULT_ACTION_RESET_OUTPUT_FINISHED_AT;
        out->loaded = 1;
        return 0;
    }

    int ok = 0;
    ok |= parse_sid_after_identifier(text, "/ietf-coreconf-actions:reboot\"", &out->reboot_rpc_sid);
    ok |= parse_sid_after_identifier(text, "/ietf-coreconf-actions:reboot/input/delay\"", &out->reboot_input_delay);
    ok |= parse_sid_after_identifier(text, "/ietf-coreconf-actions:reset\"", &out->reset_action_sid);
    ok |= parse_sid_after_identifier(text, "/ietf-coreconf-actions:reset/input/reset-at\"", &out->reset_input_reset_at);
    ok |= parse_sid_after_identifier(text, "/ietf-coreconf-actions:reset/output/reset-finished-at\"", &out->reset_output_finished_at);

    free(text);

    if (ok != 0) {
        fprintf(stderr, "[SID] CLI: error parsing POST operation SIDs in %s\n", chosen);
        return -1;
    }

    out->loaded = 1;
    printf("[SID] CLI loaded %s\n", chosen);
    return 0;
}


static coap_response_t response_handler(coap_session_t *sess,
                                          const coap_pdu_t *sent,
                                          const coap_pdu_t *recv,
                                          const coap_mid_t mid) {
    (void)sess; (void)sent; (void)mid; // Silence unused-parameter warnings

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
            printf("    (stream without payload)\n");
        }
        printf("coreconf> ");
        fflush(stdout);
    }

    g_response_received = 1; // Notify send_and_wait that a response arrived
    return COAP_RESPONSE_OK;
}

static int send_and_wait(coap_pdu_t *pdu) {
    g_response_received = 0; // Reset response flag and payload buffer
    g_response_len      = 0;
    coap_mid_t mid = coap_send(g_session, pdu);

    if (mid == COAP_INVALID_MID) { 
        printf(" Send error\n"); 
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
        default: printf("(type %d)", val->type); break;
    }
}

static void print_datastore(const uint8_t *data, size_t len) {
    CoreconfValueT *ds = parse_get_response(data, len);
    if (!ds || ds->type != CORECONF_HASHMAP) {
        printf("(decode error)\n");
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
    printf("    ── %zu root node(s) ──\n", shown);
    freeCoreconf(ds, true);
}

static void print_post_response(const uint8_t *data, size_t len) {
    const uint8_t *ptr = data;
    const uint8_t *end = data + len;
    size_t shown = 0;

    printf("\n");
    while (ptr < end) {
        zcbor_state_t dec[12];
        zcbor_new_decode_state(dec, 12, ptr, (size_t)(end - ptr), 1, NULL, 0);

        if (!zcbor_map_start_decode(dec) || zcbor_array_at_end(dec)) {
            printf("    (decode error)\n");
            return;
        }

        uint64_t sid = 0;
        int key_is_iid = 0;
        char iid_key[128] = {0};
        zcbor_major_type_t key_mt = ZCBOR_MAJOR_TYPE(*dec[0].payload);

        if (key_mt == ZCBOR_MAJOR_TYPE_PINT) {
            if (!zcbor_uint64_decode(dec, &sid)) {
                printf("    (decode error)\n");
                return;
            }
        } else if (key_mt == ZCBOR_MAJOR_TYPE_LIST) {
            struct zcbor_string zs;
            if (!zcbor_list_start_decode(dec) ||
                !zcbor_uint64_decode(dec, &sid) ||
                !zcbor_tstr_decode(dec, &zs) ||
                !zcbor_list_end_decode(dec)) {
                printf("    (decode error)\n");
                return;
            }
            size_t kl = zs.len < sizeof(iid_key) - 1 ? zs.len : sizeof(iid_key) - 1;
            memcpy(iid_key, zs.value, kl);
            iid_key[kl] = '\0';
            key_is_iid = 1;
        } else {
            printf("    (decode error)\n");
            return;
        }

        if (key_is_iid) {
            printf("    [%" PRIu64 ",\"%s\"] : ", sid, iid_key);
        } else {
            printf("    %-8" PRIu64 " : ", sid);
        }

        if (zcbor_nil_expect(dec, NULL)) {
            printf("null");
        } else if (zcbor_map_start_decode(dec)) {
            printf("{");
            int first = 1;
            while (!zcbor_array_at_end(dec)) {
                uint64_t inner_sid = 0;
                struct zcbor_string inner_str;
                if (!zcbor_uint64_decode(dec, &inner_sid) || !zcbor_tstr_decode(dec, &inner_str)) {
                    printf("(decode error)");
                    return;
                }
                if (!first) printf(", ");
                printf("%" PRIu64 ":\"%.*s\"", inner_sid, (int)inner_str.len, (const char *)inner_str.value);
                first = 0;
            }
            if (!zcbor_map_end_decode(dec)) {
                printf("(decode error)");
                return;
            }
            printf("}");
        } else {
            printf("(unsupported value)");
        }

        if (!zcbor_array_at_end(dec) || !zcbor_map_end_decode(dec)) {
            printf("\n    (decode error)\n");
            return;
        }

        printf("\n");
        ptr = dec[0].payload;
        shown++;
    }

    printf("    -- %zu root node(s) --\n", shown);
}


static CoreconfValueT *parse_value_ptr(const char **p);

static CoreconfValueT *parse_map_ptr(const char **p) {
    CoreconfValueT *map = createCoreconfHashmap();
    while (**p && **p != '}') {
        while (**p == ' ' || **p == ',') (*p)++;
        if (**p == '}') break;
        char *ep;
        uint64_t key = (uint64_t)strtoull(*p, &ep, 10);
        if (ep == *p) break;  
        *p = ep;
        while (**p == ' ') (*p)++;
        if (**p == ':') (*p)++;
        while (**p == ' ') (*p)++;
        CoreconfValueT *val = parse_value_ptr(p);
        if (val) insertCoreconfHashMap(map->data.map_value, key, val);
        while (**p == ' ') (*p)++;
    }
    if (**p == '}') (*p)++;  
    return map;
}

static CoreconfValueT *parse_value_ptr(const char **p) {
    while (**p == ' ') (*p)++;
    if (**p == '{') {
        (*p)++;  
        return parse_map_ptr(p);
    }
    const char *start = *p;
    while (**p && **p != ',' && **p != '}') (*p)++;
    size_t len = (size_t)(*p - start);
    char buf[256];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
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


static void cmd_store(int argc, char **argv) {
    const char *tipo = (argc >= 1) ? argv[0] : g_device_type;
    double valor     = (argc >= 2) ? atof(argv[1]) : 22.5;

    strncpy(g_device_type, tipo, sizeof(g_device_type) - 1);

    CoreconfValueT  *data = createCoreconfHashmap();
    CoreconfHashMapT *map = data->data.map_value;

    if (strcmp(tipo, "ietf-example") == 0) {
        if (!g_sys_sids.loaded || !g_if_sids.loaded) {
            printf("  Error: runtime SID dictionary not loaded\n");
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

        CoreconfValueT *clock_c = createCoreconfHashmap();
        insertCoreconfHashMap(clock_c->data.map_value, delta_clock_current,
            createCoreconfString("2016-10-26T12:16:31Z")); 
        insertCoreconfHashMap(clock_c->data.map_value, delta_clock_boot,
            createCoreconfString("2014-10-05T09:00:00Z")); 
        insertCoreconfHashMap(map, g_sys_sids.clock, clock_c);

        CoreconfValueT *iface = createCoreconfHashmap();
        insertCoreconfHashMap(iface->data.map_value, delta_if_name,
            createCoreconfString("eth0"));              
        insertCoreconfHashMap(iface->data.map_value, delta_if_desc,
            createCoreconfString("Ethernet adaptor")); 
        insertCoreconfHashMap(iface->data.map_value, delta_if_type,
            createCoreconfUint64(g_if_sids.identity_ethernet_csmacd)); 
        insertCoreconfHashMap(iface->data.map_value, delta_if_enabled,
            createCoreconfBoolean(true));              
        insertCoreconfHashMap(iface->data.map_value, delta_if_oper,
            createCoreconfUint64(3));                  
        CoreconfValueT *ifaces = createCoreconfArray();
        addToCoreconfArray(ifaces, iface);
        free(iface); 
        insertCoreconfHashMap(map, g_if_sids.iface, ifaces);

        insertCoreconfHashMap(map, g_sys_sids.ntp_enabled, createCoreconfBoolean(false));

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
                 printf("ietf-example datastore loaded (clock %" PRIu64 ", ifaces %" PRIu64 ", ntp %" PRIu64 "/%" PRIu64 ")\n",
                     g_sys_sids.clock, g_if_sids.iface, g_sys_sids.ntp_enabled, g_sys_sids.ntp_server);
        else
            printf("datastore initialized with SIDs 10,11,20,21\n");
    }
}

static void cmd_fetch(int argc, char **argv) {
    if (argc == 0) {
        printf("  Usage: fetch <SID> [SID...] [[SID,key]...]\n");
        printf("  Ex:  fetch 1723 [1533,eth0]  (ejemplo §3.1.3.1 del draft)\n");
        return;
    }

    InstanceIdentifier iids[64]; int n = 0;
    for (int i = 0; i < argc && i < 64; i++) {
        if (argv[i][0] == '[') {
            char *p = argv[i] + 1;   
            char *comma = strchr(p, ',');
            if (!comma) {
                printf("  Error: expected [SID,key], received: %s\n", argv[i]);
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
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_YANG_IDENTIFIERS);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    uint8_t acc[4]; size_t accl = coap_encode_var_safe(acc, sizeof(acc), CF_YANG_INSTANCES);
    coap_add_option(pdu, COAP_OPTION_ACCEPT, accl, acc);
    coap_add_data(pdu, len, buf);

    printf("  FETCH /c (%d IID(s), %zu bytes req)... ", n, len);
    if (send_and_wait(pdu) && g_response_len > 0)
        print_datastore(g_response_buf, g_response_len);
    else if (g_response_len == 0)
        printf("no payload\n");
    (void)accl;
}

static void cmd_sfetch(int argc, char **argv) {
    if (argc == 0) {
        printf("  Usage: sfetch <SID> [SID...] [[SID,key]...]\n");
        return;
    }

    InstanceIdentifier iids[64]; int n = 0;
    for (int i = 0; i < argc && i < 64; i++) {
        if (argv[i][0] == '[') {
            char *p = argv[i] + 1;
            char *comma = strchr(p, ',');
            if (!comma) {
                printf("  Error: expected [SID,key], received: %s\n", argv[i]);
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
        printf("stream without payload\n");
    (void)accl;
}

static void cmd_get(void) {
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");

    printf("  GET /c... ");
    if (send_and_wait(pdu)) {
        if (g_response_len > 0)
            print_datastore(g_response_buf, g_response_len);
        else
            printf("no payload\n");
    }
}

static void cmd_ipatch(int argc, char **argv) {
    if (argc < 2) {
        printf("  Usage: ipatch <SID> <value>\n");
        printf("  Usage: ipatch [SID,key] null|<value>\n");
        return;
    }

    uint8_t buf[BUFFER_SIZE];
    size_t  len = 0;
    coap_pdu_t *pdu = NULL;

    if (argv[0][0] == '[') {
        char tmp[256];
        strncpy(tmp, argv[0] + 1, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *closing = strchr(tmp, ']');
        if (closing) *closing = '\0';
        char *comma = strchr(tmp, ',');
        if (!comma) { printf("  Error: expected format [SID,key]\n"); return; }
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

static void cmd_post(int argc, char **argv) {
    if (argc < 1) {
        printf("  Usage: post reboot [delay-seconds]\n");
        printf("  Usage: post reset <server-name> <reset-at-rfc3339>\n");
        return;
    }

    uint8_t buf[BUFFER_SIZE];
    size_t len = 0;
    const char *op = argv[0];

    if (strcmp(op, "reboot") == 0) {
        if (argc > 2) {
            printf("  Usage: post reboot [delay-seconds]\n");
            return;
        }

        uint64_t delay = 0;
        if (argc == 2) {
            char *endp = NULL;
            delay = strtoull(argv[1], &endp, 10);
            if (!endp || *endp != '\0') {
                printf("  Error: delay must be an unsigned integer.\n");
                return;
            }
        }

        CoreconfValueT *input = createCoreconfHashmap();
        if (!input) {
            printf("  Error: out of memory.\n");
            return;
        }
        insertCoreconfHashMap(input->data.map_value, g_ops_sids.reboot_input_delay, createCoreconfUint64(delay));
        len = create_post_request_rpc(buf, BUFFER_SIZE, g_ops_sids.reboot_rpc_sid, input);
        freeCoreconf(input, true);

        if (len == 0) {
            printf("  Error: failed to encode reboot RPC payload.\n");
            return;
        }
        printf("  POST /c reboot(delay=%" PRIu64 ")... ", delay);

    } else if (strcmp(op, "reset") == 0) {
        if (argc != 3) {
            printf("  Usage: post reset <server-name> <reset-at-rfc3339>\n");
            return;
        }

        const char *server_name = argv[1];
        const char *reset_at = argv[2];
        CoreconfValueT *input = createCoreconfHashmap();
        insertCoreconfHashMap(input->data.map_value, g_ops_sids.reset_input_reset_at, createCoreconfString(reset_at));
        len = create_ipatch_iid_request(buf, BUFFER_SIZE, g_ops_sids.reset_action_sid, server_name, input);
        freeCoreconf(input, true);

        if (len == 0) {
            printf("  Error: failed to encode reset action payload.\n");
            return;
        }

        printf("  POST /c reset(server=%s)... ", server_name);

    } else {
        printf("  Unknown post operation: %s\n", op);
        printf("  Supported operations: reboot, reset\n");
        return;
    }

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");

    uint8_t cf[4];
    size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_YANG_INSTANCES);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);

    uint8_t acc[4];
    size_t accl = coap_encode_var_safe(acc, sizeof(acc), CF_YANG_INSTANCES);
    coap_add_option(pdu, COAP_OPTION_ACCEPT, accl, acc);
    coap_add_data(pdu, len, buf);

    if (send_and_wait(pdu)) {
        if (g_response_len > 0)
            print_post_response(g_response_buf, g_response_len);
        else
            printf("no payload\n");
    }
}

static void cmd_demo(void) {
    if (!g_sys_sids.loaded || !g_if_sids.loaded) {
        printf("\n[SID] Error: runtime SID dictionary not loaded in CLI\n\n");
        return;
    }

    uint64_t delta_ntp_name     = g_sys_sids.ntp_server_name - g_sys_sids.ntp_server;
    uint64_t delta_ntp_prefer   = g_sys_sids.ntp_server_prefer - g_sys_sids.ntp_server;
    uint64_t delta_ntp_udp      = g_sys_sids.ntp_server_udp - g_sys_sids.ntp_server;
    uint64_t delta_ntp_udp_addr = g_sys_sids.ntp_server_udp_address - g_sys_sids.ntp_server_udp;

    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  DEMO: Exact draft-ietf-core-comi-20 examples              ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    printf("── STEP 1: store ietf-example  (§3.3.1 PUT /c) ─────────────────\n");
    char *store_args[] = {"ietf-example"};
    cmd_store(1, store_args);
    printf("\n");

    printf("── STEP 2: GET /c — full datastore  (§3.3.1) ───────────────────\n");
    printf("  Server response:\n");
    cmd_get();
    printf("\n");

    printf("── STEP 3: FETCH /c  (§3.1.3.1) ────────────────────────────────\n");
    printf("  REQ payload (yang-identifiers+cbor-seq):\n");
    printf("    %" PRIu64 ",             / current-datetime /\n", g_sys_sids.clock_current);
    printf("    [%" PRIu64 ", \"eth0\"]   / interface con name=\"eth0\" /\n", g_if_sids.iface);
    printf("  Server response:\n");
    {
        char fa0[32];
        char fa1[64];
        snprintf(fa0, sizeof(fa0), "%" PRIu64, g_sys_sids.clock_current);
        snprintf(fa1, sizeof(fa1), "[%" PRIu64 ",eth0]", g_if_sids.iface);
        char *fetch_args[] = {fa0, fa1};
        cmd_fetch(2, fetch_args);
    }
    printf("\n");

    printf("── STEP 4: iPATCH /c  (§3.2.3.1 — NTP) ────────────────────────\n");
    printf("  REQ payload (yang-instances+cbor-seq, 3 mapas):\n");
    printf("    { %" PRIu64 ": true }                        set ntp/enabled\n", g_sys_sids.ntp_enabled);
    printf("    { [%" PRIu64 ", \"tac.nrc.ca\"]: null }       remove NTP server\n", g_sys_sids.ntp_server);
    printf("    { %" PRIu64 ": { %" PRIu64 ":\"tic.nrc.ca\", %" PRIu64 ":true,\n",
           g_sys_sids.ntp_server, delta_ntp_name, delta_ntp_prefer);
    printf("               %" PRIu64 ":{ %" PRIu64 ":\"132.246.11.231\" } } add NTP server\n",
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

        printf("  Total payload: %zu bytes (3 concatenated maps in cbor-seq)\n", offset);
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

    printf("── STEP 5: GET /c — post-iPATCH verification ───────────────────\n");
    printf("  ntp/enabled (SID %" PRIu64 ") must be true\n", g_sys_sids.ntp_enabled);
    printf("  ntp/server  (SID %" PRIu64 ") must contain tic.nrc.ca con address 132.246.11.231\n", g_sys_sids.ntp_server);
    cmd_get();
    printf("\nDemo completed.\n\n");
}

static void cmd_put(int argc, char **argv) {
    if (argc < 2 || argc % 2 != 0) {
        printf("  Usage: put <SID> <value> [SID value ...]\n"); return;
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

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_PUT, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_YANG_DATA_CBOR);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_add_data(pdu, len, buf);

    printf("  PUT /c (%d SIDs)... ", argc / 2);
    if (send_and_wait(pdu)) printf("\n");
}

static void cmd_delete(int argc, char **argv) {
    if (argc > 0) {
        printf("  WARNING: DELETE removes the full datastore.\n");
        printf("  To delete a specific SID use: ipatch <SID> null\n");
        printf("  Continue with full DELETE /c? (y/N): ");
        char resp[8];
        if (!fgets(resp, sizeof(resp), stdin)) return;
        if (resp[0] != 'y' && resp[0] != 'Y') { printf("  Cancelled.\n"); return; }
    }

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_DELETE, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");

    printf("  DELETE /c (full datastore)... ");
    if (send_and_wait(pdu)) printf("\n");
    (void)argv; 
}

static void cmd_observe(int argc, char **argv) {
    int secs = 20;
    if (argc >= 1) {
        secs = atoi(argv[0]);
        if (secs <= 0) secs = 20;
    }

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"s");
    coap_add_option(pdu, COAP_OPTION_OBSERVE, 0, NULL);
    uint8_t acc[4]; size_t accl = coap_encode_var_safe(acc, sizeof(acc), CF_YANG_INSTANCES);
    coap_add_option(pdu, COAP_OPTION_ACCEPT, accl, acc);

    g_response_received = 0;
    g_response_len = 0;
    g_observe_mode = 1;

    printf("  OBSERVE /s (%d s)... ", secs);
    coap_mid_t mid = coap_send(g_session, pdu);
    if (mid == COAP_INVALID_MID) {
        g_observe_mode = 0;
        printf("Send error\n");
        return;
    }

    int waited = 0;
    while (!g_response_received && waited < 5000) {
        coap_io_process(g_ctx, 100);
        waited += 100;
    }
    if (!g_response_received) {
        g_observe_mode = 0;
        printf("Initial timeout\n");
        return;
    }
    printf("%d.%02d\n", COAP_RESPONSE_CLASS(g_response_code), g_response_code & 0x1F);
    if (g_response_len > 0) print_datastore(g_response_buf, g_response_len);

    printf("  Listening for /s notifications for %d s...\n", secs);
    int loops = secs * 10;
    while (loops-- > 0) {
        coap_io_process(g_ctx, 100);
    }

    g_observe_mode = 0;
    printf("  Observe finished.\n");
}


static void print_help(void) {
    printf("\n  Available commands (draft-ietf-core-comi-20):\n");
    printf("  ───────────────────────────────────────────────────────────────────\n");
    printf("  store  [type] [value]         PUT /c  - initialize datastore\n");
    printf("  store  ietf-example           PUT /c  - exact draft example §3.3.1\n");
    printf("  fetch  <SID> [SID...]         FETCH /c - read specific SIDs\n");
    printf("  fetch  [SID,key]              FETCH /c - list instance (draft §3.1.3.1)\n");
    printf("  sfetch <SID> [SID...]         FETCH /s - SID-filtered stream\n");
    printf("  get                           GET /c  - full datastore\n");
    printf("  ipatch <SID> <value>          iPATCH /c - update SID (in payload)\n");
    printf("  ipatch <SID> null             iPATCH /c - delete one SID\n");
    printf("  ipatch [SID,key] null         iPATCH /c - delete list instance\n");
    printf("  ipatch [SID,key] <value>      iPATCH /c - update/add instance\n");
    printf("  post reboot [delay]           POST /c - invoke RPC reboot (SID 61000)\n");
    printf("  post reset <name> <reset-at>  POST /c - invoke action reset (SID 60002)\n");
    printf("  put    <SID> <v> [SID <v>]    PUT /c  - replace datastore\n");
    printf("  delete                        DELETE /c - delete full datastore\n");
    printf("  observe [seconds]             GET /s Observe(0) - event stream\n");
    printf("  demo                          replay exact draft examples\n");
    printf("  sidcheck                      re-check SID compatibility with server\n");
    printf("  info                          show current configuration\n");
    printf("  quit / exit                   exit\n");
    printf("  ───────────────────────────────────────────────────────────────────\n");
    printf("  Content-Formats: 140=yang-data+cbor;id=sid  141=yang-identifiers+cbor-seq\n");
    printf("                   142=yang-instances+cbor-seq\n");
    printf("  Simple examples:\n");
    printf("    store temperature 22.5    → PUT datastore {10:temperature, 20:22.5}\n");
    printf("    get                       → GET /c (full datastore)\n");
    printf("    fetch 10 20               → FETCH /c SIDs [10,20]\n");
    printf("    sfetch 1755               → FETCH /s filtering by SID 1755\n");
    printf("    ipatch 20 99.9            → iPATCH /c {20:99.9}\n");
    printf("    ipatch 20 null            → delete SID 20\n");
    printf("    post reboot 77            → POST /c {61000:{1:77}}\n");
    printf("    post reset myserver 2016-02-08T14:10:08Z\n");
    printf("                             → POST /c {[60002,\"myserver\"]:{1:\"...\"}}\n");
    printf("    observe 30               → listen on /s for 30 seconds\n");
    printf("    delete                    → DELETE /c (full datastore)\n");
    printf("  Draft examples (ietf-system + ietf-interfaces):\n");
    printf("    store ietf-example        → clock(1721)+ifaces(1533)+ntp(1755,1756)\n");
    printf("    get                       → see §3.3.1 of the draft\n");
    printf("    fetch 1723 [1533,eth0]    → §3.1.3.1: current-datetime + interface eth0\n");
    printf("    ipatch 1755 true          → ntp/enabled=true (§3.2.3.1)\n");
    printf("    demo                      → complete draft steps 1-5\n\n");
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


int main(int argc, char *argv[]) {
    if (argc >= 2) strncpy(g_device_type, argv[1], sizeof(g_device_type) - 1);

    if (load_runtime_system_sids(&g_sys_sids) != 0) {
        fprintf(stderr, "[SID] CLI FATAL: could not load sid/ietf-system.sid\n");
        return 1;
    }
    if (load_runtime_interfaces_sids(&g_if_sids) != 0) {
        fprintf(stderr, "[SID] CLI FATAL: could not load sid/ietf-interfaces.sid\n");
        return 1;
    }
    if (load_runtime_operation_sids(&g_ops_sids) != 0) {
        fprintf(stderr, "[SID] CLI FATAL: could not load sid/ietf-coreconf-actions.sid\n");
        return 1;
    }

    const char *host      = getenv("SERVER_HOST");
    const char *port_str  = getenv("SERVER_PORT");
    const char *sni       = getenv("SERVER_SNI");   
    const char *dtls_env  = getenv("CORECONF_DTLS");   
    const char *tls_mode  = getenv("CORECONF_TLS_MODE");
    const char *profile   = getenv("CORECONF_CERT_PROFILE");
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
    
    if (!host) host = "localhost";
    if (!sni) sni = "localhost";
    if (!tls_mode || tls_mode[0] == '\0') tls_mode = "cert";

    int use_dtls = (dtls_env && dtls_env[0] == '0') ? 0 :
                   coap_dtls_is_supported()          ? 1 : 0;
    int port = port_str ? atoi(port_str) : (use_dtls ? 5684 : 5683);

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   CORECONF CLI — draft-ietf-core-comi-20                   ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Unified datastore at /c - temperature, humidity, etc.        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("  Initial type: %s\n", g_device_type);
    printf("  Server:       %s://%s:%d/c  %s\n",
           use_dtls ? "coaps" : "coap", host, port,
           use_dtls ? "(DTLS)" : "(unencrypted UDP)");

    if (use_dtls && strcmp(tls_mode, "psk") == 0) {
        printf("  Security:     PSK (id=%s)\n", psk_id);
    } else if (use_dtls) {
        printf("  Security:     CERT (ca=%s, cert=%s, key=%s)\n", ca_file, cert_file, key_file);
        
        if (access(ca_file, R_OK) != 0) fprintf(stderr, "\n[DTLS WARN] Cannot read CA file: %s\n", ca_file);
        if (access(cert_file, R_OK) != 0) fprintf(stderr, "[DTLS WARN] Cannot read client certificate: %s\n", cert_file);
        if (access(key_file, R_OK) != 0) fprintf(stderr, "[DTLS WARN] Cannot read client private key: %s\n\n", key_file);
    }
    printf("  Type 'help' to see commands.\n\n");

    coap_startup();

    const char *log_env = getenv("COAP_DEBUG_LEVEL");
    if (log_env && strcmp(log_env, "debug") == 0) {
        coap_set_log_level(COAP_LOG_DEBUG);
    } else {
        coap_set_log_level(COAP_LOG_WARN);
    }

    coap_address_t dst;
    coap_address_init(&dst);
    dst.size = sizeof(struct sockaddr_in);
    dst.addr.sin.sin_family = AF_INET;
    dst.addr.sin.sin_port   = htons(port);
    
   struct hostent *he = gethostbyname(host);
    if (he) {
        memcpy(&dst.addr.sin.sin_addr, he->h_addr_list[0], he->h_length);
    } else if (inet_pton(AF_INET, host, &dst.addr.sin.sin_addr) != 1) {
        fprintf(stderr, "Unresolvable IP/host: %s\n", host); return 1;
    }

    g_ctx = coap_new_context(NULL);
    if (!g_ctx) { fprintf(stderr, "Error creating CoAP context\n"); return 1; }

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
        pki.client_sni               = (char *)sni;
        pki.pki_key.key_type         = COAP_PKI_KEY_PEM;
        pki.pki_key.key.pem.ca_file     = ca_file;
        pki.pki_key.key.pem.public_cert = cert_file;
        pki.pki_key.key.pem.private_key = key_file;
        g_session = coap_new_client_session_pki(g_ctx, NULL, &dst, COAP_PROTO_DTLS, &pki);
    } else {
        g_session = coap_new_client_session(g_ctx, NULL, &dst, COAP_PROTO_UDP);
    }
    
    if (!g_session) {
        fprintf(stderr, "Error creating session %s\n", use_dtls ? "DTLS" : "CoAP");
        coap_free_context(g_ctx); return 1;
    }
    coap_register_response_handler(g_ctx, response_handler);

    if (check_server_sid_compatibility() != 0) {
        coap_session_release(g_session);
        coap_free_context(g_ctx);
        coap_cleanup();
        return 1;
    }

    
    char line[512];
    while (1) {
        printf("coreconf> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break; 

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
             printf("  type=%s  server=%s://%s:%d/c  tls-mode=%s\n",
                 g_device_type,
                 use_dtls ? "coaps" : "coap", host, port,
                 use_dtls ? tls_mode : "none");
        else if (strcmp(cmd, "store")  == 0) cmd_store (nargs-1, args+1);
        else if (strcmp(cmd, "fetch")  == 0) cmd_fetch (nargs-1, args+1);
        else if (strcmp(cmd, "sfetch") == 0) cmd_sfetch(nargs-1, args+1);
        else if (strcmp(cmd, "get")    == 0) cmd_get   ();
        else if (strcmp(cmd, "ipatch") == 0) cmd_ipatch(nargs-1, args+1);
        else if (strcmp(cmd, "post")   == 0) cmd_post  (nargs-1, args+1);
        else if (strcmp(cmd, "put")    == 0) cmd_put   (nargs-1, args+1);
        else if (strcmp(cmd, "delete") == 0) cmd_delete(nargs-1, args+1);
        else if (strcmp(cmd, "observe") == 0) cmd_observe(nargs-1, args+1);
        else if (strcmp(cmd, "demo")     == 0) cmd_demo  ();
        else if (strcmp(cmd, "sidcheck") == 0) check_server_sid_compatibility();
        else
            printf("  Unknown command: '%s'  (type 'help')\n", cmd);
    }

    printf("\nExiting...\n");
    coap_session_release(g_session);
    coap_free_context(g_ctx);
    coap_cleanup();
    return 0;
}

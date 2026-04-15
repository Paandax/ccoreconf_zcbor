/* CORECONF server implementation over CoAP/CoAPS using libcoap. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <coap3/coap.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <signal.h>


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

static size_t stream_wrap_notification_instance(uint8_t *out, size_t out_sz, uint64_t notif_sid, const uint8_t *payload_map, size_t payload_map_len);
static void stream_push_event(const uint8_t *data, size_t len, const char *reason);
static void notify_stream_observers(const char *reason);
static void load_all_sid_files(void);
static void publish_example_port_fault(const char *port_name, const char *port_fault);
static int load_text_from_candidates(const char *const *candidates,
                                     size_t candidate_count,
                                     char **text_out,
                                     const char **chosen_out);
static int parse_sid_after_identifier(const char *text, const char *identifier, uint64_t *sid_out);

static void load_all_sid_files(void) {
    const char *sid_dir = "sid";
    DIR *dir = opendir(sid_dir);
    if (!dir) { perror("opendir sid/"); return; }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".sid")) {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s", sid_dir, entry->d_name);
            char *text = NULL;
            FILE *fp = fopen(path, "rb");
            if (!fp) continue;
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            text = (char *)malloc(sz + 1);
            if (!text) { fclose(fp); continue; }
            fread(text, 1, sz, fp);
            text[sz] = '\0';
            fclose(fp);
            printf("[SID] Loaded file: %s\n", path);
            free(text);
        }
    }
    closedir(dir);
}

static void publish_example_port_fault(const char *port_name, const char *port_fault) {
    uint64_t notif_sid = 60010;
    uint64_t sid_port_name = 1; // Draft example uses SID 1 for port-name
    uint64_t sid_port_fault = 2; // Draft example uses SID 2 for port-fault
    uint8_t buf[256];
    zcbor_state_t enc[4];
    zcbor_new_encode_state(enc, 4, buf, sizeof(buf), 1);
    zcbor_map_start_encode(enc, 2);
    zcbor_uint64_put(enc, sid_port_name);
    zcbor_tstr_put_lit(enc, port_name);
    zcbor_uint64_put(enc, sid_port_fault);
    zcbor_tstr_put_lit(enc, port_fault);
    zcbor_map_end_encode(enc, 2);
    size_t map_len = (size_t)(enc[0].payload - buf);
    uint8_t wrapped[300];
    size_t wrapped_len = stream_wrap_notification_instance(wrapped, sizeof(wrapped), notif_sid, buf, map_len);
    assert(wrapped_len > 0);
    stream_push_event(wrapped, wrapped_len, "example-port-fault");
    notify_stream_observers("example-port-fault");
    printf("[EXAMPLE] example-port-fault event published\n");
}

#define BUFFER_SIZE 4096


#include <coap3/coap.h>

#ifndef COAP_LOG_WARN
#define COAP_LOG_WARN 4
#endif
#ifndef COAP_LOG_ERR
#define COAP_LOG_ERR  3
#endif
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <signal.h>

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


#define CF_YANG_DATA_CBOR   140   
#define CF_YANG_IDENTIFIERS 141   
#define CF_YANG_INSTANCES   142   
#define CF_TEXT_PLAIN       0     

static int quit = 0;


static CoreconfValueT *g_datastore = NULL;
static int             g_ds_exists = 0;
static uint64_t        g_sid_fingerprint = 0;
static uint64_t        g_notification_sid = 0; // Notification SID (example: 60010)
static int load_notification_sid(uint64_t *out) {
    if (!out) return -1;
    const char *candidates[] = {
        "sid/ietf-comi-notification.sid",
        "../sid/ietf-comi-notification.sid",
        "../../sid/ietf-comi-notification.sid",
        "/app/sid/ietf-comi-notification.sid",
    };
    char *text = NULL;
    const char *chosen = NULL;
    if (load_text_from_candidates(candidates, sizeof(candidates)/sizeof(candidates[0]), &text, &chosen) != 0) {
        fprintf(stderr, "[SID] ietf-comi-notification.sid not found in known paths\n");
        return -1;
    }
    int ok = parse_sid_after_identifier(text, "/ietf-comi-notification:notification", out);
    free(text);
    if (ok != 0) {
        fprintf(stderr, "[SID] Error parsing notification SID in %s\n", chosen);
        return -1;
    }
    printf("[SID] Notification loaded %s: notification_sid=%" PRIu64 "\n", chosen, *out);
    return 0;
}
static coap_resource_t *g_stream_res = NULL;

#define STREAM_MAX_EVENTS 16
typedef struct {
    uint8_t *data;
    size_t len;
} StreamEventT;

static StreamEventT g_stream_events[STREAM_MAX_EVENTS];
static size_t       g_stream_event_count = 0;

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

static RuntimeSystemSidsT g_sys_sids = {0};

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
        fprintf(stderr, "[SID] ietf-system.sid not found in known paths\n");
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
        fprintf(stderr, "[SID] Error parsing critical SIDs in %s\n", chosen);
        return -1;
    }

    out->loaded = 1;
    printf("[SID] loaded %s\n", chosen);
    printf("[SID] module=%" PRIu64 " clock=%" PRIu64 " ntp-enabled=%" PRIu64 " ntp-server=%" PRIu64 "\n",
           out->module, out->clock, out->ntp_enabled, out->ntp_server);
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
        fprintf(stderr, "[SID] ietf-interfaces.sid not found in known paths\n");
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
        fprintf(stderr, "[SID] Error parsing critical SIDs in %s\n", chosen);
        return -1;
    }

    out->loaded = 1;
    printf("[SID] loaded %s\n", chosen);
    printf("[SID] module=%" PRIu64 " iface=%" PRIu64 " name=%" PRIu64 " oper-status=%" PRIu64 "\n",
           out->module, out->iface, out->iface_name, out->iface_oper_status);
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
        fprintf(stderr, "[SID] warning: ietf-coreconf-actions.sid not found, using built-in POST SIDs\n");
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
        fprintf(stderr, "[SID] Error parsing POST operation SIDs in %s\n", chosen);
        return -1;
    }

    out->loaded = 1;
    printf("[SID] loaded %s\n", chosen);
    printf("[SID] reboot=%" PRIu64 " reset=%" PRIu64 " reset-out=%" PRIu64 "\n",
           out->reboot_rpc_sid, out->reset_action_sid, out->reset_output_finished_at);
    return 0;
}


static void init_ietf_example_datastore(void) {
    if (!g_sys_sids.loaded || !g_if_sids.loaded) {
        fprintf(stderr, "[SID] SID dictionary not loaded; datastore initialization skipped\n");
        return;
    }

    uint64_t delta_clock_boot    = g_sys_sids.clock_boot - g_sys_sids.clock;
    uint64_t delta_clock_current = g_sys_sids.clock_current - g_sys_sids.clock;
    uint64_t delta_ntp_name      = g_sys_sids.ntp_server_name - g_sys_sids.ntp_server;
    uint64_t delta_ntp_prefer    = g_sys_sids.ntp_server_prefer - g_sys_sids.ntp_server;
    uint64_t delta_ntp_udp       = g_sys_sids.ntp_server_udp - g_sys_sids.ntp_server;
    uint64_t delta_ntp_udp_addr  = g_sys_sids.ntp_server_udp_address - g_sys_sids.ntp_server_udp;
    uint64_t delta_if_desc       = g_if_sids.iface_description - g_if_sids.iface;
    uint64_t delta_if_enabled    = g_if_sids.iface_enabled - g_if_sids.iface;
    uint64_t delta_if_name       = g_if_sids.iface_name - g_if_sids.iface;
    uint64_t delta_if_type       = g_if_sids.iface_type - g_if_sids.iface;
    uint64_t delta_if_oper       = g_if_sids.iface_oper_status - g_if_sids.iface;

    CoreconfValueT *ds = createCoreconfHashmap();

    
    CoreconfValueT *clock_c = createCoreconfHashmap();
    insertCoreconfHashMap(clock_c->data.map_value, delta_clock_boot,
        createCoreconfString("2014-10-05T09:00:00Z"));
    insertCoreconfHashMap(clock_c->data.map_value, delta_clock_current,
        createCoreconfString("2016-10-26T12:16:31Z"));
    insertCoreconfHashMap(ds->data.map_value, g_sys_sids.clock, clock_c);

    
    CoreconfValueT *iface = createCoreconfHashmap();
    insertCoreconfHashMap(iface->data.map_value, delta_if_name,        createCoreconfString("eth0"));
    insertCoreconfHashMap(iface->data.map_value, delta_if_desc,        createCoreconfString("Ethernet adaptor"));
    insertCoreconfHashMap(iface->data.map_value, delta_if_type,        createCoreconfUint64(g_if_sids.identity_ethernet_csmacd));
    insertCoreconfHashMap(iface->data.map_value, delta_if_enabled,     createCoreconfBoolean(true));
    insertCoreconfHashMap(iface->data.map_value, delta_if_oper,        createCoreconfUint64(3));
    CoreconfValueT *ifaces = createCoreconfArray();
    addToCoreconfArray(ifaces, iface);
    free(iface);
    insertCoreconfHashMap(ds->data.map_value, g_if_sids.iface, ifaces);

    
    insertCoreconfHashMap(ds->data.map_value, g_sys_sids.ntp_enabled, createCoreconfBoolean(false));

    
    CoreconfValueT *udp = createCoreconfHashmap();
    insertCoreconfHashMap(udp->data.map_value, delta_ntp_udp_addr, createCoreconfString("128.100.49.105"));
    CoreconfValueT *srv = createCoreconfHashmap();
    insertCoreconfHashMap(srv->data.map_value, delta_ntp_name,   createCoreconfString("tac.nrc.ca"));
    insertCoreconfHashMap(srv->data.map_value, delta_ntp_prefer, createCoreconfBoolean(false));
    insertCoreconfHashMap(srv->data.map_value, delta_ntp_udp,    udp);
    CoreconfValueT *servers = createCoreconfArray();
    addToCoreconfArray(servers, srv);
    free(srv);
    insertCoreconfHashMap(ds->data.map_value, g_sys_sids.ntp_server, servers);

    if (g_datastore) freeCoreconf(g_datastore, true);
    g_datastore = ds;
    g_ds_exists = 1;
     printf("ietf-example datastore loaded: %zu SIDs (clock %" PRIu64 ", ifaces %" PRIu64 ", ntp %" PRIu64 "/%" PRIu64 ")\n\n",
           g_datastore->data.map_value->size,
              g_sys_sids.clock, g_if_sids.iface, g_sys_sids.ntp_enabled, g_sys_sids.ntp_server);

}


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

static void stream_clear_events(void) {
    for (size_t i = 0; i < g_stream_event_count; i++) {
        free(g_stream_events[i].data);
        g_stream_events[i].data = NULL;
        g_stream_events[i].len = 0;
    }
    g_stream_event_count = 0;
}

static size_t cbor_encode_uint(uint8_t *out, size_t out_sz, uint64_t value) {
    if (!out || out_sz == 0) return 0;
    if (value < 24) {
        if (out_sz < 1) return 0;
        out[0] = (uint8_t)value;
        return 1;
    }
    if (value <= 0xFF) {
        if (out_sz < 2) return 0;
        out[0] = 0x18;
        out[1] = (uint8_t)value;
        return 2;
    }
    if (value <= 0xFFFF) {
        if (out_sz < 3) return 0;
        out[0] = 0x19;
        out[1] = (uint8_t)(value >> 8);
        out[2] = (uint8_t)value;
        return 3;
    }
    if (value <= 0xFFFFFFFFULL) {
        if (out_sz < 5) return 0;
        out[0] = 0x1a;
        out[1] = (uint8_t)(value >> 24);
        out[2] = (uint8_t)(value >> 16);
        out[3] = (uint8_t)(value >> 8);
        out[4] = (uint8_t)value;
        return 5;
    }
    if (out_sz < 9) return 0;
    out[0] = 0x1b;
    out[1] = (uint8_t)(value >> 56);
    out[2] = (uint8_t)(value >> 48);
    out[3] = (uint8_t)(value >> 40);
    out[4] = (uint8_t)(value >> 32);
    out[5] = (uint8_t)(value >> 24);
    out[6] = (uint8_t)(value >> 16);
    out[7] = (uint8_t)(value >> 8);
    out[8] = (uint8_t)value;
    return 9;
}

static size_t stream_wrap_notification_instance(uint8_t *out, size_t out_sz,
                                                uint64_t notif_sid,
                                                const uint8_t *payload_map,
                                                size_t payload_map_len) {
    if (!out || out_sz < 2 || !payload_map || payload_map_len == 0) return 0;

    
    out[0] = 0xa1;
    size_t key_len = cbor_encode_uint(out + 1, out_sz - 1, notif_sid);
    if (key_len == 0) return 0;
    if (1 + key_len + payload_map_len > out_sz) return 0;

    memcpy(out + 1 + key_len, payload_map, payload_map_len);
    return 1 + key_len + payload_map_len;
}

static uint64_t stream_extract_first_sid_from_map(const uint8_t *map_data, size_t map_len,
                                                  uint64_t fallback_sid) {
    if (!map_data || map_len == 0) return fallback_sid;

    zcbor_state_t st[8];
    zcbor_new_decode_state(st, 8, map_data, map_len, 1, NULL, 0);
    if (!zcbor_map_start_decode(st)) return fallback_sid;
    if (zcbor_array_at_end(st)) return fallback_sid;

    uint64_t sid = fallback_sid;
    zcbor_major_type_t mt = ZCBOR_MAJOR_TYPE(*st[0].payload);
    if (mt == ZCBOR_MAJOR_TYPE_PINT) {
        if (!zcbor_uint64_decode(st, &sid)) sid = fallback_sid;
    } else if (mt == ZCBOR_MAJOR_TYPE_LIST) {
        if (zcbor_list_start_decode(st) && zcbor_uint64_decode(st, &sid)) {
            
            if (!zcbor_any_skip(st, NULL)) sid = fallback_sid;
            if (!zcbor_list_end_decode(st)) sid = fallback_sid;
        } else {
            sid = fallback_sid;
        }
    }
    return sid;
}

static void stream_push_event(const uint8_t *data, size_t len, const char *reason) {
    if (!data || len == 0) return;

    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) return;
    memcpy(copy, data, len);

    if (g_stream_event_count == STREAM_MAX_EVENTS) {
        free(g_stream_events[STREAM_MAX_EVENTS - 1].data);
        g_stream_event_count--;
    }

    for (size_t i = g_stream_event_count; i > 0; i--) {
        g_stream_events[i] = g_stream_events[i - 1];
    }
    g_stream_events[0].data = copy;
    g_stream_events[0].len = len;
    g_stream_event_count++;

    printf("[OBSERVE] queued event #%zu (%zu bytes, %s)\n",
           g_stream_event_count, len, reason ? reason : "change");
}

static void stream_push_notification_map(const uint8_t *map_data, size_t map_len, const char *reason) {
    if (!map_data || map_len == 0) return;
    uint64_t notif_sid = stream_extract_first_sid_from_map(map_data, map_len, g_sys_sids.module);
    uint8_t wrapped[BUFFER_SIZE];
    size_t wrapped_len = stream_wrap_notification_instance(wrapped, sizeof(wrapped),
                                                           notif_sid,
                                                           map_data, map_len);
    if (wrapped_len > 0) {
        stream_push_event(wrapped, wrapped_len, reason);
    }
}

static void stream_push_notification_sequence(const uint8_t *seq_data, size_t seq_len, const char *reason) {
    if (!seq_data || seq_len == 0) return;

    const uint8_t *ptr = seq_data;
    const uint8_t *end = seq_data + seq_len;
    while (ptr < end) {
        zcbor_state_t st[8];
        zcbor_new_decode_state(st, 8, ptr, (size_t)(end - ptr), 1, NULL, 0);
        if (!zcbor_map_start_decode(st)) break;
        while (!zcbor_array_at_end(st)) {
            if (!zcbor_any_skip(st, NULL)) break;
            if (!zcbor_any_skip(st, NULL)) break;
        }
        if (!zcbor_map_end_decode(st)) break;

        size_t map_len = (size_t)(st[0].payload - ptr);
        if (map_len == 0) break;
        stream_push_notification_map(ptr, map_len, reason);
        ptr = st[0].payload;
    }
}

static int stream_collect_filter_sids(const uint8_t *data, size_t len,
                                      uint64_t *out_sids, size_t out_cap,
                                      size_t *out_count) {
    if (!data || len == 0 || !out_sids || out_cap == 0 || !out_count) return -1;

    size_t count = 0;
    const uint8_t *ptr = data;
    const uint8_t *end = data + len;

    while (ptr < end && count < out_cap) {
        size_t rem = (size_t)(end - ptr);
        zcbor_state_t st[8];
        zcbor_new_decode_state(st, 8, ptr, rem, 1, NULL, 0);

        zcbor_major_type_t mt = ZCBOR_MAJOR_TYPE(*ptr);
        if (mt == ZCBOR_MAJOR_TYPE_PINT) {
            uint64_t sid = 0;
            if (!zcbor_uint64_decode(st, &sid)) break;
            out_sids[count++] = sid;
            ptr = st[0].payload;
            continue;
        }

        if (mt == ZCBOR_MAJOR_TYPE_LIST) {
            if (!zcbor_list_start_decode(st)) break;
            uint64_t sid = 0;
            if (!zcbor_uint64_decode(st, &sid)) break;
            
            if (!zcbor_any_skip(st, NULL)) break;
            if (!zcbor_list_end_decode(st)) break;
            out_sids[count++] = sid;
            ptr = st[0].payload;
            continue;
        }

        break;
    }

    *out_count = count;
    return (count > 0) ? 0 : -1;
}

static int stream_sid_in_filter(uint64_t sid, const uint64_t *filter_sids, size_t filter_count) {
    for (size_t i = 0; i < filter_count; i++) {
        if (filter_sids[i] == sid) return 1;
    }
    return 0;
}

static int stream_event_matches_filter(const uint8_t *data, size_t len,
                                       const uint64_t *filter_sids, size_t filter_count) {
    if (!data || len == 0 || !filter_sids || filter_count == 0) return 0;

    const uint8_t *ptr = data;
    const uint8_t *end = data + len;

    
    while (ptr < end) {
        zcbor_state_t st[16];
        zcbor_new_decode_state(st, 16, ptr, (size_t)(end - ptr), 1, NULL, 0);

        if (!zcbor_map_start_decode(st)) break;

        while (!zcbor_array_at_end(st)) {
            uint64_t sid = 0;
            zcbor_major_type_t mt = ZCBOR_MAJOR_TYPE(*st[0].payload);

            if (mt == ZCBOR_MAJOR_TYPE_PINT) {
                if (!zcbor_uint64_decode(st, &sid)) break;
            } else if (mt == ZCBOR_MAJOR_TYPE_LIST) {
                if (!zcbor_list_start_decode(st)) break;
                if (!zcbor_uint64_decode(st, &sid)) {
                    zcbor_list_end_decode(st);
                    break;
                }
                if (!zcbor_any_skip(st, NULL)) {
                    zcbor_list_end_decode(st);
                    break;
                }
                if (!zcbor_list_end_decode(st)) break;
            } else {
                break;
            }

            if (stream_sid_in_filter(sid, filter_sids, filter_count)) return 1;

            if (!zcbor_any_skip(st, NULL)) break;
        }

        zcbor_map_end_decode(st);
        ptr = st[0].payload;
    }

    return 0;
}

static size_t stream_encode_sequence(uint8_t *out, size_t out_sz, size_t max_items) {
    if (!out || out_sz == 0 || g_stream_event_count == 0) return 0;
    if (max_items == 0 || max_items > g_stream_event_count) max_items = g_stream_event_count;

    size_t used = 0;
    for (size_t i = 0; i < max_items; i++) {
        size_t elen = g_stream_events[i].len;
        if (!g_stream_events[i].data || elen == 0) continue;
        if (used + elen > out_sz) break;
        memcpy(out + used, g_stream_events[i].data, elen);
        used += elen;
    }
    return used;
}

static size_t stream_encode_sequence_filtered(uint8_t *out, size_t out_sz, size_t max_items,
                                             const uint64_t *filter_sids, size_t filter_count) {
    if (!out || out_sz == 0 || g_stream_event_count == 0) return 0;
    if (!filter_sids || filter_count == 0) return stream_encode_sequence(out, out_sz, max_items);
    if (max_items == 0 || max_items > g_stream_event_count) max_items = g_stream_event_count;

    size_t used = 0;
    size_t written = 0;
    for (size_t i = 0; i < g_stream_event_count && written < max_items; i++) {
        if (!stream_event_matches_filter(g_stream_events[i].data, g_stream_events[i].len,
                                         filter_sids, filter_count)) {
            continue;
        }

        size_t elen = g_stream_events[i].len;
        if (!g_stream_events[i].data || elen == 0) continue;
        if (used + elen > out_sz) break;
        memcpy(out + used, g_stream_events[i].data, elen);
        used += elen;
        written++;
    }
    return used;
}

static void notify_stream_observers(const char *reason) {
    if (!g_stream_res) return;
    coap_resource_notify_observers(g_stream_res, NULL);
    printf("[OBSERVE] notified stream /s (%s)\n", reason ? reason : "change");
}


static void handle_sid_info(coap_resource_t *rsrc, coap_session_t *sess,
                            const coap_pdu_t *req, const coap_string_t *query,
                            coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)req; (void)query;
    char body[96];
    int n = snprintf(body, sizeof(body), "sid-fingerprint=%" PRIu64 "\n", g_sid_fingerprint);
    if (n < 0) {
        coap_pdu_set_code(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        return;
    }

    uint8_t cf[4];
    size_t cfl = coap_encode_var_safe(cf, sizeof(cf), CF_TEXT_PLAIN);
    coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data(resp, (size_t)n, (const uint8_t *)body);
}


static void handle_stream_get(coap_resource_t *rsrc, coap_session_t *sess,
                              const coap_pdu_t *req, const coap_string_t *query,
                              coap_pdu_t *resp) {
    (void)rsrc; (void)sess;

    coap_opt_iterator_t oi;
    int has_observe = (coap_check_option(req, COAP_OPTION_OBSERVE, &oi) != NULL);

    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    uint8_t cf_buf[4];
    size_t cfl = coap_encode_var_safe(cf_buf, sizeof(cf_buf), CF_YANG_INSTANCES);
    coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT, cfl, cf_buf);

    uint8_t buf[BUFFER_SIZE];
    size_t len = stream_encode_sequence(buf, BUFFER_SIZE, 8);
    if (len > 0) {
        coap_add_data(resp, len, buf);
        printf("[OBSERVE] GET /s (%s) -> %zu bytes (%zu queued event(s))\n",
               has_observe ? "observe" : "plain", len, g_stream_event_count);
    } else {
        
        printf("[OBSERVE] GET /s (%s) -> empty stream\n", has_observe ? "observe" : "plain");
    }
    (void)query;
}


static void handle_stream_fetch(coap_resource_t *rsrc, coap_session_t *sess,
                                const coap_pdu_t *req, const coap_string_t *query,
                                coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)query;

    size_t qlen = 0;
    const uint8_t *qdata = NULL;
    int has_payload = coap_get_data(req, &qlen, &qdata) && qlen > 0;
    uint64_t filter_sids[64] = {0};
    size_t filter_count = 0;

    if (has_payload) {
        uint16_t cf = get_content_format(req);
        if (cf != CF_YANG_IDENTIFIERS) {
            send_error(resp, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                       1012, "FETCH /s with payload requires CF=141");
            return;
        }
        if (stream_collect_filter_sids(qdata, qlen, filter_sids,
                                       sizeof(filter_sids)/sizeof(filter_sids[0]),
                                       &filter_count) != 0) {
            send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST,
                       1012, "FETCH /s filter payload has no valid IIDs");
            return;
        }
        printf("[OBSERVE] FETCH /s with filter (%zu bytes) -> %zu SID(s) base\n", qlen, filter_count);
    }

    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    uint8_t cf_buf[4];
    size_t cfl = coap_encode_var_safe(cf_buf, sizeof(cf_buf), CF_YANG_INSTANCES);
    coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT, cfl, cf_buf);

    uint8_t buf[BUFFER_SIZE];
    size_t len = stream_encode_sequence_filtered(buf, BUFFER_SIZE, 8, filter_sids, filter_count);
    if (len > 0) {
        coap_add_data(resp, len, buf);
        printf("[OBSERVE] FETCH /s -> %zu bytes (%zu queued event(s), filter=%s)\n",
               len, g_stream_event_count, has_payload ? "yes" : "no");
    } else {
        printf("[OBSERVE] FETCH /s -> empty stream (filter=%s)\n", has_payload ? "yes" : "no");
    }
}

static int format_utc_timestamp(char *out, size_t out_sz, time_t when) {
    struct tm utc_tm;
    if (!out || out_sz == 0) return -1;
    if (!gmtime_r(&when, &utc_tm)) return -1;
    if (strftime(out, out_sz, "%Y-%m-%dT%H:%M:%SZ", &utc_tm) == 0) return -1;
    return 0;
}

static int parse_utc_timestamp(const char *in, time_t *when) {
    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0, second = 0;
    char z = '\0';
    struct tm tm_val;

    if (!in || !when) return -1;

    if (sscanf(in, "%4d-%2d-%2dT%2d:%2d:%2d%c",
               &year, &month, &day, &hour, &minute, &second, &z) != 7 || z != 'Z') {
        return -1;
    }

    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year = year - 1900;
    tm_val.tm_mon = month - 1;
    tm_val.tm_mday = day;
    tm_val.tm_hour = hour;
    tm_val.tm_min = minute;
    tm_val.tm_sec = second;
    tm_val.tm_isdst = 0;

    *when = timegm(&tm_val);
    return 0;
}

static CoreconfValueT *create_coreconf_null(void) {
    CoreconfValueT *v = (CoreconfValueT *)malloc(sizeof(CoreconfValueT));
    if (!v) {
        return NULL;
    }
    v->type = CORECONF_NULL;
    v->data.u64 = 0;
    return v;
}


static void handle_post(coap_resource_t *rsrc, coap_session_t *sess,
                         const coap_pdu_t *req, const coap_string_t *query,
                         coap_pdu_t *resp) {
    (void)rsrc; (void)sess; (void)query;

    uint16_t cf = get_content_format(req);
    if (cf != CF_YANG_INSTANCES) {
        send_error(resp, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                   1012, "POST requires CF=142 (yang-instances+cbor-seq)");
        return;
    }

    size_t len = 0;
    const uint8_t *data = NULL;
    if (!coap_get_data(req, &len, &data) || len == 0) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "missing payload");
        return;
    }
    print_cbor_hex("[POST] payload", data, len);

    PostItem *requests = NULL;
    size_t request_count = 0;
    PostItem *responses = NULL;
    uint8_t out[BUFFER_SIZE];
    size_t out_len = 0;

    if (!parse_post_items(data, len, &requests, &request_count) || request_count == 0) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012,
                   "POST payload must be cbor-seq of single-pair maps");
        return;
    }

    responses = (PostItem *)calloc(request_count, sizeof(PostItem));
    if (!responses) {
        free_post_items(requests, request_count);
        send_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1013,
                   "out of memory");
        return;
    }

    for (size_t i = 0; i < request_count; i++) {
        PostItem *in = &requests[i];
        PostItem *out_item = &responses[i];
        out_item->iid_type = in->iid_type;
        out_item->sid = in->sid;

        if (in->iid_type == POST_IID_SID_STR_KEY) {
            out_item->str_key = strdup(in->str_key ? in->str_key : "");
            if (!out_item->str_key) {
                free_post_items(requests, request_count);
                free_post_items(responses, request_count);
                send_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1013,
                           "out of memory");
                return;
            }
        }

        if (in->iid_type == POST_IID_SID && in->sid == g_ops_sids.reboot_rpc_sid) {
            uint64_t delay = 0;

            if (in->value->type == CORECONF_HASHMAP) {
                CoreconfValueT *delay_value =
                    getCoreconfHashMap(in->value->data.map_value, g_ops_sids.reboot_input_delay);
                if (delay_value) {
                    delay = getCoreconfValueAsUint64(delay_value);
                }
            } else if (in->value->type != CORECONF_NULL) {
                free_post_items(requests, request_count);
                free_post_items(responses, request_count);
                send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012,
                           "reboot RPC input must be map or null");
                return;
            }

            out_item->value = create_coreconf_null();
            if (!out_item->value) {
                free_post_items(requests, request_count);
                free_post_items(responses, request_count);
                send_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1013,
                           "out of memory");
                return;
            }

            printf("[POST] RPC reboot sid=%" PRIu64 " delay=%" PRIu64 "s\n",
                   in->sid, delay);

        } else if (in->iid_type == POST_IID_SID_STR_KEY && in->sid == g_ops_sids.reset_action_sid) {
            const char *reset_at = NULL;
            char reset_finished_at[32];
            time_t reset_base = 0;

            if (in->value->type != CORECONF_HASHMAP) {
                free_post_items(requests, request_count);
                free_post_items(responses, request_count);
                send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012,
                           "reset action input must be a map");
                return;
            }

            CoreconfValueT *reset_at_value =
                getCoreconfHashMap(in->value->data.map_value, g_ops_sids.reset_input_reset_at);
            if (!reset_at_value || reset_at_value->type != CORECONF_STRING) {
                free_post_items(requests, request_count);
                free_post_items(responses, request_count);
                send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012,
                           "reset action requires key 1 as string (reset-at)");
                return;
            }
            reset_at = reset_at_value->data.string_value;

            if (parse_utc_timestamp(reset_at, &reset_base) != 0) {
                reset_base = time(NULL);
            }

            if (format_utc_timestamp(reset_finished_at, sizeof(reset_finished_at), reset_base + 3) != 0) {
                free_post_items(requests, request_count);
                free_post_items(responses, request_count);
                send_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1013,
                           "failed to format reset completion timestamp");
                return;
            }

            out_item->value = createCoreconfHashmap();
            if (!out_item->value) {
                free_post_items(requests, request_count);
                free_post_items(responses, request_count);
                send_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1013,
                           "out of memory");
                return;
            }

            if (insertCoreconfHashMap(out_item->value->data.map_value,
                                      g_ops_sids.reset_output_finished_at,
                                      createCoreconfString(reset_finished_at)) != 0) {
                free_post_items(requests, request_count);
                free_post_items(responses, request_count);
                send_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1013,
                           "failed to build reset action response");
                return;
            }

            printf("[POST] action reset sid=%" PRIu64 " key=%s reset-at=%s\n",
                   in->sid, in->str_key ? in->str_key : "", reset_at);

        } else {
            free_post_items(requests, request_count);
            free_post_items(responses, request_count);
            send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND, 1012,
                       "unsupported RPC/action IID");
            return;
        }
    }

    out_len = create_post_items(out, sizeof(out), responses, request_count);
    if (out_len == 0) {
        free_post_items(requests, request_count);
        free_post_items(responses, request_count);
        send_error(resp, COAP_RESPONSE_CODE_INTERNAL_ERROR, 1013,
                   "failed to encode POST response");
        return;
    }

    free_post_items(requests, request_count);
    free_post_items(responses, request_count);

    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CHANGED);
    uint8_t cf_buf[4];
    size_t cfl = coap_encode_var_safe(cf_buf, sizeof(cf_buf), CF_YANG_INSTANCES);
    coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT, cfl, cf_buf);
    coap_add_data(resp, out_len, out);
    printf("[POST] %zu RPC/action invocation(s) processed -> %zu bytes\n", request_count, out_len);
}




static int fetch_find_list_entry(CoreconfValueT *arr, const char *key_str) {
    if (!arr || arr->type != CORECONF_ARRAY) return -1;
    CoreconfArrayT *a = arr->data.array_value;
    for (size_t i = 0; i < a->size; i++) {
        CoreconfValueT *entry = &a->elements[i];
        if (entry->type != CORECONF_HASHMAP) continue;
        CoreconfHashMapT *m = entry->data.map_value;
        for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
            CoreconfObjectT *obj = m->table[t];
            while (obj) {
                if (obj->value && obj->value->type == CORECONF_STRING &&
                    strcmp(obj->value->data.string_value, key_str) == 0)
                    return (int)i;
                obj = obj->next;
            }
        }
    }
    return -1;
}

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
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "missing IID list");
        return;
    }
    print_cbor_hex("[FETCH] req", data, len);

    if (!g_datastore || !g_ds_exists) {
        send_error(resp, COAP_RESPONSE_CODE_NOT_FOUND,
                   1019, "datastore empty — use PUT /c first");
        return;
    }

    
    typedef enum { IID_SCALAR, IID_LIST } IidKind;
    struct { IidKind kind; uint64_t sid; char key[128]; } iids[64];
    int n = 0;

    const uint8_t *ptr = data;
    const uint8_t *end = data + len;
    while (ptr < end && n < 64) {
        size_t rem = (size_t)(end - ptr);
        zcbor_state_t st[8];
        zcbor_new_decode_state(st, 8, ptr, rem, 1, NULL, 0);

        zcbor_major_type_t mt = ZCBOR_MAJOR_TYPE(*ptr);
        if (mt == ZCBOR_MAJOR_TYPE_PINT) {
            uint64_t sid = 0;
            if (!zcbor_uint64_decode(st, &sid)) break;
            iids[n].kind = IID_SCALAR; iids[n].sid = sid; iids[n].key[0] = '\0';
            n++; ptr = st[0].payload;
        } else if (mt == ZCBOR_MAJOR_TYPE_LIST) {
            if (!zcbor_list_start_decode(st)) break;
            uint64_t sid = 0;
            if (!zcbor_uint64_decode(st, &sid)) break;
            struct zcbor_string zs;
            if (!zcbor_tstr_decode(st, &zs)) break;
            if (!zcbor_list_end_decode(st)) break;
            iids[n].kind = IID_LIST; iids[n].sid = sid;
            size_t kl = zs.len < 127 ? zs.len : 127;
            memcpy(iids[n].key, zs.value, kl); iids[n].key[kl] = '\0';
            n++; ptr = st[0].payload;
        } else {
            break;
        }
    }

    if (n == 0) {
        send_error(resp, COAP_RESPONSE_CODE_BAD_REQUEST, 1012, "no IIDs in request");
        return;
    }
    printf("[FETCH] %d IID(s) requested\n", n);

    
    uint8_t buf[BUFFER_SIZE];
    zcbor_state_t enc[8];
    zcbor_new_encode_state(enc, 8, buf, BUFFER_SIZE, 1);
    zcbor_map_start_encode(enc, (size_t)n);

    for (int i = 0; i < n; i++) {
        uint64_t sid = iids[i].sid;

        if (iids[i].kind == IID_SCALAR) {
            
            CoreconfValueT *val = getCoreconfHashMap(g_datastore->data.map_value, sid);
            if (val) {
                zcbor_uint64_put(enc, sid);
                coreconfToCBOR(val, enc);
                continue;
            }
            
            CoreconfHashMapT *root = g_datastore->data.map_value;
            bool found = false;
            for (size_t t = 0; t < HASHMAP_TABLE_SIZE && !found; t++) {
                CoreconfObjectT *obj = root->table[t];
                while (obj && !found) {
                    if (obj->key <= sid && obj->value &&
                        obj->value->type == CORECONF_HASHMAP) {
                        uint64_t delta = sid - obj->key;
                        CoreconfValueT *leaf = getCoreconfHashMap(
                            obj->value->data.map_value, delta);
                        if (leaf) {
                            zcbor_uint64_put(enc, sid);
                            coreconfToCBOR(leaf, enc);
                            found = true;
                        }
                    }
                    obj = obj->next;
                }
            }
            if (!found) printf("[FETCH] SID %"PRIu64" not found\n", sid);

        } else {
            
            CoreconfValueT *list = getCoreconfHashMap(g_datastore->data.map_value, sid);
            if (list && list->type == CORECONF_ARRAY) {
                int idx = fetch_find_list_entry(list, iids[i].key);
                if (idx >= 0) {
                    CoreconfValueT *entry = &list->data.array_value->elements[idx];
                    zcbor_uint64_put(enc, sid);
                    
                    zcbor_list_start_encode(enc, 1);
                    coreconfToCBOR(entry, enc);
                    zcbor_list_end_encode(enc, 1);
                } else {
                    printf("[FETCH] list entry [%"PRIu64",\"%s\"] not found\n",
                           sid, iids[i].key);
                }
            } else {
                printf("[FETCH] list SID %"PRIu64" not found\n", sid);
            }
        }
    }

    zcbor_map_end_encode(enc, (size_t)n);
    size_t resp_len = (size_t)(enc[0].payload - buf);

    uint8_t cf_buf[4];
    size_t cfl = coap_encode_var_safe(cf_buf, sizeof(cf_buf), CF_YANG_INSTANCES);
    coap_add_option(resp, COAP_OPTION_CONTENT_FORMAT, cfl, cf_buf);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data(resp, resp_len, buf);
    printf("[FETCH] resp %zu bytes (CF=142)\n", resp_len);
}


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
    printf("[iPATCH] %d SIDs updated  (%zu SIDs en datastore)\n",
           n, g_datastore->data.map_value->size);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CHANGED);
    if (n >= 0) {
        
        stream_push_notification_sequence(data, len, "ipatch");
        notify_stream_observers("ipatch");
    }
}


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


    uint64_t SID_PASSWORD = 1735; // Update if your password SID is different

    CoreconfValueT *password_node = getCoreconfHashMap(new_ds->data.map_value, SID_PASSWORD);

    if (password_node != NULL && password_node->type == CORECONF_STRING) {

        const char *new_password = password_node->data.string_value;

        printf("[DTLS SECURE] Password rotation request received.\n");
        printf("[DTLS SECURE] Applying new password in container...\n");

        char command[512];
        snprintf(command, sizeof(command), "/usr/local/bin/rotate_password.sh iotadmin '%s'", new_password);

        int ret = system(command);
        if (ret == 0) {
            printf("[DTLS SECURE] Success: container password updated.\n");
        } else {
            printf("[DTLS SECURE] Error: failed to run password rotation script.\n");
        }
    }


    int was_existing = g_ds_exists;
    if (g_datastore) freeCoreconf(g_datastore, true);
    g_datastore = new_ds;
    g_ds_exists = 1;

    printf("[PUT] %zu SIDs  (%s)\n", g_datastore->data.map_value->size,
           was_existing ? "2.04 Changed" : "2.01 Created");
    coap_pdu_set_code(resp, was_existing ? COAP_RESPONSE_CODE_CHANGED
                                         : COAP_RESPONSE_CODE_CREATED);
    
    stream_push_notification_map(data, len, "put");
    notify_stream_observers("put");
}

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
    printf("[DELETE] datastore deleted (%zu SIDs)\n", old);
    coap_pdu_set_code(resp, COAP_RESPONSE_CODE_DELETED);
    {
        const uint8_t empty_map[] = {0xa0};
        stream_push_notification_map(empty_map, sizeof(empty_map), "delete");
    }
    notify_stream_observers("delete");
}


static void sig_handler(int s) { (void)s; quit = 1; }

int main(void) {
                load_all_sid_files();
            publish_example_port_fault("0/4/21", "Open pin 2");
            publish_example_port_fault("1/4/21", "Open pin 5");
        if (load_notification_sid(&g_notification_sid) == 0) {
            printf("[SID] Using notification_sid=%" PRIu64 " for draft notifications\n", g_notification_sid);
        } else {
            g_notification_sid = 0;
        }
        (void)g_notification_sid; 
    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);

    if (load_runtime_system_sids(&g_sys_sids) != 0) {
        fprintf(stderr, "[SID] FATAL: could not load sid/ietf-system.sid\n");
        return 1;
    }
    if (load_runtime_interfaces_sids(&g_if_sids) != 0) {
        fprintf(stderr, "[SID] FATAL: could not load sid/ietf-interfaces.sid\n");
        return 1;
    }
    if (load_runtime_operation_sids(&g_ops_sids) != 0) {
        fprintf(stderr, "[SID] FATAL: could not load sid/ietf-coreconf-actions.sid\n");
        return 1;
    }
    g_sid_fingerprint = compute_sid_fingerprint(&g_sys_sids, &g_if_sids, &g_ops_sids);
    printf("[SID] fingerprint=%" PRIu64 "\n", g_sid_fingerprint);

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   CORECONF SERVER — draft-ietf-core-comi-20 (libcoap)      ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  PUT    /c  CF=140   -> initialize/replace datastore     ║\n");
    printf("║  FETCH  /c  CF=141   -> read SIDs (list in payload)        ║\n");
    printf("║  GET    /c           -> full datastore                  ║\n");
    printf("║  iPATCH /c  CF=142   -> update nodes (SID in payload)   ║\n");
    printf("║  DELETE /c           -> delete full datastore           ║\n");
    printf("║  POST   /c           -> RPC/actions only             ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Unified datastore - temperature, humidity, etc.              ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    printf("Listening on coap://0.0.0.0:5683/c\n");
    printf("              coap://0.0.0.0:5683/s  (Observe stream)\n");
    printf("              coaps://0.0.0.0:5684/c  (DTLS)\n");
    printf("              coaps://0.0.0.0:5684/s  (DTLS Observe stream)\n\n");
        printf("Stream notification SID: derived from first changed SID (module fallback %" PRIu64 ")\n\n",
            g_sys_sids.module);

    coap_startup();
    coap_set_log_level(COAP_LOG_WARN);

    coap_context_t *ctx = coap_new_context(NULL);
    if (!ctx) { fprintf(stderr, "Error creating context\n"); return 1; }
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);

    
    coap_address_t addr;
    coap_address_init(&addr);
    addr.addr.sin.sin_family      = AF_INET;
    addr.addr.sin.sin_addr.s_addr = INADDR_ANY;
    addr.addr.sin.sin_port        = htons(5683);
    addr.size                     = sizeof(struct sockaddr_in);
    if (!coap_new_endpoint(ctx, &addr, COAP_PROTO_UDP)) {
        fprintf(stderr, "Error creating UDP endpoint 5683\n"); return 1;
    }

    
    const char *tls_mode = getenv("CORECONF_TLS_MODE");
    if (!tls_mode || tls_mode[0] == '\0') tls_mode = "cert";
    if (coap_dtls_is_supported()) {
        if (strcmp(tls_mode, "psk") == 0) {
            const char *psk_id  = getenv("CORECONF_PSK_ID")  ? getenv("CORECONF_PSK_ID")  : "coreconf-client";
            const char *psk_key = getenv("CORECONF_PSK_KEY") ? getenv("CORECONF_PSK_KEY") : "coreconf-secret";
            coap_dtls_spsk_t spsk;
            memset(&spsk, 0, sizeof(spsk));
            spsk.version                = COAP_DTLS_SPSK_SETUP_VERSION;
            spsk.psk_info.hint.s        = (const uint8_t *)psk_id;
            spsk.psk_info.hint.length   = strlen(psk_id);
            spsk.psk_info.key.s         = (const uint8_t *)psk_key;
            spsk.psk_info.key.length    = strlen(psk_key);
            if (!coap_context_set_psk2(ctx, &spsk)) {
                fprintf(stderr, "[DTLS] error configuring PSK\n");
                return 1;
            }
            printf("[DTLS] mode PSK  id=\"%s\"\n", psk_id);
        } else {

            const char *profile = getenv("CORECONF_CERT_PROFILE");
            const char *ca_file, *cert_file, *key_file;
            if (profile && strcmp(profile, "local") == 0) {
                ca_file   = getenv("CORECONF_CA_CERT")     ? getenv("CORECONF_CA_CERT")     : "certs/ca-local.crt";
                cert_file = getenv("CORECONF_SERVER_CERT") ? getenv("CORECONF_SERVER_CERT") : "certs/server-local.crt";
                key_file  = getenv("CORECONF_SERVER_KEY")  ? getenv("CORECONF_SERVER_KEY")  : "certs/server-local.key";
            } else {
                ca_file   = getenv("CORECONF_CA_CERT")     ? getenv("CORECONF_CA_CERT")     : "certs/ca.crt";
                cert_file = getenv("CORECONF_SERVER_CERT") ? getenv("CORECONF_SERVER_CERT") : "certs/server.crt";
                key_file  = getenv("CORECONF_SERVER_KEY")  ? getenv("CORECONF_SERVER_KEY")  : "certs/server.key";
            }

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
            pki.pki_key.key_type         = COAP_PKI_KEY_PEM;
            pki.pki_key.key.pem.ca_file     = ca_file;
            pki.pki_key.key.pem.public_cert = cert_file;
            pki.pki_key.key.pem.private_key = key_file;

            if (!coap_context_set_pki(ctx, &pki)) {
                fprintf(stderr, "[DTLS] error configuring certificates (ca=%s cert=%s key=%s)\n",
                        ca_file, cert_file, key_file);
                return 1;
            }
            printf("[DTLS] mode CERT  ca=%s cert=%s key=%s\n", ca_file, cert_file, key_file);
        }

        coap_address_t dtls_addr;
        coap_address_init(&dtls_addr);
        dtls_addr.addr.sin.sin_family      = AF_INET;
        dtls_addr.addr.sin.sin_addr.s_addr = INADDR_ANY;
        dtls_addr.addr.sin.sin_port        = htons(5684);
        dtls_addr.size                     = sizeof(struct sockaddr_in);
        if (coap_new_endpoint(ctx, &dtls_addr, COAP_PROTO_DTLS))
            printf("[DTLS] coaps://0.0.0.0:5684/c active\n");
        else
            fprintf(stderr, "[DTLS] warning: could not open endpoint 5684\n");
    } else {
        printf("[DTLS] not available in this build (rebuild libcoap with DTLS)\n");
    }

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

    coap_resource_t *stream_res = coap_resource_init(coap_make_str_const("s"),
                                                     COAP_RESOURCE_FLAGS_NOTIFY_CON);
    coap_resource_set_get_observable(stream_res, 1);
    coap_register_handler(stream_res, COAP_REQUEST_GET, handle_stream_get);
    coap_register_handler(stream_res, COAP_REQUEST_FETCH, handle_stream_fetch);
    a = coap_add_attr(stream_res, coap_make_str_const("rt"),
                      coap_make_str_const("\"core.c.es\""), 0); (void)a;
    coap_add_resource(ctx, stream_res);
    g_stream_res = stream_res;

    coap_resource_t *sid_res = coap_resource_init(coap_make_str_const("sid"), 0);
    coap_register_handler(sid_res, COAP_REQUEST_GET, handle_sid_info);
    coap_add_resource(ctx, sid_res);

    init_ietf_example_datastore();

    while (!quit) coap_io_process(ctx, 1000);

    printf("\nShutting down server...\n");
    stream_clear_events();
    if (g_datastore) freeCoreconf(g_datastore, true);
    coap_free_context(ctx);
    coap_cleanup();
    return 0;
}

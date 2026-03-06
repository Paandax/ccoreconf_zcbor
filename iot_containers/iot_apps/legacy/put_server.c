/**
 * put_server.c - Servidor IoT 100% RFC compliant con PUT
 *               (CORECONF RFC draft-ietf-core-comi §4.3)
 *
 * OBJETIVO: Demostrar la diferencia PUT vs iPATCH:
 *   POST   /c  → STORE: carga datos iniciales (CF=60)
 *   GET    /c  → LEE el datastore completo (CF=142)
 *   iPATCH /c  → actualización PARCIAL: solo los SIDs del payload cambian
 *   PUT    /c  → REEMPLAZO TOTAL: el datastore queda exactamente con el payload
 *
 * DIFERENCIA CLAVE PUT vs iPATCH (RFC §4.3 vs §4.5):
 *
 *   Datastore inicial: { SID10:"temperature", SID11:ts, SID20:22.5, SID21:"celsius" }
 *
 *   iPATCH { 20: 99.9 }
 *     Resultado: { SID10:"temperature", SID11:ts, SID20:99.9, SID21:"celsius" }
 *                  SID10/11/21 intactos ──────────────────────────────────────────
 *
 *   PUT { 20: 99.9 }
 *     Resultado: { SID20: 99.9 }
 *                  SID10, SID11, SID21 ELIMINADOS ─────────────────────────────────
 *
 * RFC COMPLIANCE:
 *   §4.3   PUT CF=142 request → 2.04 Changed / 2.01 Created
 *   §4     Block-wise: COAP_BLOCK_USE_LIBCOAP
 *   §5     Discovery: rt="core.c.ds", ds=1029
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
#include "../../include/ipatch.h"
#include "../../include/put.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096

static int quit = 0;

/* ── Base de datos mock ── */
typedef struct {
    CoreconfValueT *data;
    char device_id[64];
    time_t last_update;
    int exists;          /* 0 = nunca almacenado (→ 2.01 Created) */
} DeviceRecord;

#define MAX_DEVICES 10
static DeviceRecord devices[MAX_DEVICES];
static int device_count = 0;

/* ── Helpers ── */

/* Extrae Content-Format de un PDU; retorna 0xFFFF si ausente */
static uint16_t get_content_format(const coap_pdu_t *pdu) {
    coap_opt_iterator_t oi;
    coap_opt_t *opt = coap_check_option(pdu, COAP_OPTION_CONTENT_FORMAT, &oi);
    if (!opt) return 0xFFFF;
    return (uint16_t)coap_decode_var_bytes(coap_opt_value(opt), coap_opt_length(opt));
}

static void print_cbor_hex(const uint8_t *data, size_t len) {
    printf("   ");
    for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i < len - 1) printf("\n   ");
    }
    if (len > 64) printf("... (%zu bytes total)", len);
    printf("\n");
}

/* ── RFC §6: payload de error CBOR ── */
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
                             uint64_t error_tag_sid, const char *message) {
    uint8_t err_buf[512];
    size_t err_len = encode_cbor_error(err_buf, sizeof(err_buf),
                                        error_tag_sid, message);
    coap_pdu_set_code(response, code);
    if (err_len > 0) {
        uint8_t cf_buf[4];
        size_t cf_len = coap_encode_var_safe(cf_buf, sizeof(cf_buf),
                                              COAP_MEDIA_TYPE_YANG_DATA_CBOR);
        coap_add_option(response, COAP_OPTION_CONTENT_FORMAT, cf_len, cf_buf);
        coap_add_data(response, err_len, err_buf);
    }
}

/* ── Query param helper ── */
static char get_query_param(const coap_string_t *query, const char *key) {
    if (!query || !query->s || query->length == 0) return '\0';
    size_t key_len = strlen(key);
    const char *q = (const char *)query->s;
    size_t q_len  = query->length;
    size_t i = 0;
    while (i < q_len) {
        size_t j = i;
        while (j < q_len && q[j] != '&') j++;
        if ((j - i) > key_len + 1 &&
            strncmp(q + i, key, key_len) == 0 &&
            q[i + key_len] == '=') {
            char v = q[i + key_len + 1];
            return (v != '\0' && v != '&') ? v : '?';
        }
        i = j + 1;
    }
    return '\0';
}

typedef struct { CoreconfHashMapT *target_map; } CopyContext;

static void copy_data_sid(CoreconfObjectT *obj, void *udata) {
    CopyContext *ctx = (CopyContext *)udata;
    if (obj->key >= 10 && obj->value) {
        CoreconfValueT *copy = malloc(sizeof(CoreconfValueT));
        memcpy(copy, obj->value, sizeof(CoreconfValueT));
        if (obj->value->type == CORECONF_STRING && obj->value->data.string_value)
            copy->data.string_value = strdup(obj->value->data.string_value);
        insertCoreconfHashMap(ctx->target_map, obj->key, copy);
    }
}

/* ── Buscar/crear slot de dispositivo ── */
static int find_or_create_device(const char *device_id) {
    for (int i = 0; i < device_count; i++)
        if (strcmp(devices[i].device_id, device_id) == 0)
            return i;
    if (device_count >= MAX_DEVICES) return -1;
    strncpy(devices[device_count].device_id, device_id,
            sizeof(devices[device_count].device_id) - 1);
    devices[device_count].data   = NULL;
    devices[device_count].exists = 0;
    return device_count++;
}

/* ============================================================================
 * HANDLER: POST /c → STORE (carga inicial)
 * =========================================================================*/
static void handle_post_store(coap_resource_t *resource,
                               coap_session_t *session,
                               const coap_pdu_t *request,
                               const coap_string_t *query,
                               coap_pdu_t *response) {
    (void)resource; (void)session; (void)query;
    printf("\n📥 POST /c recibido (STORE)\n");

    size_t payload_len;
    const uint8_t *payload_data;
    if (!coap_get_data(request, &payload_len, &payload_data)) {
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_REQUEST,
                        1012, "missing payload");
        return;
    }

    printf("   📦 CBOR (%zu bytes)\n", payload_len);
    print_cbor_hex(payload_data, payload_len);

    zcbor_state_t states[5];
    zcbor_new_decode_state(states, 5, payload_data, payload_len, 1, NULL, 0);
    CoreconfValueT *received = cborToCoreconfValue(states, 0);

    if (!received || received->type != CORECONF_HASHMAP) {
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_REQUEST,
                        1012, "malformed CBOR payload");
        if (received) freeCoreconf(received, true);
        return;
    }

    CoreconfValueT *id_val = getCoreconfHashMap(received->data.map_value, 1);
    const char *device_id = (id_val && id_val->type == CORECONF_STRING)
                             ? id_val->data.string_value : "unknown";

    CoreconfValueT *clean = createCoreconfHashmap();
    CopyContext ctx = { .target_map = clean->data.map_value };
    iterateCoreconfHashMap(received->data.map_value, &ctx, copy_data_sid);
    freeCoreconf(received, true);

    int idx = find_or_create_device(device_id);
    if (idx < 0) {
        send_cbor_error(response, COAP_RESPONSE_CODE_INTERNAL_ERROR,
                        1019, "device limit reached");
        freeCoreconf(clean, true);
        return;
    }

    if (devices[idx].data) freeCoreconf(devices[idx].data, true);
    devices[idx].data        = clean;
    devices[idx].last_update = time(NULL);
    devices[idx].exists      = 1;

    printf("   📊 Dispositivo '%s' almacenado (%zu SIDs)\n",
           device_id, clean->data.map_value->size);
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
    printf("   ✅ STORE → 2.04 Changed\n");
}

/* ============================================================================
 * HANDLER: GET /c → datastore completo (RFC §3.3)
 * =========================================================================*/
static void handle_get_coreconf(coap_resource_t *resource,
                                 coap_session_t *session,
                                 const coap_pdu_t *request,
                                 const coap_string_t *query,
                                 coap_pdu_t *response) {
    printf("\n📥 GET /c recibido\n");

    char c_param = get_query_param(query, "c");
    if (c_param == '\0') c_param = 'a';
    if (c_param != 'a' && c_param != 'c' && c_param != 'n') {
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_OPTION,
                        1023, "unknown value for query param 'c'");
        return;
    }
    char d_param = get_query_param(query, "d");
    if (d_param == '\0') d_param = 't';
    if (d_param != 't' && d_param != 'a') {
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_OPTION,
                        1023, "unknown value for query param 'd'");
        return;
    }

    CoreconfValueT *device_data = (device_count > 0)
                                  ? devices[device_count - 1].data : NULL;
    if (!device_data) {
        send_cbor_error(response, COAP_RESPONSE_CODE_NOT_FOUND,
                        1019, "datastore is empty");
        return;
    }

    printf("   🗂️  Datastore de: %s (%zu SIDs)\n",
           devices[device_count - 1].device_id,
           device_data->data.map_value->size);

    static uint8_t resp_buf[BUFFER_SIZE];
    size_t resp_len = create_get_response(resp_buf, BUFFER_SIZE, device_data);
    if (resp_len == 0) {
        send_cbor_error(response, COAP_RESPONSE_CODE_INTERNAL_ERROR,
                        1019, "failed to serialize datastore");
        return;
    }

    printf("   📦 CBOR (%zu bytes)\n", resp_len);
    print_cbor_hex(resp_buf, resp_len);

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data_large_response(resource, session, request, response,
                                  query, COAP_MEDIA_TYPE_YANG_DATA_CBOR,
                                  -1, 0, resp_len, resp_buf, NULL, NULL);
    printf("   ✅ GET → 2.05 Content (CF=142)\n");
}

/* ============================================================================
 * HANDLER: iPATCH /c → actualización PARCIAL (RFC §4.5)
 * =========================================================================*/
static void handle_ipatch_coreconf(coap_resource_t *resource,
                                    coap_session_t *session,
                                    const coap_pdu_t *request,
                                    const coap_string_t *query,
                                    coap_pdu_t *response) {
    (void)resource; (void)session; (void)query;
    printf("\n📥 iPATCH /c recibido (actualización PARCIAL)\n");

    size_t payload_len;
    const uint8_t *payload_data;
    if (!coap_get_data(request, &payload_len, &payload_data) || payload_len == 0) {
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_REQUEST,
                        1012, "iPATCH requires a payload");
        return;
    }

    printf("   📦 Parche (%zu bytes)\n", payload_len);
    print_cbor_hex(payload_data, payload_len);

    CoreconfValueT *patch = parse_ipatch_request(payload_data, payload_len);
    if (!patch || patch->type != CORECONF_HASHMAP) {
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_REQUEST,
                        1012, "malformed iPATCH payload");
        if (patch) freeCoreconf(patch, true);
        return;
    }

    if (device_count == 0) {
        send_cbor_error(response, COAP_RESPONSE_CODE_NOT_FOUND,
                        1019, "no device in datastore");
        freeCoreconf(patch, true);
        return;
    }

    printf("   🔧 Aplicando parche PARCIAL (%zu SIDs) sobre '%s'\n",
           patch->data.map_value->size,
           devices[device_count - 1].device_id);

    int rc = apply_ipatch(devices[device_count - 1].data, patch);
    freeCoreconf(patch, true);

    if (rc != 0) {
        send_cbor_error(response, COAP_RESPONSE_CODE_INTERNAL_ERROR,
                        1019, "failed to apply iPATCH");
        return;
    }

    devices[device_count - 1].last_update = time(NULL);
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
    printf("   ✅ iPATCH → 2.04 Changed (sin payload)\n");
}

/* ============================================================================
 * HANDLER: PUT /c → REEMPLAZO TOTAL del datastore (RFC §4.3)
 *
 * A diferencia de iPATCH (fusión parcial), PUT SUSTITUYE completamente:
 *   - El datastore anterior se descarta
 *   - El nuevo datastore contiene SOLO los SIDs del payload
 *   - Si el recurso no existía → 2.01 Created
 *   - Si el recurso existía    → 2.04 Changed
 * =========================================================================*/
static void handle_put_coreconf(coap_resource_t *resource,
                                 coap_session_t *session,
                                 const coap_pdu_t *request,
                                 const coap_string_t *query,
                                 coap_pdu_t *response) {
    (void)resource; (void)session; (void)query;
    printf("\n📥 PUT /c recibido (REEMPLAZO TOTAL)\n");

    size_t payload_len;
    const uint8_t *payload_data;
    /* ── RFC §4.3: validar Content-Format = 142 (yang-data+cbor; id=sid) ── */
    uint16_t req_cf = get_content_format(request);
    if (req_cf != COAP_MEDIA_TYPE_YANG_DATA_CBOR) {
        printf("   ❌ CF incorrecto: %u (esperado 142)\n", req_cf);
        send_cbor_error(response, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                        1012, "PUT requires CF=142 (yang-data+cbor; id=sid)");
        return;
    }

    if (!coap_get_data(request, &payload_len, &payload_data) || payload_len == 0) {
        printf("   ❌ Sin payload\n");
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_REQUEST,
                        1012, "PUT requires a payload (CF=142)");
        return;
    }

    printf("   📦 CBOR nuevo datastore (%zu bytes)\n", payload_len);
    print_cbor_hex(payload_data, payload_len);

    /* ── Parsear el nuevo datastore completo (CF=142) ── */
    CoreconfValueT *new_ds = parse_put_request(payload_data, payload_len);
    if (!new_ds || new_ds->type != CORECONF_HASHMAP) {
        printf("   ❌ CBOR inválido\n");
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_REQUEST,
                        1012, "malformed PUT payload (expected CBOR map)");
        if (new_ds) freeCoreconf(new_ds, true);
        return;
    }

    printf("   🔄 Nuevo datastore contiene %zu SIDs\n",
           new_ds->data.map_value->size);

    /* ── Determinar si el recurso existía (para 2.01 vs 2.04) ── */
    int was_existing = (device_count > 0 && devices[device_count - 1].exists);

    if (device_count == 0) {
        /* Primer dispositivo — crear entrada con ID genérico */
        strncpy(devices[0].device_id, "put-device",
                sizeof(devices[0].device_id) - 1);
        devices[0].data   = NULL;
        devices[0].exists = 0;
        device_count      = 1;
    }

    /* ── REEMPLAZAR el datastore COMPLETO ── */
    if (devices[device_count - 1].data) {
        printf("   🗑️  Descartando datastore anterior (%zu SIDs eliminados)\n",
               devices[device_count - 1].data->data.map_value->size);
        freeCoreconf(devices[device_count - 1].data, true);
    }

    devices[device_count - 1].data        = new_ds;
    devices[device_count - 1].last_update = time(NULL);
    devices[device_count - 1].exists      = 1;

    printf("   ✅ Datastore reemplazado con %zu SIDs\n",
           new_ds->data.map_value->size);

    /* RFC §4.3: 2.01 Created si era nuevo, 2.04 Changed si existía */
    if (was_existing) {
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
        printf("   ✅ PUT → 2.04 Changed (recurso existía)\n");
    } else {
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_CREATED);
        printf("   ✅ PUT → 2.01 Created (recurso nuevo)\n");
    }
}

/* ── Señal Ctrl+C ── */
static void handle_sigint(int signum) {
    (void)signum;
    quit = 1;
    printf("\n🛑 Señal recibida, cerrando servidor...\n");
}

/* ============================================================================
 * MAIN
 * =========================================================================*/
int main(void) {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  PUT SERVER - CORECONF PUT/iPATCH/GET sobre CoAP (libcoap) ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    printf("📡 Puerto CoAP UDP: 5683\n");
    printf("   POST /c   → STORE (carga inicial)\n");
    printf("   GET  /c   → LEE datastore (CF=142)\n");
    printf("   iPATCH /c → actualización PARCIAL (CF=141)\n");
    printf("   PUT  /c   → REEMPLAZO TOTAL (CF=142)\n\n");

    signal(SIGINT, handle_sigint);
    coap_startup();

    coap_address_t listen_addr;
    coap_address_init(&listen_addr);
    listen_addr.addr.sin.sin_family      = AF_INET;
    listen_addr.addr.sin.sin_port        = htons(5683);
    listen_addr.addr.sin.sin_addr.s_addr = INADDR_ANY;

    coap_context_t *ctx = coap_new_context(&listen_addr);
    if (!ctx) { fprintf(stderr, "❌ Error creando contexto\n"); return 1; }

    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);

    coap_resource_t *resource = coap_resource_init(coap_make_str_const("c"), 0);
    if (!resource) {
        coap_free_context(ctx);
        fprintf(stderr, "❌ Error creando recurso\n");
        return 1;
    }

    /* RFC §5: discovery */
    coap_add_attr(resource, coap_make_str_const("rt"),
                  coap_make_str_const("\"core.c.ds\""), 0);
    coap_add_attr(resource, coap_make_str_const("ds"),
                  coap_make_str_const("1029"), 0);

    /* Los 4 métodos */
    coap_register_handler(resource, COAP_REQUEST_POST,   handle_post_store);
    coap_register_handler(resource, COAP_REQUEST_GET,    handle_get_coreconf);
    coap_register_handler(resource, COAP_REQUEST_IPATCH, handle_ipatch_coreconf);
    coap_register_handler(resource, COAP_REQUEST_PUT,    handle_put_coreconf);
    coap_add_resource(ctx, resource);

    printf("✅ Escuchando en 0.0.0.0:5683 (UDP)\n");
    printf("✅ Block-wise activado\n");
    printf("✅ Discovery: rt=\"core.c.ds\", ds=1029\n\n");
    printf("⏳ Esperando peticiones (Ctrl+C para salir)...\n\n");

    while (!quit) coap_io_process(ctx, 1000);

    coap_free_context(ctx);
    coap_cleanup();
    printf("✨ Servidor cerrado correctamente\n");
    return 0;
}

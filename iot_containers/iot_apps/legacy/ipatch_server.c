/**
 * ipatch_server.c - Servidor IoT 100% RFC compliant con iPATCH
 *                   (CORECONF RFC draft-ietf-core-comi §4.5)
 *
 * OBJETIVO: Gateway IoT que escucha en CoAP UDP:5683 y soporta:
 *   1. POST /c    → STORE: carga datos iniciales del dispositivo
 *   2. GET /c     → LEE el datastore completo (CF=142)
 *   3. iPATCH /c  → ACTUALIZA PARCIALMENTE el datastore (CF=141)
 *
 * FLUJO iPATCH (RFC §4.5):
 *
 *   [Estado inicial del datastore]
 *     { SID10: "temperature", SID11: timestamp, SID20: 22.5, SID21: "celsius" }
 *
 *   [Cliente actualiza solo SID 20 (valor)]
 *   Cliente                           Servidor
 *     |  CON [iPATCH /c]                 |
 *     |  Content-Format: 141             |
 *     |  Payload: { 20: 99.9 }           |  ← solo el SID a modificar
 *     |─────────────────────────────────>|  → handle_ipatch_coreconf()
 *     |  ACK [2.04 Changed]              |  ← sin payload
 *     |<─────────────────────────────────|
 *
 *   [Datastore después del iPATCH]
 *     { SID10: "temperature", SID11: timestamp, SID20: 99.9, SID21: "celsius" }
 *     └─ SID10, SID11, SID21 → sin cambios  ────────────────────────────────────
 *     └─ SID20 → actualizado a 99.9         ────────────────────────────────────
 *
 * RFC COMPLIANCE:
 *   §3.1.1  Query param 'c': a|c|n   → 4.02 si inválido
 *   §3.1.2  Query param 'd': t|a     → 4.02 si inválido
 *   §4      Block-wise: COAP_BLOCK_USE_LIBCOAP
 *   §4.5    iPATCH CF=141 request, 2.04 Changed response
 *   §5      Discovery: rt="core.c.ds", ds=1029
 *   §6      Errores CBOR CF=142: { 1024: { 4: error_tag, 3: "msg" } }
 *
 * Content-Formats:
 *   CF=141 → application/yang-patch+cbor; id=sid  (iPATCH request)
 *   CF=142 → application/yang-data+cbor; id=sid   (GET response / errores)
 */

#include <coap3/coap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <signal.h>

#include "coreconfTypes.h"
#include "serialization.h"
#include "get.h"
#include "ipatch.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096

static int quit = 0;

/* ── Base de datos mock ── */
typedef struct {
    CoreconfValueT *data;
    char device_id[64];
    time_t last_update;
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

/* ── RFC §6: Payload de error CBOR ── */
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
    pos += msg_len;
    return pos;
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

/* ── RFC §3.1.1 / §3.1.2: query param ── */
static char get_query_param(const coap_string_t *query, const char *key) {
    if (!query || !query->s || query->length == 0) return '\0';
    size_t key_len = strlen(key);
    const char *q   = (const char *)query->s;
    size_t q_len    = query->length;
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

static void store_device_data(const char *device_id, CoreconfValueT *data) {
    CoreconfValueT *to_store = data;

    if (data->type == CORECONF_HASHMAP) {
        CoreconfValueT *clean = createCoreconfHashmap();
        CopyContext ctx = { .target_map = clean->data.map_value };
        iterateCoreconfHashMap(data->data.map_value, &ctx, copy_data_sid);
        printf("   🗂️  Almacenando SIDs (>= 10), size=%zu\n",
               clean->data.map_value->size);
        to_store = clean;
    }

    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].device_id, device_id) == 0) {
            if (devices[i].data) freeCoreconf(devices[i].data, true);
            devices[i].data = to_store;
            devices[i].last_update = time(NULL);
            printf("   📝 Datos de '%s' actualizados\n", device_id);
            return;
        }
    }
    if (device_count < MAX_DEVICES) {
        strncpy(devices[device_count].device_id, device_id,
                sizeof(devices[device_count].device_id) - 1);
        devices[device_count].data = to_store;
        devices[device_count].last_update = time(NULL);
        device_count++;
        printf("   ✨ Nuevo dispositivo '%s' registrado (total: %d)\n",
               device_id, device_count);
    }
}

/* ============================================================================
 * HANDLER: POST /c → STORE
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
        printf("   ❌ Sin payload\n");
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_REQUEST,
                        1012, "missing payload");
        return;
    }

    printf("   📦 CBOR recibido (%zu bytes)\n", payload_len);
    print_cbor_hex(payload_data, payload_len);

    zcbor_state_t states[5];
    zcbor_new_decode_state(states, 5, payload_data, payload_len, 1, NULL, 0);
    CoreconfValueT *received = cborToCoreconfValue(states, 0);

    if (!received || received->type != CORECONF_HASHMAP) {
        printf("   ❌ CBOR inválido\n");
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_REQUEST,
                        1012, "malformed CBOR payload");
        if (received) freeCoreconf(received, true);
        return;
    }

    CoreconfValueT *id_val = getCoreconfHashMap(received->data.map_value, 1);
    const char *device_id = (id_val && id_val->type == CORECONF_STRING)
                             ? id_val->data.string_value : "unknown";

    printf("   📊 Dispositivo: %s\n", device_id);
    store_device_data(device_id, received);

    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
    printf("   ✅ STORE completado → 2.04 Changed\n");
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

    /* RFC §3.1.1: query param 'c' */
    char c_param = get_query_param(query, "c");
    if (c_param == '\0') c_param = 'a';
    if (c_param != 'a' && c_param != 'c' && c_param != 'n') {
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_OPTION,
                        1023, "unknown value for query param 'c'");
        return;
    }

    /* RFC §3.1.2: query param 'd' */
    char d_param = get_query_param(query, "d");
    if (d_param == '\0') d_param = 't';
    if (d_param != 't' && d_param != 'a') {
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_OPTION,
                        1023, "unknown value for query param 'd'");
        return;
    }

    printf("   🔍 c=%c d=%c\n", c_param, d_param);

    CoreconfValueT *device_data = (device_count > 0)
                                  ? devices[device_count - 1].data : NULL;
    if (!device_data) {
        send_cbor_error(response, COAP_RESPONSE_CODE_NOT_FOUND,
                        1019, "datastore is empty");
        return;
    }

    printf("   🗂️  Datastore de: %s\n", devices[device_count - 1].device_id);

    static uint8_t resp_buf[BUFFER_SIZE];
    size_t resp_len = create_get_response(resp_buf, BUFFER_SIZE, device_data);

    if (resp_len == 0) {
        send_cbor_error(response, COAP_RESPONSE_CODE_INTERNAL_ERROR,
                        1019, "failed to serialize datastore");
        return;
    }

    printf("   📦 CBOR (%zu bytes)\n", resp_len);
    print_cbor_hex(resp_buf, resp_len);

    /* RFC §4: block-wise via COAP_BLOCK_USE_LIBCOAP */
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data_large_response(resource, session, request, response,
                                  query, COAP_MEDIA_TYPE_YANG_DATA_CBOR,
                                  -1, 0, resp_len, resp_buf, NULL, NULL);

    printf("   ✅ GET → 2.05 Content (CF=142)\n");
}

/* ============================================================================
 * HANDLER: iPATCH /c → actualización parcial del datastore (RFC §4.5)
 *
 * RFC §4.5:
 *   Request:  iPATCH /c  CF=141  { SID: new_val, ... }
 *   Response: 2.04 Changed  (sin payload si éxito)
 *   Error:    4.xx / 5.xx  CF=142  { 1024: { 4: error_tag, 3: "msg" } }
 *
 * Semántica de actualización PARCIAL:
 *   - Solo se modifican los SIDs presentes en el payload
 *   - Los SIDs ausentes quedan intactos (diferencia con PUT)
 * =========================================================================*/
static void handle_ipatch_coreconf(coap_resource_t *resource,
                                    coap_session_t *session,
                                    const coap_pdu_t *request,
                                    const coap_string_t *query,
                                    coap_pdu_t *response) {
    (void)resource; (void)session; (void)query;

    printf("\n📥 iPATCH /c recibido (actualización parcial)\n");

    /* ── Leer payload ── */
    size_t payload_len;
    const uint8_t *payload_data;

    /* ── RFC §4.5: validar Content-Format = 141 (yang-patch+cbor; id=sid) ── */
    uint16_t req_cf = get_content_format(request);
    if (req_cf != COAP_MEDIA_TYPE_YANG_PATCH_CBOR) {
        printf("   ❌ CF incorrecto: %u (esperado 141)\n", req_cf);
        send_cbor_error(response, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                        1012, "iPATCH requires CF=141 (yang-patch+cbor; id=sid)");
        return;
    }

    if (!coap_get_data(request, &payload_len, &payload_data) || payload_len == 0) {
        printf("   ❌ Sin payload\n");
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_REQUEST,
                        1012, "iPATCH requires a payload (CF=141)");
        return;
    }

    printf("   📦 CBOR parche recibido (%zu bytes)\n", payload_len);
    print_cbor_hex(payload_data, payload_len);

    /* ── RFC §4.5: parsear el parche (CF=141) ── */
    CoreconfValueT *patch = parse_ipatch_request(payload_data, payload_len);

    if (!patch || patch->type != CORECONF_HASHMAP) {
        printf("   ❌ CBOR del parche inválido\n");
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_REQUEST,
                        1012, "malformed iPATCH payload (expected CBOR map)");
        if (patch) freeCoreconf(patch, true);
        return;
    }

    printf("   🔧 Parche contiene %zu SID(s)\n", patch->data.map_value->size);

    /* ── Buscar datastore del dispositivo ── */
    if (device_count == 0) {
        printf("   ⚠️  No hay dispositivos en el datastore\n");
        send_cbor_error(response, COAP_RESPONSE_CODE_NOT_FOUND,
                        1019, "no device in datastore — send POST first");
        freeCoreconf(patch, true);
        return;
    }

    CoreconfValueT *datastore = devices[device_count - 1].data;

    /* ── Aplicar parche (actualización parcial in-place) ── */
    printf("   🛠️  Aplicando parche al datastore de '%s'\n",
           devices[device_count - 1].device_id);

    int rc = apply_ipatch(datastore, patch);
    freeCoreconf(patch, true);  /* ya no necesitamos el parche */

    if (rc != 0) {
        printf("   ❌ Error aplicando el parche\n");
        send_cbor_error(response, COAP_RESPONSE_CODE_INTERNAL_ERROR,
                        1019, "failed to apply iPATCH");
        return;
    }

    devices[device_count - 1].last_update = time(NULL);

    /* ── 2.04 Changed sin payload (RFC §4.5 éxito) ── */
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);
    printf("   ✅ iPATCH completado → 2.04 Changed (sin payload)\n");
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
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║  iPATCH SERVER - CORECONF iPATCH sobre CoAP (libcoap)   ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    printf("📡 Puerto CoAP UDP: 5683\n");
    printf("   Operaciones: POST (STORE) | GET (leer) | iPATCH (actualizar)\n\n");

    signal(SIGINT, handle_sigint);
    coap_startup();

    coap_address_t listen_addr;
    coap_address_init(&listen_addr);
    listen_addr.addr.sin.sin_family      = AF_INET;
    listen_addr.addr.sin.sin_port        = htons(5683);
    listen_addr.addr.sin.sin_addr.s_addr = INADDR_ANY;

    coap_context_t *ctx = coap_new_context(&listen_addr);
    if (!ctx) { fprintf(stderr, "❌ Error creando contexto\n"); return 1; }

    printf("✅ Escuchando en 0.0.0.0:5683 (UDP)\n");

    /* RFC §4: block-wise gestionado por libcoap */
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);
    printf("✅ Block-wise activado (COAP_BLOCK_USE_LIBCOAP)\n");

    coap_resource_t *resource = coap_resource_init(coap_make_str_const("c"), 0);
    if (!resource) {
        fprintf(stderr, "❌ Error creando recurso\n");
        coap_free_context(ctx);
        return 1;
    }

    /* RFC §5: atributos de descubrimiento */
    coap_add_attr(resource, coap_make_str_const("rt"),
                  coap_make_str_const("\"core.c.ds\""), 0);
    coap_add_attr(resource, coap_make_str_const("ds"),
                  coap_make_str_const("1029"), 0);

    /* Registrar los 3 métodos */
    coap_register_handler(resource, COAP_REQUEST_POST,   handle_post_store);
    coap_register_handler(resource, COAP_REQUEST_GET,    handle_get_coreconf);
    coap_register_handler(resource, COAP_REQUEST_IPATCH, handle_ipatch_coreconf);
    coap_add_resource(ctx, resource);

    printf("✅ Recurso /c registrado\n");
    printf("   Métodos: POST (STORE) | GET (CF=142) | iPATCH (CF=141)\n");
    printf("   Discovery: rt=\"core.c.ds\", ds=1029 en /.well-known/core\n\n");
    printf("⏳ Esperando peticiones (Ctrl+C para salir)...\n\n");

    while (!quit) {
        coap_io_process(ctx, 1000);
    }

    printf("\n🧹 Liberando recursos...\n");
    coap_free_context(ctx);
    coap_cleanup();
    printf("✨ Servidor cerrado correctamente\n");
    return 0;
}

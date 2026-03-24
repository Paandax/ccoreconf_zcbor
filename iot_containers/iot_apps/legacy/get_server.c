/**
 * get_server.c - Servidor IoT GET 100% RFC draft-ietf-core-comi compliant
 *
 * OBJETIVO: Gateway IoT que escucha en CoAP UDP:5683 y:
 *   1. Recibe POST /c  → almacena datos del dispositivo (STORE)
 *   2. Recibe GET  /c  → responde con el datastore completo (yang-data+cbor CF=142)
 *
 * COMPILAR (local macOS):
 *   cd iot_containers/iot_apps && make get_server
 *
 * FLUJO CoAP:
 *
 *   [STORE - dispositivo envía datos]
 *   Cliente                           Servidor
 *     |  CON [POST /c]                   |
 *     |  Content-Format: 60 (CBOR)       |
 *     |  Payload: {1:id, 10:tipo, 20:v}  |
 *     |─────────────────────────────────>|  → handle_post_store()
 *     |  ACK [2.04 Changed]              |
 *     |<─────────────────────────────────|
 *
 *   [GET - cliente pide datastore completo]
 *   Cliente                           Servidor
 *     |  CON [GET /c]                    |
 *     |  (sin payload, sin Content-Format)|
 *     |  Uri-Query: c=a  (opcional)       |
 *     |  Uri-Query: d=t  (opcional)       |
 *     |─────────────────────────────────>|  → handle_get_coreconf()
 *     |  ACK [2.05 Content]              |
 *     |  Content-Format: 142             |
 *     |  Payload: { SID: val, ... }      |  CF=142: yang-data+cbor; id=sid
 *     |<─────────────────────────────────|
 *
 * RFC COMPLIANCE:
 *   §3.1.1  Query param 'c': a(all) | c(config) | n(non-config)  →  4.02 si inválido
 *   §3.1.2  Query param 'd': t(trim) | a(all)                    →  4.02 si inválido
 *   §4      Block-wise transfers: COAP_BLOCK_USE_LIBCOAP
 *   §5      Discovery: rt="core.c.ds", ds=1029 en /.well-known/core
 *   §6      Errores con payload CBOR CF=142: { 1024: { 4: error_tag, 3: "msg" } }
 *
 * Content-Formats (RFC draft-ietf-core-comi §2.3):
 *   CF=142 → application/yang-data+cbor; id=sid   (GET response / errors)
 *
 * Funciones CORECONF usadas:
 *   create_get_response()  → serializa el datastore completo → mapa CBOR
 *
 * Error-tag SIDs (RFC §A): operation-failed=1019, invalid-value=1011,
 *   missing-element=1014, unknown-element=1023, malformed-message=1012
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
#include "zcbor_encode.h"
#include "zcbor_decode.h"

#define BUFFER_SIZE 4096

static int quit = 0;

/* ── Base de datos mock (igual que iot_server.c) ── */
typedef struct {
    CoreconfValueT *data;
    char device_id[64];
    time_t last_update;
} DeviceRecord;

#define MAX_DEVICES 10
static DeviceRecord devices[MAX_DEVICES];
static int device_count = 0;

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

/* ============================================================================
 * RFC §6: Payload de error CBOR
 *   Formato: { 1024: { 4: error_tag_sid, 3: "mensaje" } }
 *   Outer key 1024 = error container SID
 *   Inner key 4    = delta de error-tag  (1028 - 1024)
 *   Inner key 3    = delta de error-message (1027 - 1024)
 * =========================================================================*/
static size_t encode_cbor_error(uint8_t *buf, size_t buf_size,
                                uint64_t error_tag_sid, const char *message) {
    size_t msg_len = strlen(message);
    /* worst case: 1+3 + 1 + 1+3 + 1+3 + 1 + 1+1 + msg_len */
    if (buf_size < 16 + msg_len) return 0;
    size_t pos = 0;

    /* outer map(1) */
    buf[pos++] = 0xa1;
    /* key 1024  →  0x19 0x04 0x00 */
    buf[pos++] = 0x19; buf[pos++] = 0x04; buf[pos++] = 0x00;
    /* inner map(2) */
    buf[pos++] = 0xa2;
    /* error-tag delta key = 4 */
    buf[pos++] = 0x04;
    /* error_tag_sid value (uint) */
    if (error_tag_sid < 24) {
        buf[pos++] = (uint8_t)error_tag_sid;
    } else if (error_tag_sid < 256) {
        buf[pos++] = 0x18; buf[pos++] = (uint8_t)error_tag_sid;
    } else {
        buf[pos++] = 0x19;
        buf[pos++] = (uint8_t)(error_tag_sid >> 8);
        buf[pos++] = (uint8_t)(error_tag_sid & 0xFF);
    }
    /* error-message delta key = 3 */
    buf[pos++] = 0x03;
    /* text string */
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

/* ============================================================================
 * RFC §3.1.1 / §3.1.2: Parseo de query params 'c' y 'd'
 *   Retorna: '\0' = no presente (usar default)
 *            Char = primer carácter del valor
 *            '?' = valor presente pero vacío
 * =========================================================================*/
static char get_query_param(const coap_string_t *query, const char *key) {
    if (!query || !query->s || query->length == 0) return '\0';
    size_t key_len = strlen(key);
    const char *q   = (const char *)query->s;
    size_t q_len    = query->length;
    size_t i = 0;
    while (i < q_len) {
        size_t j = i;
        while (j < q_len && q[j] != '&') j++;
        /* segmento q[i .. j-1] */
        if ((j - i) > key_len + 1 &&
            strncmp(q + i, key, key_len) == 0 &&
            q[i + key_len] == '=') {
            char v = q[i + key_len + 1];
            return (v != '\0' && v != '&') ? v : '?';
        }
        i = j + 1;
    }
    return '\0'; /* no encontrado */
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
        printf("   🗂️  Almacenando SIDs (>= 10), size=%zu\n", clean->data.map_value->size);
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
        printf("   ✨ Nuevo dispositivo '%s' registrado (total: %d)\n", device_id, device_count);
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
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        return;
    }

    printf("   📦 CBOR recibido (%zu bytes)\n", payload_len);
    print_cbor_hex(payload_data, payload_len);

    zcbor_state_t states[5];
    zcbor_new_decode_state(states, 5, payload_data, payload_len, 1, NULL, 0);
    CoreconfValueT *received = cborToCoreconfValue(states, 0);

    if (!received || received->type != CORECONF_HASHMAP) {
        printf("   ❌ CBOR inválido\n");
        /* RFC §6: error con payload CBOR (error-tag: malformed-message=1012) */
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
 * HANDLER: GET /c → responder con el datastore completo
 *
 * RFC §3.3: GET /c sin payload → respuesta 2.05 Content CF=142
 *   Payload = un único mapa CBOR { SID: value, SID: value, ... }
 *
 * Se usa create_get_response() de src/get.c para serializar.
 * =========================================================================*/
static void handle_get_coreconf(coap_resource_t *resource,
                                 coap_session_t *session,
                                 const coap_pdu_t *request,
                                 const coap_string_t *query,
                                 coap_pdu_t *response) {
    printf("\n📥 GET /c recibido\n");

    /* ── RFC §3.1.1: query param 'c' (content filter) ──────────────────
     * Valores válidos: 'a' = all (default), 'c' = config, 'n' = non-config
     * Valor desconocido → 4.02 Bad Option con CBOR error (RFC §6)
     * ----------------------------------------------------------------*/
    char c_param = get_query_param(query, "c");
    if (c_param == '\0') c_param = 'a'; /* default: all */
    if (c_param != 'a' && c_param != 'c' && c_param != 'n') {
        printf("   ❌ Query param c='%c' inválido (acepto: a|c|n)\n", c_param);
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_OPTION,
                        1023, "unknown value for query param 'c'");
        return;
    }
    printf("   🔍 Filtro contenido (c=%c): %s\n", c_param,
           c_param == 'a' ? "all" : c_param == 'c' ? "config" : "non-config");

    /* ── RFC §3.1.2: query param 'd' (with-defaults) ───────────────────
     * Valores válidos: 't' = trim (default), 'a' = all
     * Valor desconocido → 4.02 Bad Option con CBOR error (RFC §6)
     * ----------------------------------------------------------------*/
    char d_param = get_query_param(query, "d");
    if (d_param == '\0') d_param = 't'; /* default: trim */
    if (d_param != 't' && d_param != 'a') {
        printf("   ❌ Query param d='%c' inválido (acepto: t|a)\n", d_param);
        send_cbor_error(response, COAP_RESPONSE_CODE_BAD_OPTION,
                        1023, "unknown value for query param 'd'");
        return;
    }
    printf("   🔍 With-defaults   (d=%c): %s\n", d_param,
           d_param == 't' ? "trim" : "all");

    /* ── Seleccionar datos del dispositivo ── */
    CoreconfValueT *device_data = (device_count > 0)
                                  ? devices[device_count - 1].data : NULL;

    if (!device_data) {
        printf("   ⚠️  No hay dispositivos en el datastore\n");
        /* RFC §6: error con payload CBOR (operation-failed=1019) */
        send_cbor_error(response, COAP_RESPONSE_CODE_NOT_FOUND,
                        1019, "datastore is empty");
        return;
    }

    printf("   🗂️  Leyendo datastore de: %s\n", devices[device_count - 1].device_id);

    /* ── Serializar datastore completo con create_get_response() ── */
    static uint8_t resp_buf[BUFFER_SIZE];  /* static para block-wise seguro */
    size_t resp_len = create_get_response(resp_buf, BUFFER_SIZE, device_data);

    if (resp_len == 0) {
        printf("   ❌ Error serializando datastore\n");
        /* RFC §6: error con payload CBOR (operation-failed=1019) */
        send_cbor_error(response, COAP_RESPONSE_CODE_INTERNAL_ERROR,
                        1019, "failed to serialize datastore");
        return;
    }

    printf("   📦 CBOR mapa generado (%zu bytes)\n", resp_len);
    print_cbor_hex(resp_buf, resp_len);

    /* ── RFC §4: Responder con block-wise via libcoap (COAP_BLOCK_USE_LIBCOAP) ── */
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);

    coap_add_data_large_response(resource, session, request, response,
                                  query,
                                  COAP_MEDIA_TYPE_YANG_DATA_CBOR,
                                  -1,   /* max-age: use default */
                                  0,    /* etag */
                                  resp_len, resp_buf,
                                  NULL, NULL);

    printf("   ✅ GET completado → 2.05 Content (CF=142 yang-data+cbor; id=sid, block-wise)\n");
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
    printf("║   GET SERVER - CORECONF GET sobre CoAP (libcoap)         ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    printf("📡 Puerto CoAP UDP: 5683\n\n");

    signal(SIGINT, handle_sigint);
    coap_startup();

    coap_address_t listen_addr;
    coap_address_init(&listen_addr);
    listen_addr.addr.sin.sin_family      = AF_INET;
    listen_addr.addr.sin.sin_port        = htons(5683);
    listen_addr.addr.sin.sin_addr.s_addr = INADDR_ANY;

    coap_context_t *ctx = coap_new_context(&listen_addr);
    if (!ctx) {
        fprintf(stderr, "❌ Error creando contexto\n");
        return 1;
    }
    printf("✅ Escuchando en 0.0.0.0:5683 (UDP)\n");

    /* RFC §4: habilitar block-wise gestionado por libcoap */
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);
    printf("✅ Block-wise activado (COAP_BLOCK_USE_LIBCOAP)\n");

    coap_resource_t *resource = coap_resource_init(coap_make_str_const("c"), 0);
    if (!resource) {
        fprintf(stderr, "❌ Error creando recurso\n");
        coap_free_context(ctx);
        return 1;
    }

    /* RFC §5: atributos de descubrimiento → /.well-known/core
     *   rt="core.c.ds"  tipo de recurso CORECONF datastore
     *   ds=1029         SID del datastore raíz                  */
    coap_add_attr(resource, coap_make_str_const("rt"),
                  coap_make_str_const("\"core.c.ds\""), 0);
    coap_add_attr(resource, coap_make_str_const("ds"),
                  coap_make_str_const("1029"), 0);

    /* Registrar POST (STORE) y GET */
    coap_register_handler(resource, COAP_REQUEST_POST, handle_post_store);
    coap_register_handler(resource, COAP_REQUEST_GET,  handle_get_coreconf);
    coap_add_resource(ctx, resource);

    printf("✅ Recurso /c registrado\n");
    printf("   Métodos: POST (STORE) | GET (datastore completo CF=142)\n");
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

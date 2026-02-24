/**
 * iot_server.c - Servidor IoT Gateway usando CORECONF/CBOR sobre CoAP (libcoap)
 * 
 * OBJETIVO: Gateway IoT que escucha en CoAP UDP:5683 y:
 *   1. Recibe POST /c con CBOR → almacena datos del dispositivo (STORE)
 *   2. Recibe FETCH /c con CBOR sequence → responde con los valores pedidos
 * 
 * COMPILAR:
 *   gcc iot_server.c ccoreconf.a -lcoap-3 -I../include -I../coreconf_zcbor_generated -o iot_server
 * 
 * FLUJO CoAP:
 *
 *   [STORE - dispositivo envía datos]
 *   Cliente                        Servidor (este programa)
 *   -------                        ------------------------
 *     |  CON [POST /c]               |
 *     |  Content-Format: 60 (CBOR)   |
 *     |  Payload: {1:id, 20:valor}   |
 *     |------------------------------>|  → handle_post_store()
 *     |  ACK [2.04 Changed]          |  → store_device_data()
 *     |<------------------------------|
 *
 *   [FETCH - dispositivo pide datos]
 *   Cliente                        Servidor
 *     |  CON [FETCH /c]              |
 *     |  Content-Format: 60 (CBOR)   |
 *     |  Payload: CBOR sequence iids |
 *     |------------------------------>|  → handle_fetch_coreconf()
 *     |  ACK [2.05 Content]          |  → parse_fetch_request_iids()
 *     |  Payload: CBOR sequence vals |  → create_fetch_response_iids()
 *     |<------------------------------|
 *
 * LÓGICA CORECONF (ya implementada):
 *   - store_device_data()             → guarda en base de datos mock
 *   - parse_fetch_request_iids()      → decodifica los SIDs pedidos
 *   - create_fetch_response_iids()    → genera la respuesta CBOR
 *
 * TU TRABAJO: Completar los TODOs de libcoap (handlers + main)
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
#include "../../include/fetch.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096

static int quit = 0;

/*
 * ============================================================================
 * BASE DE DATOS GLOBAL (mock)
 * ============================================================================
 * Almacena los datos de cada dispositivo que hace STORE.
 * En producción sería una base de datos real.
 */

// Base de datos global simulada
typedef struct {
    CoreconfValueT *data;
    char device_id[64];
    time_t last_update;
} DeviceRecord;

#define MAX_DEVICES 10
static DeviceRecord devices[MAX_DEVICES];
static int device_count = 0;

void print_cbor_hex(const uint8_t *data, size_t len) {
    printf("   ");
    for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i < len - 1) printf("\n   ");
    }
    if (len > 64) printf("... (%zu bytes total)", len);
    printf("\n");
}

// Callback para iterar y copiar datos
typedef struct {
    CoreconfHashMapT *target_map;
} CopyContext;

void copy_data_sid(CoreconfObjectT* obj, void* udata) {
    CopyContext *ctx = (CopyContext*)udata;
    uint64_t sid = obj->key;
    
    // Solo copiar SIDs de datos (>= 10)
    if (sid >= 10 && obj->value) {
        // Crear copia del valor
        CoreconfValueT *value_copy = malloc(sizeof(CoreconfValueT));
        memcpy(value_copy, obj->value, sizeof(CoreconfValueT));
        
        // Si es string, copiar el string también
        if (obj->value->type == CORECONF_STRING && obj->value->data.string_value) {
            value_copy->data.string_value = strdup(obj->value->data.string_value);
        }
        
        insertCoreconfHashMap(ctx->target_map, sid, value_copy);
    }
}

// Almacenar datos de dispositivo
void store_device_data(const char *device_id, CoreconfValueT *data) {
    // Si data es un hashmap, extraemos solo los datos relevantes (sin metadata de control)
    CoreconfValueT *data_to_store = data;
    
    if (data->type == CORECONF_HASHMAP) {
        // Crear nuevo hashmap limpio
        CoreconfValueT *clean_hashmap_val = createCoreconfHashmap();
        CoreconfHashMapT *clean_map = clean_hashmap_val->data.map_value;
        CoreconfHashMapT *original_map = data->data.map_value;
        
        // Copiar solo los SIDs de datos (>= 10)
        CopyContext ctx = { .target_map = clean_map };
        iterateCoreconfHashMap(original_map, &ctx, copy_data_sid);
        
        printf("   🗂️  Almacenando SIDs de datos (>= 10), size=%zu\n", clean_map->size);
        
        data_to_store = clean_hashmap_val;
    }
    
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].device_id, device_id) == 0) {
            if (devices[i].data) {
                freeCoreconf(devices[i].data, true);
            }
            devices[i].data = data_to_store;
            devices[i].last_update = time(NULL);
            printf("   📝 Datos de '%s' actualizados\n", device_id);
            return;
        }
    }
    
    if (device_count < MAX_DEVICES) {
        strncpy(devices[device_count].device_id, device_id, sizeof(devices[device_count].device_id) - 1);
        devices[device_count].data = data_to_store;
        devices[device_count].last_update = time(NULL);
        device_count++;
        printf("   ✨ Nuevo dispositivo '%s' registrado (total: %d)\n", device_id, device_count);
    }
}

// Callback para debug: mostrar SIDs
void print_sid_key(CoreconfObjectT* obj, void* udata) {
    (void)udata;
    printf("      - SID %" PRIu64 " presente\n", obj->key);
}

// FETCH: Buscar valor por SID
CoreconfValueT* fetch_by_sid(CoreconfValueT *source, uint64_t sid) {
    if (!source || source->type != CORECONF_HASHMAP) {
        printf("   ⚠️  FETCH: source no es hashmap o es NULL\n");
        return NULL;
    }
    
    CoreconfHashMapT *map = source->data.map_value;
    printf("   🔎 Buscando SID %" PRIu64 " en mapa con size=%zu\n", sid, map->size);
    
    // Listar todos los SIDs
    iterateCoreconfHashMap(map, NULL, print_sid_key);
    
    CoreconfValueT *result = getCoreconfHashMap(map, sid);
    if (result) {
        printf("   ✅ SID %" PRIu64 " encontrado\n", sid);
    } else {
        printf("   ❌ SID %" PRIu64 " NO encontrado\n", sid);
    }
    
    return result;
}

/*
 * ============================================================================
 * HANDLER CoAP: POST /c → STORE
 * ============================================================================
 * Se ejecuta cuando un dispositivo envía sus datos (POST /c con CBOR).
 * Tu trabajo: extraer el payload y configurar la respuesta CoAP.
 * La lógica CORECONF (store_device_data) ya está implementada.
 *
 * PARÁMETROS CoAP:
 *   resource → el recurso /c que recibió la petición
 *   session  → sesión del dispositivo cliente
 *   request  → PDU con el CBOR del dispositivo (payload = datos del sensor)
 *   response → PDU de respuesta (ya creado, solo hay que rellenarlo)
 */
static void handle_post_store(coap_resource_t *resource,
                              coap_session_t *session,
                              const coap_pdu_t *request,
                              const coap_string_t *query,
                              coap_pdu_t *response) {
    (void)resource; (void)session; (void)query;

    printf("\n📥 POST /c recibido (STORE)\n");

    size_t payload_len;
    const uint8_t *payload_data;

    /*
     * ========================================================================
     * TODO 1: Extraer payload CBOR del request
     * ========================================================================
     * FUNCIÓN: coap_get_data(pdu, &longitud, &datos)
     * Retorna: 1 si hay payload, 0 si no
     *
     * Si no hay payload → responder con COAP_RESPONSE_CODE_BAD_REQUEST
     */
    // ESCRIBE AQUÍ:
    if (!coap_get_data(request, &payload_len, &payload_data)) {
        printf("   ❌ No hay payload en el request\n");
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        return;
    }


    printf("   📦 CBOR recibido (%zu bytes)\n", payload_len);
    print_cbor_hex(payload_data, payload_len);

    /* ── Lógica CORECONF (ya implementada, no tocar) ── */
    zcbor_state_t states[5];
    zcbor_new_decode_state(states, 5, payload_data, payload_len, 1, NULL, 0);
    CoreconfValueT *received_data = cborToCoreconfValue(states, 0);

    if (!received_data || received_data->type != CORECONF_HASHMAP) {
        printf("   ❌ CBOR inválido o no es hashmap\n");
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        if (received_data) freeCoreconf(received_data, true);
        return;
    }

    CoreconfHashMapT *msg = received_data->data.map_value;
    CoreconfValueT *device_id_val = getCoreconfHashMap(msg, 1);
    const char *device_id = (device_id_val && device_id_val->type == CORECONF_STRING)
                            ? device_id_val->data.string_value : "unknown";

    printf("   📊 Dispositivo: %s\n", device_id);
    store_device_data(device_id, received_data);
    /* ── Fin lógica CORECONF ── */

    /*
     * ========================================================================
     * TODO 2: Configurar respuesta CoAP
     * ========================================================================
     * Para STORE la respuesta correcta es 2.04 Changed (no 2.05 Content).
     * No lleva payload (solo el código de respuesta).
     *
     * FUNCIÓN: coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED)
     */
    // ESCRIBE AQUÍ:
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CHANGED);


    printf("   ✅ STORE completado → 2.04 Changed\n");
}

/*
 * ============================================================================
 * HANDLER CoAP: FETCH /c → responder con valores pedidos
 * ============================================================================
 * Se ejecuta cuando un dispositivo pide datos con FETCH /c.
 * El payload del request es una CBOR sequence de instance-identifiers.
 * Tu trabajo: extraer payload, llamar a las funciones CORECONF y enviar respuesta.
 *
 * DIFERENCIA con POST:
 *   - POST: el cliente envía datos al servidor
 *   - FETCH: el cliente pide datos específicos (como GET pero con payload)
 */
static void handle_fetch_coreconf(coap_resource_t *resource,
                                  coap_session_t *session,
                                  const coap_pdu_t *request,
                                  const coap_string_t *query,
                                  coap_pdu_t *response) {
    (void)resource; (void)session; (void)query;

    printf("\n📥 FETCH /c recibido\n");

    size_t payload_len;
    const uint8_t *payload_data;

    /*
     * ========================================================================
     * TODO 3: Extraer payload CBOR del request
     * ========================================================================
     * Igual que en el handler POST.
     * Si no hay payload → responder con COAP_RESPONSE_CODE_BAD_REQUEST
     */
    // ESCRIBE AQUÍ:
    if(!coap_get_data(request, &payload_len, &payload_data)) {
        printf("   ❌ No hay payload en el request\n");
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        return;
    }

    printf("   📦 CBOR sequence recibida (%zu bytes)\n", payload_len);
    print_cbor_hex(payload_data, payload_len);

    /* ── Lógica CORECONF (ya implementada, no tocar) ── */
    InstanceIdentifier *fetch_iids = NULL;
    size_t iid_count = 0;

    if (!parse_fetch_request_iids(payload_data, payload_len, &fetch_iids, &iid_count)) {
        printf("   ❌ Error parseando FETCH request\n");
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
        return;
    }

    printf("   📋 Identificadores solicitados (%zu):\n", iid_count);
    for (size_t i = 0; i < iid_count; i++) {
        if (fetch_iids[i].type == IID_SIMPLE)
            printf("      - SID %" PRIu64 "\n", fetch_iids[i].sid);
        else if (fetch_iids[i].type == IID_WITH_STR_KEY)
            printf("      - [%" PRIu64 ", \"%s\"]\n", fetch_iids[i].sid, fetch_iids[i].key.str_key);
        else if (fetch_iids[i].type == IID_WITH_INT_KEY)
            printf("      - [%" PRIu64 ", %" PRId64 "]\n", fetch_iids[i].sid, fetch_iids[i].key.int_key);
    }

    CoreconfValueT *device_data = device_count > 0 ? devices[device_count - 1].data : NULL;
    if (device_data)
        printf("   🗂️  Buscando en: %s\n", devices[device_count - 1].device_id);
    else
        printf("   ⚠️  No hay dispositivos almacenados\n");

    uint8_t response_buffer[BUFFER_SIZE];
    size_t response_len = create_fetch_response_iids(response_buffer, BUFFER_SIZE,
                                                     device_data, fetch_iids, iid_count);
    free_instance_identifiers(fetch_iids, iid_count);

    if (response_len == 0) {
        printf("   ❌ Error generando respuesta CBOR\n");
        coap_pdu_set_code(response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        return;
    }

    printf("   📦 Respuesta CBOR (%zu bytes)\n", response_len);
    print_cbor_hex(response_buffer, response_len);
    /* ── Fin lógica CORECONF ── */

    /*
     * ========================================================================
     * TODO 4: Configurar respuesta CoAP con el CBOR generado
     * ========================================================================
     * PASOS:
     *   4.1 → coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT)
     *
     *   4.2 → Añadir Content-Format: 60 (CBOR)
     *         uint8_t cf_buf[4];
     *         size_t cf_len = coap_encode_var_safe(cf_buf, sizeof(cf_buf), 60);
     *         coap_add_option(response, COAP_OPTION_CONTENT_FORMAT, cf_len, cf_buf);
     *
     *   4.3 → coap_add_data(response, response_len, response_buffer)
     */
    // 4.1 ESCRIBE AQUÍ:
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);

    // 4.2 ESCRIBE AQUÍ:
    uint8_t content_format_buf[4];
    size_t content_format_len = coap_encode_var_safe(content_format_buf, sizeof(content_format_buf), 60);
    coap_add_option(response, COAP_OPTION_CONTENT_FORMAT, content_format_len, content_format_buf);
    // 4.3 ESCRIBE AQUÍ:
    coap_add_data(response, response_len, response_buffer);

    printf("   ✅ FETCH completado → 2.05 Content\n");
}

/*
 * ============================================================================
 * Handler Ctrl+C
 * ============================================================================
 */
static void handle_sigint(int signum) {
    (void)signum;
    quit = 1;
    printf("\n🛑 Señal recibida, cerrando servidor...\n");
}

/*
 * ============================================================================
 * MAIN: Configurar y arrancar servidor CoAP
 * ============================================================================
 */
int main(void) {
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║   IoT GATEWAY - CORECONF/CBOR sobre CoAP (libcoap)       ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");

    const char *device_id_env = getenv("DEVICE_ID");
    printf("🔧 Gateway ID: %s\n", device_id_env ? device_id_env : "gateway-001");
    printf("📡 Puerto CoAP UDP: 5683\n\n");

    signal(SIGINT, handle_sigint);

    /*
     * ========================================================================
     * TODO 5: Inicializar libcoap
     * ========================================================================
     * FUNCIÓN: coap_startup()
     * Siempre lo primero antes de cualquier función de libcoap.
     */
    // ESCRIBE AQUÍ:
    coap_startup();

    /*
     * ========================================================================
     * TODO 6: Configurar dirección de escucha
     * ========================================================================
     * El servidor escucha en todas las interfaces (INADDR_ANY), puerto 5683.
     *
     * PASOS:
     *   coap_address_t listen_addr;
     *   coap_address_init(&listen_addr);
     *   listen_addr.addr.sin.sin_family = AF_INET;
     *   listen_addr.addr.sin.sin_port   = htons(5683);
     *   listen_addr.addr.sin.sin_addr.s_addr = INADDR_ANY;
     */
    // ESCRIBE AQUÍ:
    coap_address_t listen_addr;
    coap_address_init(&listen_addr);
    listen_addr.addr.sin.sin_family = AF_INET;
    listen_addr.addr.sin.sin_port = htons(5683);
    listen_addr.addr.sin.sin_addr.s_addr = INADDR_ANY;

    /*
     * ========================================================================
     * TODO 7: Crear contexto CoAP con la dirección de escucha
     * ========================================================================
     * DIFERENCIA cliente/servidor:
     *   Cliente → coap_new_context(NULL)          (libcoap elige puerto)
     *   Servidor → coap_new_context(&listen_addr) (bind en 5683)
     */
    coap_context_t *ctx = NULL;
    // ESCRIBE AQUÍ:
    ctx = coap_new_context(&listen_addr);

    if (!ctx) {
        fprintf(stderr, "❌ Error creando contexto\n");
        return 1;
    }
    printf("✅ Escuchando en 0.0.0.0:5683 (UDP)\n");

    /*
     * ========================================================================
     * TODO 8: Crear recurso /c
     * ========================================================================
     * FUNCIÓN: coap_resource_init(coap_make_str_const("c"), 0)
     * Este es el endpoint CORECONF estándar (RFC 9254 usa /c)
     */
    coap_resource_t *resource = NULL;
    // ESCRIBE AQUÍ:
    resource = coap_resource_init(coap_make_str_const("c"), 0);

    if (!resource) {
        fprintf(stderr, "❌ Error creando recurso\n");
        coap_free_context(ctx);
        return 1;
    }

    /*
     * ========================================================================
     * TODO 9: Registrar handlers para POST y FETCH
     * ========================================================================
     * FUNCIÓN: coap_register_handler(resource, método, función)
     *
     * MÉTODOS disponibles:
     *   COAP_REQUEST_POST   → STORE (el dispositivo sube datos)
     *   COAP_REQUEST_FETCH  → FETCH (el dispositivo pide datos)
     *
     * Necesitas registrar los dos:
     *   coap_register_handler(resource, COAP_REQUEST_POST,  handle_post_store);
     *   coap_register_handler(resource, COAP_REQUEST_FETCH, handle_fetch_coreconf);
     */
    // ESCRIBE AQUÍ:
    coap_register_handler(resource, COAP_REQUEST_POST, handle_post_store);
    coap_register_handler(resource, COAP_REQUEST_FETCH, handle_fetch_coreconf);

    /*
     * ========================================================================
     * TODO 10: Añadir recurso al contexto
     * ========================================================================
     * FUNCIÓN: coap_add_resource(ctx, resource)
     */
    // ESCRIBE AQUÍ:
    coap_add_resource(ctx, resource);

    printf("✅ Recurso /c registrado\n");
    printf("   Métodos: POST (STORE) | FETCH (CORECONF RFC 9254)\n");
    printf("   Content-Type: application/cbor (60)\n\n");
    printf("⏳ Esperando peticiones (Ctrl+C para salir)...\n\n");

    /*
     * ========================================================================
     * TODO 11: Loop principal del servidor
     * ========================================================================
     * FUNCIÓN: coap_io_process(ctx, timeout_ms)
     *
     * Igual que en el servidor del ejercicio 2:
     * - Espera peticiones en el socket UDP
     * - Cuando llega una, llama al handler correspondiente (POST o FETCH)
     * - El timeout de 1000ms permite salir limpiamente con Ctrl+C
     */
    while (!quit) {
        // ESCRIBE AQUÍ:
        coap_io_process(ctx, 1000);
    }

    /*
     * ========================================================================
     * TODO 12: Limpieza
     * ========================================================================
     * coap_free_context(ctx)  → libera contexto, recursos y sesiones
     * coap_cleanup()          → cleanup global de libcoap
     */
    printf("\n🧹 Liberando recursos...\n");
    // ESCRIBE AQUÍ:
    coap_free_context(ctx);
    coap_cleanup();

    printf("✨ Gateway cerrado correctamente\n");
    return 0;
}

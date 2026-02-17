/**
 * iot_server.c - Servidor IoT Gateway usando CORECONF/CBOR con FETCH
 * 
 * Simula un gateway IoT que:
 * 1. Recibe datos CBOR de dispositivos
 * 2. Decodifica y almacena
 * 3. Responde con operaciones FETCH
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>

#include "../../include/coreconfTypes.h"
#include "../../include/serialization.h"
#include "../../include/fetch.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define PORT 5683
#define BUFFER_SIZE 4096

// Base de datos global simulada
typedef struct {
    CoreconfValueT *data;
    char device_id[64];
    time_t last_update;
} DeviceRecord;

#define MAX_DEVICES 10
DeviceRecord devices[MAX_DEVICES];
int device_count = 0;

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

void handle_client_connection(int client_sock, struct sockaddr_in *client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, INET_ADDRSTRLEN);
    
    printf("\n🔗 Nueva conexión desde %s:%d\n", client_ip, ntohs(client_addr->sin_port));
    
    uint8_t buffer[BUFFER_SIZE];
    int message_count = 0;
    
    // Loop para manejar múltiples mensajes en la misma conexión
    while (1) {
        message_count++;
        printf("\n   ━━━ Mensaje #%d ━━━\n", message_count);
        
        ssize_t bytes_received = recv(client_sock, buffer, BUFFER_SIZE, 0);
        
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("   ℹ️  Cliente cerró la conexión\n");
            } else {
                printf("   ❌ Error recibiendo datos\n");
            }
            break;
        }
        
        printf("   📥 Recibidos %zd bytes\n", bytes_received);
        printf("   📦 CBOR:\n");
        print_cbor_hex(buffer, (size_t)bytes_received);
        
        // Intentar decodificar como mapa CBOR (STORE operation)
        zcbor_state_t states[5];
        zcbor_new_decode_state(states, 5, buffer, (size_t)bytes_received, 1, NULL, 0);
        CoreconfValueT *received_data = cborToCoreconfValue(states, 0);
    
        uint8_t response_buffer[BUFFER_SIZE];
        size_t response_len = 0;
        
        // Verificar si es STORE (HASHMAP) o FETCH (CBOR sequence de SIDs)
        if (received_data && received_data->type == CORECONF_HASHMAP) {
            // ========== OPERACIÓN STORE ==========
            printf("   ✅ CBOR decodificado\n");
            
            CoreconfHashMapT *msg = received_data->data.map_value;
            
            // Obtener device_id (SID 1)
            CoreconfValueT *device_id_val = getCoreconfHashMap(msg, 1);
            const char *device_id = device_id_val && device_id_val->type == CORECONF_STRING 
                                    ? device_id_val->data.string_value : "unknown";
            
            printf("   📊 Dispositivo: %s\n", device_id);
            printf("   🔧 Operación: store\n");
            printf("   💾 Almacenando datos del dispositivo\n");
            store_device_data(device_id, received_data);
            
            // Crear respuesta ACK simple
            CoreconfValueT *ack = createCoreconfHashmap();
            CoreconfHashMapT *ack_map = ack->data.map_value;
            insertCoreconfHashMap(ack_map, 100, createCoreconfString("ok"));
            insertCoreconfHashMap(ack_map, 101, createCoreconfString(device_id));
            insertCoreconfHashMap(ack_map, 102, createCoreconfUint64((uint64_t)time(NULL)));
            
            zcbor_state_t resp_states[5];
            zcbor_new_encode_state(resp_states, 5, response_buffer, BUFFER_SIZE, 0);
            
            if (coreconfToCBOR(ack, resp_states)) {
                response_len = resp_states[0].payload - response_buffer;
            }
            
            freeCoreconf(ack, true);
            freeCoreconf(received_data, true);
            
        } else {
            // ========== OPERACIÓN FETCH (RFC 9254 3.1.3) ==========
            printf("   🔍 Detectado FETCH (CBOR sequence)\n");
            
            // Decodificar CBOR sequence de instance-identifiers usando la librería
            InstanceIdentifier *fetch_iids = NULL;
            size_t iid_count = 0;
            
            if (!parse_fetch_request_iids(buffer, (size_t)bytes_received, &fetch_iids, &iid_count)) {
                printf("   ❌ Error parseando FETCH request\n");
                if (received_data) freeCoreconf(received_data, true);
                continue;
            }
            
            printf("   📋 Identificadores solicitados:\n");
            for (size_t i = 0; i < iid_count; i++) {
                if (fetch_iids[i].type == IID_SIMPLE) {
                    printf("      - SID %" PRIu64 "\n", fetch_iids[i].sid);
                } else if (fetch_iids[i].type == IID_WITH_STR_KEY) {
                    printf("      - [%" PRIu64 ", \"%s\"]\n", fetch_iids[i].sid, fetch_iids[i].key.str_key);
                } else if (fetch_iids[i].type == IID_WITH_INT_KEY) {
                    printf("      - [%" PRIu64 ", %" PRId64 "]\n", fetch_iids[i].sid, fetch_iids[i].key.int_key);
                }
            }
            
            // Buscar datos del último dispositivo almacenado (para esta demo simplificada)
            CoreconfValueT *device_data = device_count > 0 ? devices[device_count - 1].data : NULL;
            
            if (device_data) {
                printf("   🗂️  Buscando en dispositivo: %s\n", devices[device_count - 1].device_id);
            } else {
                printf("   ⚠️  No hay dispositivos almacenados\n");
            }
            
            // Crear respuesta CBOR sequence usando instance-identifiers (RFC 9254 completo)
            response_len = create_fetch_response_iids(response_buffer, BUFFER_SIZE,
                                                      device_data, fetch_iids, iid_count);
            
            free_instance_identifiers(fetch_iids, iid_count);
            if (received_data) freeCoreconf(received_data, true);
        }
        
        // Enviar respuesta
        if (response_len > 0) {
            printf("   📤 Enviando respuesta (%zu bytes)\n", response_len);
            printf("   📦 CBOR:\n");
            print_cbor_hex(response_buffer, response_len);
            
            ssize_t sent = send(client_sock, response_buffer, response_len, 0);
            if (sent > 0) {
                printf("   ✅ Respuesta enviada\n");
            } else {
                printf("   ❌ Error enviando respuesta\n");
                break;
            }
        } else {
            printf("   ❌ Error: sin respuesta para enviar\n");
        }
    }
    
    close(client_sock);
    printf("   🔌 Conexión cerrada (total mensajes: %d)\n", message_count);
}

void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(void) {
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║    IoT GATEWAY - CORECONF/CBOR con FETCH OPERATIONS      ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    
    const char *device_id = getenv("DEVICE_ID");
    printf("🔧 Gateway ID: %s\n", device_id ? device_id : "gateway-001");
    printf("📡 Escuchando en puerto %d\n\n", PORT);
    
    signal(SIGCHLD, sigchld_handler);
    
    // Crear socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("❌ Error creando socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("❌ Error en bind");
        close(server_sock);
        return 1;
    }
    
    if (listen(server_sock, 5) < 0) {
        perror("❌ Error en listen");
        close(server_sock);
        return 1;
    }
    
    printf("✅ Servidor listo. Esperando conexiones...\n\n");
    
    // Accept loop
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock >= 0) {
            pid_t pid = fork();
            if (pid == 0) {
                // Proceso hijo
                close(server_sock);
                handle_client_connection(client_sock, &client_addr);
                exit(0);
            } else {
                // Proceso padre
                close(client_sock);
            }
        }
    }
    
    close(server_sock);
    return 0;
}

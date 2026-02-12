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
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>

#include "../../include/coreconfTypes.h"
#include "../../include/serialization.h"
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
    printf("      - SID %lu presente\n", obj->key);
}

// FETCH: Buscar valor por SID
CoreconfValueT* fetch_by_sid(CoreconfValueT *source, uint64_t sid) {
    if (!source || source->type != CORECONF_HASHMAP) {
        printf("   ⚠️  FETCH: source no es hashmap o es NULL\n");
        return NULL;
    }
    
    CoreconfHashMapT *map = source->data.map_value;
    printf("   🔎 Buscando SID %lu en mapa con size=%zu\n", sid, map->size);
    
    // Listar todos los SIDs
    iterateCoreconfHashMap(map, NULL, print_sid_key);
    
    CoreconfValueT *result = getCoreconfHashMap(map, sid);
    if (result) {
        printf("   ✅ SID %lu encontrado\n", sid);
    } else {
        printf("   ❌ SID %lu NO encontrado\n", sid);
    }
    
    return result;
}

// Crear respuesta FETCH
CoreconfValueT* create_fetch_response(const char *device_id, CoreconfValueT *result, uint64_t sid) {
    CoreconfValueT *response = createCoreconfHashmap();
    CoreconfHashMapT *map = response->data.map_value;
    
    // Status (SID 100)
    insertCoreconfHashMap(map, 100, createCoreconfString("ok"));
    
    // Device ID (SID 101)
    insertCoreconfHashMap(map, 101, createCoreconfString(device_id));
    
    // Timestamp (SID 102)
    insertCoreconfHashMap(map, 102, createCoreconfUint64((uint64_t)time(NULL)));
    
    // Resultado del fetch con el SID ORIGINAL solicitado
    if (result) {
        insertCoreconfHashMap(map, sid, result);
    }
    
    return response;
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
        
        // Decodificar CBOR
        zcbor_state_t states[5];
        zcbor_new_decode_state(states, 5, buffer, (size_t)bytes_received, 1, NULL, 0);
        CoreconfValueT *received_data = cborToCoreconfValue(states, 0);
    
        if (!received_data) {
            printf("   ❌ Error decodificando CBOR\n");
            break;
        }
    
    printf("   ✅ CBOR decodificado\n");
    
    // Procesar mensaje (buscar device_id y operación)
    CoreconfValueT *response = NULL;
    
    if (received_data->type == CORECONF_HASHMAP) {
        CoreconfHashMapT *msg = received_data->data.map_value;
        
        // Obtener device_id (SID 1)
        CoreconfValueT *device_id_val = getCoreconfHashMap(msg, 1);
        const char *device_id = device_id_val && device_id_val->type == CORECONF_STRING 
                                ? device_id_val->data.string_value : "unknown";
        
        printf("   📊 Dispositivo: %s\n", device_id);
        
        // Obtener tipo de operación (SID 2)
        CoreconfValueT *op_val = getCoreconfHashMap(msg, 2);
        const char *operation = op_val && op_val->type == CORECONF_STRING 
                               ? op_val->data.string_value : "store";
        
        printf("   🔧 Operación: %s\n", operation);
        
        if (strcmp(operation, "fetch") == 0) {
            // FETCH: buscar SID específico
            CoreconfValueT *sid_val = getCoreconfHashMap(msg, 3);
            if (sid_val && sid_val->type == CORECONF_UINT_64) {
                uint64_t sid = sid_val->data.u64;
                printf("   🔍 FETCH SID %lu\n", sid);
                
                // Buscar en dispositivo almacenado
                CoreconfValueT *device_data = NULL;
                for (int i = 0; i < device_count; i++) {
                    if (strcmp(devices[i].device_id, device_id) == 0) {
                        device_data = fetch_by_sid(devices[i].data, sid);
                        break;
                    }
                }
                
                if (device_data) {
                    printf("   ✅ FETCH encontró valor para SID %lu\n", sid);
                    response = create_fetch_response(device_id, device_data, sid);
                } else {
                    printf("   ❌ FETCH no encontró SID %lu\n", sid);
                    response = create_fetch_response(device_id, NULL, sid);
                }
            }
        } else {
            // STORE: almacenar datos
            printf("   💾 Almacenando datos del dispositivo\n");
            store_device_data(device_id, received_data);
            response = create_fetch_response(device_id, NULL, 0);
        }
    }
    
        // Enviar respuesta
        if (response) {
            uint8_t response_buffer[BUFFER_SIZE];
            zcbor_state_t resp_states[5];
            zcbor_new_encode_state(resp_states, 5, response_buffer, BUFFER_SIZE, 0);
            
            if (coreconfToCBOR(response, resp_states)) {
                size_t response_len = resp_states[0].payload - response_buffer;
                printf("   📤 Enviando respuesta (%zu bytes)\n", response_len);
                printf("   📦 CBOR:\n");
                print_cbor_hex(response_buffer, response_len);
                
                ssize_t sent = send(client_sock, response_buffer, response_len, 0);
                if (sent > 0) {
                    printf("   ✅ Respuesta enviada\n");
                } else {
                    printf("   ❌ Error enviando respuesta\n");
                    freeCoreconf(response, true);
                    break;
                }
            }
            
            freeCoreconf(response, true);
        }
        
        freeCoreconf(received_data, true);
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

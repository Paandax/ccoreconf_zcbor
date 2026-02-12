/**
 * iot_client.c - Cliente IoT (sensor/actuador) con FETCH operations
 * 
 * Envía datos CBOR al gateway y realiza operaciones FETCH
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "../../include/coreconfTypes.h"
#include "../../include/serialization.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096
#define SEND_INTERVAL 3

void print_cbor_hex(const uint8_t *data, size_t len) {
    printf("   ");
    for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i < len - 1) printf("\n   ");
    }
    if (len > 64) printf("... ");
    printf("\n");
}

// Crear datos de sensor
CoreconfValueT* create_sensor_data(const char *device_type, const char *device_id) {
    CoreconfValueT *data = createCoreconfHashmap();
    CoreconfHashMapT *map = data->data.map_value;
    
    // SID 1: device_id
    insertCoreconfHashMap(map, 1, createCoreconfString(device_id));
    
    // SID 2: operación (store)
    insertCoreconfHashMap(map, 2, createCoreconfString("store"));
    
    // SID 10: device_type
    insertCoreconfHashMap(map, 10, createCoreconfString(device_type));
    
    // SID 11: timestamp
    insertCoreconfHashMap(map, 11, createCoreconfUint64((uint64_t)time(NULL)));
    
    // Datos específicos según tipo
    if (strcmp(device_type, "temperature") == 0) {
        // SID 20: temperatura (15-30°C)
        double temp = 15.0 + (rand() % 1500) / 100.0;
        insertCoreconfHashMap(map, 20, createCoreconfReal(temp));
        
        // SID 21: unidad
        insertCoreconfHashMap(map, 21, createCoreconfString("celsius"));
        
    } else if (strcmp(device_type, "humidity") == 0) {
        // SID 20: humedad (30-90%)
        double hum = 30.0 + (rand() % 6000) / 100.0;
        insertCoreconfHashMap(map, 20, createCoreconfReal(hum));
        
        // SID 21: unidad
        insertCoreconfHashMap(map, 21, createCoreconfString("percent"));
        
    } else if (strcmp(device_type, "actuator") == 0) {
        // SID 20: estado (0-1)
        insertCoreconfHashMap(map, 20, createCoreconfBoolean(rand() % 2));
        
    } else if (strcmp(device_type, "edge") == 0) {
        // SID 20: temperatura virtual
        insertCoreconfHashMap(map, 20, createCoreconfReal(22.5 + (rand() % 300) / 100.0));
        
        // SID 21: humedad virtual
        insertCoreconfHashMap(map, 21, createCoreconfReal(60.0 + (rand() % 2000) / 100.0));
    }
    
    return data;
}

// Crear mensaje de FETCH
CoreconfValueT* create_fetch_message(const char *device_id, uint64_t sid) {
    CoreconfValueT *msg = createCoreconfHashmap();
    CoreconfHashMapT *map = msg->data.map_value;
    
    // SID 1: device_id
    insertCoreconfHashMap(map, 1, createCoreconfString(device_id));
    
    // SID 2: operación (fetch)
    insertCoreconfHashMap(map, 2, createCoreconfString("fetch"));
    
    // SID 3: SID a buscar
    insertCoreconfHashMap(map, 3, createCoreconfUint64(sid));
    
    return msg;
}

int communicate_with_gateway(const char *host, int port, const char *device_type, const char *device_id) {
    // Crear socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("❌ Socket");
        return -1;
    }
    
    // Conectar
    struct sockaddr_in gateway_addr = {0};
    gateway_addr.sin_family = AF_INET;
    gateway_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &gateway_addr.sin_addr) <= 0) {
        printf("❌ Dirección inválida: %s\n", host);
        close(sock);
        return -1;
    }
    
    printf("🔌 Conectando a gateway %s:%d... ", host, port);
    fflush(stdout);
    
    if (connect(sock, (struct sockaddr*)&gateway_addr, sizeof(gateway_addr)) < 0) {
        printf("❌\n");
        perror("   Error");
        close(sock);
        return -1;
    }
    printf("✅\n");
    
    // ========== PASO 1: ENVIAR DATOS ==========
    printf("\n📤 PASO 1: Enviando datos al gateway\n");
    
    CoreconfValueT *sensor_data = create_sensor_data(device_type, device_id);
    
    uint8_t buffer[BUFFER_SIZE];
    zcbor_state_t enc_states[5];
    zcbor_new_encode_state(enc_states, 5, buffer, BUFFER_SIZE, 0);
    
    if (!coreconfToCBOR(sensor_data, enc_states)) {
        printf("❌ Error codificando CBOR\n");
        freeCoreconf(sensor_data, true);
        close(sock);
        return -1;
    }
    
    size_t cbor_len = enc_states[0].payload - buffer;
    printf("   📦 CBOR (%zu bytes):\n", cbor_len);
    print_cbor_hex(buffer, cbor_len);
    
    ssize_t sent = send(sock, buffer, cbor_len, 0);
    if (sent < 0) {
        perror("❌ Send");
        freeCoreconf(sensor_data, true);
        close(sock);
        return -1;
    }
    
    printf("   ✅ Datos enviados (%zd bytes)\n", sent);
    freeCoreconf(sensor_data, true);
    
    // ========== PASO 2: RECIBIR RESPUESTA ==========
    printf("\n📥 PASO 2: Recibiendo respuesta\n");
    
    uint8_t response[BUFFER_SIZE];
    ssize_t received = recv(sock, response, BUFFER_SIZE, 0);
    
    if (received <= 0) {
        printf("❌ No se recibió respuesta\n");
        close(sock);
        return -1;
    }
    
    printf("   ✅ Respuesta recibida (%zd bytes)\n", received);
    printf("   📦 CBOR:\n");
    print_cbor_hex(response, (size_t)received);
    
    // Decodificar respuesta
    zcbor_state_t dec_states[5];
    zcbor_new_decode_state(dec_states, 5, response, (size_t)received, 1, NULL, 0);
    CoreconfValueT *resp_data = cborToCoreconfValue(dec_states, 0);
    
    if (resp_data) {
        printf("   ✅ Respuesta decodificada\n");
        freeCoreconf(resp_data, true);
    }
    
    // ========== PASO 3: FETCH OPERATION ==========
    printf("\n🔍 PASO 3: FETCH - Pidiendo dato específico\n");
    
    // Pedir valor de temperatura (SID 20)
    uint64_t fetch_sid = 20;
    printf("   Solicitando SID %lu...\n", fetch_sid);
    
    CoreconfValueT *fetch_msg = create_fetch_message(device_id, fetch_sid);
    
    zcbor_new_encode_state(enc_states, 5, buffer, BUFFER_SIZE, 0);
    
    if (!coreconfToCBOR(fetch_msg, enc_states)) {
        printf("   ❌ Error codificando FETCH\n");
        freeCoreconf(fetch_msg, true);
        close(sock);
        return -1;
    }
    
    cbor_len = enc_states[0].payload - buffer;
    printf("   📦 Mensaje FETCH (%zu bytes):\n", cbor_len);
    print_cbor_hex(buffer, cbor_len);
    
    sent = send(sock, buffer, cbor_len, 0);
    if (sent > 0) {
        printf("   ✅ FETCH enviado\n");
    } else {
        printf("   ❌ Error enviando FETCH\n");
    }
    
    freeCoreconf(fetch_msg, true);
    
    // Recibir resultado del FETCH
    received = recv(sock, response, BUFFER_SIZE, 0);
    
    if (received > 0) {
        printf("   📥 Resultado FETCH recibido (%zd bytes)\n", received);
        printf("   📦 CBOR:\n");
        print_cbor_hex(response, (size_t)received);
        
        zcbor_new_decode_state(dec_states, 5, response, (size_t)received, 1, NULL, 0);
        CoreconfValueT *fetch_result = cborToCoreconfValue(dec_states, 0);
        
        if (fetch_result && fetch_result->type == CORECONF_HASHMAP) {
            printf("   ✅ Resultado decodificado\n\n");
            
            CoreconfHashMapT *result_map = fetch_result->data.map_value;
            
            // Mostrar el estado de la respuesta
            CoreconfValueT *status = getCoreconfHashMap(result_map, 100);
            if (status && status->type == CORECONF_STRING) {
                printf("   📊 Estado: %s\n", status->data.string_value);
            }
            
            // Buscar y mostrar el valor solicitado (SID 20)
            CoreconfValueT *fetched_value = getCoreconfHashMap(result_map, fetch_sid);
            if (fetched_value) {
                printf("   🎯 Valor recuperado (SID %lu):\n", fetch_sid);
                
                switch (fetched_value->type) {
                    case CORECONF_REAL:
                        printf("      ➜ %.2f", fetched_value->data.real_value);
                        
                        // Buscar unidad (SID 21)
                        CoreconfValueT *unit = getCoreconfHashMap(result_map, 21);
                        if (unit && unit->type == CORECONF_STRING) {
                            printf(" %s", unit->data.string_value);
                        }
                        printf("\n");
                        break;
                        
                    case CORECONF_INT_64:
                        printf("      ➜ %ld\n", fetched_value->data.i64);
                        break;
                        
                    case CORECONF_UINT_64:
                        printf("      ➜ %lu\n", fetched_value->data.u64);
                        break;
                        
                    case CORECONF_STRING:
                        printf("      ➜ \"%s\"\n", fetched_value->data.string_value);
                        break;
                        
                    case CORECONF_TRUE:
                        printf("      ➜ true\n");
                        break;
                        
                    case CORECONF_FALSE:
                        printf("      ➜ false\n");
                        break;
                        
                    default:
                        printf("      ➜ (tipo no reconocido: %d)\n", fetched_value->type);
                        break;
                }
                
                printf("   ✅ FETCH completado exitosamente\n");
            } else {
                printf("   ⚠️  SID %lu no encontrado en respuesta\n", fetch_sid);
            }
            
            freeCoreconf(fetch_result, true);
        } else {
            printf("   ❌ Error: respuesta no válida\n");
            if (fetch_result) freeCoreconf(fetch_result, true);
        }
    } else {
        printf("   ⚠️  No se recibió resultado del FETCH\n");
    }
    
    close(sock);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <tipo_dispositivo>\n", argv[0]);
        fprintf(stderr, "Tipos: temperature, humidity, actuator, edge\n");
        return 1;
    }
    
    const char *device_type = argv[1];
    
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║     IoT CLIENT - CORECONF/CBOR con FETCH OPERATIONS      ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    
    // Configuración desde variables de entorno
    const char *device_id = getenv("DEVICE_ID");
    const char *gateway_host = getenv("GATEWAY_HOST");
    const char *gateway_port_str = getenv("GATEWAY_PORT");
    
    if (!device_id) device_id = "unknown-device";
    if (!gateway_host) gateway_host = "172.20.0.10";
    int gateway_port = gateway_port_str ? atoi(gateway_port_str) : 5683;
    
    printf("🔧 Configuración:\n");
    printf("   Device ID:   %s\n", device_id);
    printf("   Device Type: %s\n", device_type);
    printf("   Gateway:     %s:%d\n\n", gateway_host, gateway_port);
    
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    
    // Realizar ciclos de comunicación
    int count = 0;
    while (count < 2) {  // 2 ciclos
        count++;
        printf("\n╔═══════════════════════════════════════════════════════════╗\n");
        printf("║ CICLO %d - %s", count, ctime(&(time_t){time(NULL)}));
        printf("╚═══════════════════════════════════════════════════════════╝\n");
        
        if (communicate_with_gateway(gateway_host, gateway_port, device_type, device_id) == 0) {
            printf("\n✅ Ciclo completado exitosamente\n");
        } else {
            printf("\n❌ Error en el ciclo\n");
        }
        
        if (count < 2) {
            printf("\n⏳ Esperando %d segundos antes de siguiente ciclo...\n", SEND_INTERVAL);
            sleep(SEND_INTERVAL);
        }
    }
    
    printf("\n✅ Cliente terminado\n");
    return 0;
}

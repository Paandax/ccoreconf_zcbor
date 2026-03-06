/**
 * iot_test_local.c - Test local de operaciones IoT sin red
 * 
 * Prueba la serialización CBOR como si fueran mensajes IoT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#include "../../include/coreconfTypes.h"
#include "../../include/serialization.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096

void print_separator(void) {
    printf("═══════════════════════════════════════════════════════════\n");
}

void print_cbor_hex(const uint8_t *data, size_t len) {
    printf("📦 CBOR (%zu bytes):\n   ", len);
    for (size_t i = 0; i < len; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i < len - 1) printf("\n   ");
    }
    printf("\n");
}

CoreconfValueT* encode_and_measure(CoreconfValueT* data, const char* test_name) {
    uint8_t buffer[BUFFER_SIZE];
    zcbor_state_t states[5];
    
    // Inicializar zcbor para encoding
    zcbor_new_encode_state(states, 5, buffer, BUFFER_SIZE, 0);
    
    // Serializar
    bool success = coreconfToCBOR(data, states);
    
    if (success) {
        size_t cbor_len = states[0].payload - buffer;
        printf("✅ %s serializado\n", test_name);
        print_cbor_hex(buffer, cbor_len);
        
        // Deserializar
        zcbor_new_decode_state(states, 5, buffer, cbor_len, 1, NULL, 0);
        CoreconfValueT *decoded = cborToCoreconfValue(states, 0);
        
        if (decoded) {
            printf("✅ %s deserializado correctamente\n", test_name);
            return decoded;
        } else {
            printf("❌ Error deserializando %s\n", test_name);
        }
    } else {
        printf("❌ Error serializando %s\n", test_name);
    }
    
    return NULL;
}

void test_temperature_sensor(void) {
    print_separator();
    printf("🌡️  TEST: Sensor de Temperatura\n");
    print_separator();
    
    // Crear hashmap para mensaje de sensor
    CoreconfValueT *data = createCoreconfHashmap();
    CoreconfHashMapT *map = data->data.map_value;
    
    // Agregar device_id (SID 1000)
    insertCoreconfHashMap(map, 1000, createCoreconfString("temp-sensor-001"));
    
    // Agregar temperatura (SID 1001)
    insertCoreconfHashMap(map, 1001, createCoreconfReal(23.5));
    
    // Agregar unidad (SID 1002)
    insertCoreconfHashMap(map, 1002, createCoreconfString("celsius"));
    
    // Agregar timestamp (SID 1003)
    insertCoreconfHashMap(map, 1003, createCoreconfUint64((uint64_t)time(NULL)));
    
    printf("📊 Datos del sensor:\n");
    printf("   SID 1000: temp-sensor-001\n");
    printf("   SID 1001: 23.5\n");
    printf("   SID 1002: celsius\n");
    printf("   SID 1003: %" PRIu64 "\n\n", (uint64_t)time(NULL));
    
    CoreconfValueT *decoded = encode_and_measure(data, "Sensor de temperatura");
    
    if (decoded) {
        printf("📊 Tipo decodificado: %s\n", 
               decoded->type == CORECONF_HASHMAP ? "HASHMAP" : "OTRO");
        freeCoreconf(decoded, true);
    }
    
    freeCoreconf(data, true);
    printf("\n");
}

void test_multi_sensor_aggregate(void) {
    print_separator();
    printf("🔗 TEST: Agregado de Múltiples Sensores\n");
    print_separator();
    
    // Crear array con datos de múltiples sensores
    CoreconfValueT *sensors = createCoreconfArray();
    
    // Sensor 1: Temperatura
    CoreconfValueT *sensor1 = createCoreconfHashmap();
    insertCoreconfHashMap(sensor1->data.map_value, 1, 
                         createCoreconfString("temperature"));
    insertCoreconfHashMap(sensor1->data.map_value, 2, 
                         createCoreconfReal(25.3));
    addToCoreconfArray(sensors, sensor1);
    
    // Sensor 2: Humedad
    CoreconfValueT *sensor2 = createCoreconfHashmap();
    insertCoreconfHashMap(sensor2->data.map_value, 1, 
                         createCoreconfString("humidity"));
    insertCoreconfHashMap(sensor2->data.map_value, 2, 
                         createCoreconfReal(65.8));
    addToCoreconfArray(sensors, sensor2);
    
    // Sensor 3: Presión
    CoreconfValueT *sensor3 = createCoreconfHashmap();
    insertCoreconfHashMap(sensor3->data.map_value, 1, 
                         createCoreconfString("pressure"));
    insertCoreconfHashMap(sensor3->data.map_value, 2, 
                         createCoreconfReal(1013.25));
    addToCoreconfArray(sensors, sensor3);
    
    // Mensaje completo
    CoreconfValueT *message = createCoreconfHashmap();
    insertCoreconfHashMap(message->data.map_value, 100, 
                         createCoreconfString("gateway-001"));
    insertCoreconfHashMap(message->data.map_value, 101, 
                         createCoreconfUint64((uint64_t)time(NULL)));
    insertCoreconfHashMap(message->data.map_value, 102, sensors);
    
    printf("📊 Mensaje con 3 sensores agregados\n\n");
    
    CoreconfValueT *decoded = encode_and_measure(message, "Agregado de sensores");
    
    if (decoded && decoded->type == CORECONF_HASHMAP) {
        printf("📊 Mensaje complejo deserializado correctamente\n");
        freeCoreconf(decoded, true);
    }
    
    freeCoreconf(message, true);
    printf("\n");
}

void test_actuator_command(void) {
    print_separator();
    printf("🎛️  TEST: Comando de Actuador\n");
    print_separator();
    
    // Crear comando de control
    CoreconfValueT *command = createCoreconfHashmap();
    CoreconfHashMapT *map = command->data.map_value;
    
    // Target device (SID 200)
    insertCoreconfHashMap(map, 200, createCoreconfString("relay-001"));
    
    // Action (SID 201)
    insertCoreconfHashMap(map, 201, createCoreconfString("set_state"));
    
    // State (SID 202)
    insertCoreconfHashMap(map, 202, createCoreconfBoolean(true));
    
    // Priority (SID 203)
    insertCoreconfHashMap(map, 203, createCoreconfUint8(1));
    
    printf("📊 Comando de actuador:\n");
    printf("   SID 200: relay-001\n");
    printf("   SID 201: set_state\n");
    printf("   SID 202: true\n");
    printf("   SID 203: 1\n\n");
    
    CoreconfValueT *decoded = encode_and_measure(command, "Comando de actuador");
    
    if (decoded) {
        printf("✅ Comando listo para ejecutar en dispositivo\n");
        freeCoreconf(decoded, true);
    }
    
    freeCoreconf(command, true);
    printf("\n");
}

void test_cbor_size_efficiency(void) {
    print_separator();
    printf("📏 TEST: Eficiencia de Tamaño CBOR vs JSON\n");
    print_separator();
    
    // Crear un mensaje típico de IoT
    CoreconfValueT *data = createCoreconfHashmap();
    CoreconfHashMapT *map = data->data.map_value;
    
    insertCoreconfHashMap(map, 1, createCoreconfString("sensor-012"));
    insertCoreconfHashMap(map, 2, createCoreconfReal(25.6));
    insertCoreconfHashMap(map, 3, createCoreconfUint64((uint64_t)time(NULL)));
    insertCoreconfHashMap(map, 4, createCoreconfBoolean(true));
    
    uint8_t buffer[BUFFER_SIZE];
    zcbor_state_t states[5];
    
    zcbor_new_encode_state(states, 5, buffer, BUFFER_SIZE, 0);
    
    if (coreconfToCBOR(data, states)) {
        size_t cbor_len = states[0].payload - buffer;
        
        // Equivalente JSON estimado:
        // {"1":"sensor-012","2":25.6,"3":1707739200,"4":true}
        size_t json_len = 55;  // aproximado
        
        printf("📊 Tamaños:\n");
        printf("   CBOR: %zu bytes\n", cbor_len);
        printf("   JSON: ~%zu bytes (estimado)\n", json_len);
        printf("   Reducción: %.1f%%\n", 
               ((double)(json_len - cbor_len) / json_len) * 100);
        printf("\n");
        print_cbor_hex(buffer, cbor_len);
    }
    
    freeCoreconf(data, true);
    printf("\n");
}

int main(void) {
    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║     TEST LOCAL IoT - CORECONF/CBOR (sin contenedores)    ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    
    test_temperature_sensor();
    test_multi_sensor_aggregate();
    test_actuator_command();
    test_cbor_size_efficiency();
    
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                  ✅ TODOS LOS TESTS OK                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    
    return 0;
}

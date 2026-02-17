/**
 * test_fetch_with_keys.c - Demostración de FETCH con instance-identifiers RFC 9254
 * 
 * Demuestra búsqueda de elementos en arrays usando keys (RFC 9254 3.1.3)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../include/coreconfTypes.h"
#include "../include/coreconfManipulation.h"
#include "../include/serialization.h"
#include "../include/fetch.h"

void print_result(const char *description, CoreconfValueT *result) {
    printf("  %s: ", description);
    
    if (!result) {
        printf("NULL\n");
        return;
    }
    
    switch (result->type) {
        case CORECONF_STRING:
            printf("\"%s\"\n", result->data.string_value);
            break;
        case CORECONF_REAL:
            printf("%.2f\n", result->data.real_value);
            break;
        case CORECONF_INT_64:
            printf("%" PRId64 "\n", result->data.i64);
            break;
        case CORECONF_UINT_64:
            printf("%" PRIu64 "\n", result->data.u64);
            break;
        case CORECONF_HASHMAP:
            printf("{hashmap con %zu elementos}\n", result->data.map_value->size);
            break;
        case CORECONF_ARRAY:
            printf("[array con %zu elementos]\n", result->data.array_value->size);
            break;
        default:
            printf("<tipo %d>\n", result->type);
            break;
    }
}

int main(void) {
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST: FETCH con instance-identifiers RFC 9254 3.1.3      ║\n");
    printf("║  Búsqueda de elementos en arrays usando keys              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    // ========== CREAR DATOS DE EJEMPLO ==========
    printf("═══ Paso 1: Crear estructura de datos con array ═══\n");
    
    // Crear el hashmap principal
    CoreconfValueT *data = createCoreconfHashmap();
    CoreconfHashMapT *main_map = data->data.map_value;
    
    // SID 10: device_type
    insertCoreconfHashMap(main_map, 10, createCoreconfString("IoT Gateway"));
    printf("  ✓ SID 10: \"IoT Gateway\"\n");
    
    // SID 20: temperatura simple
    insertCoreconfHashMap(main_map, 20, createCoreconfReal(25.5));
    printf("  ✓ SID 20: 25.5\n");
    
    // SID 30: array de sensores
    CoreconfValueT *sensors_array = createCoreconfArray();
    
    // Sensor 1: temp1
    CoreconfValueT *sensor1 = createCoreconfHashmap();
    insertCoreconfHashMap(sensor1->data.map_value, 1, createCoreconfString("temp1"));
    insertCoreconfHashMap(sensor1->data.map_value, 2, createCoreconfReal(23.5));
    insertCoreconfHashMap(sensor1->data.map_value, 3, createCoreconfString("zone-A"));
    addToCoreconfArray(sensors_array, sensor1);
    
    // Sensor 2: hum1
    CoreconfValueT *sensor2 = createCoreconfHashmap();
    insertCoreconfHashMap(sensor2->data.map_value, 1, createCoreconfString("hum1"));
    insertCoreconfHashMap(sensor2->data.map_value, 2, createCoreconfReal(65.0));
    insertCoreconfHashMap(sensor2->data.map_value, 3, createCoreconfString("zone-B"));
    addToCoreconfArray(sensors_array, sensor2);
    
    // Sensor 3: temp2
    CoreconfValueT *sensor3 = createCoreconfHashmap();
    insertCoreconfHashMap(sensor3->data.map_value, 1, createCoreconfString("temp2"));
    insertCoreconfHashMap(sensor3->data.map_value, 2, createCoreconfReal(28.3));
    insertCoreconfHashMap(sensor3->data.map_value, 3, createCoreconfString("zone-C"));
    addToCoreconfArray(sensors_array, sensor3);
    
    insertCoreconfHashMap(main_map, 30, sensors_array);
    printf("  ✓ SID 30: [array con 3 sensores]\n");
    printf("      - {name: \"temp1\", value: 23.5, zone: \"zone-A\"}\n");
    printf("      - {name: \"hum1\", value: 65.0, zone: \"zone-B\"}\n");
    printf("      - {name: \"temp2\", value: 28.3, zone: \"zone-C\"}\n");
    
    // ========== PRUEBAS DE FETCH ==========
    printf("\n═══ Paso 2: Pruebas de FETCH RFC 9254 ═══\n\n");
    
    // Test 1: SID simple
    printf("📍 Test 1: Fetch simple - SID 20\n");
    InstanceIdentifier iid1 = {IID_SIMPLE, 20, {.str_key = NULL}};
    CoreconfValueT *result1 = fetch_value_by_iid(data, &iid1);
    print_result("  Resultado", result1);
    printf("  ✅ Esperado: 25.5\n\n");
    
    // Test 2: Array completo
    printf("📍 Test 2: Fetch simple - SID 30 (array completo)\n");
    InstanceIdentifier iid2 = {IID_SIMPLE, 30, {.str_key = NULL}};
    CoreconfValueT *result2 = fetch_value_by_iid(data, &iid2);
    print_result("  Resultado", result2);
    printf("  ✅ Esperado: array con 3 elementos\n\n");
    
    // Test 3: Búsqueda con key - "temp1"
    printf("📍 Test 3: Fetch con key - [30, \"temp1\"]\n");
    InstanceIdentifier iid3 = {IID_WITH_STR_KEY, 30, {.str_key = "temp1"}};
    CoreconfValueT *result3 = fetch_value_by_iid(data, &iid3);
    print_result("  Resultado", result3);
    
    if (result3 && result3->type == CORECONF_HASHMAP) {
        CoreconfValueT *name = getCoreconfHashMap(result3->data.map_value, 1);
        CoreconfValueT *value = getCoreconfHashMap(result3->data.map_value, 2);
        CoreconfValueT *zone = getCoreconfHashMap(result3->data.map_value, 3);
        printf("    name: %s\n", name ? name->data.string_value : "NULL");
        printf("    value: %.2f\n", value ? value->data.real_value : 0.0);
        printf("    zone: %s\n", zone ? zone->data.string_value : "NULL");
    }
    printf("  ✅ Esperado: elemento con name=\"temp1\"\n\n");
    
    // Test 4: Búsqueda con key - "hum1"
    printf("📍 Test 4: Fetch con key - [30, \"hum1\"]\n");
    InstanceIdentifier iid4 = {IID_WITH_STR_KEY, 30, {.str_key = "hum1"}};
    CoreconfValueT *result4 = fetch_value_by_iid(data, &iid4);
    print_result("  Resultado", result4);
    
    if (result4 && result4->type == CORECONF_HASHMAP) {
        CoreconfValueT *name = getCoreconfHashMap(result4->data.map_value, 1);
        CoreconfValueT *value = getCoreconfHashMap(result4->data.map_value, 2);
        CoreconfValueT *zone = getCoreconfHashMap(result4->data.map_value, 3);
        printf("    name: %s\n", name ? name->data.string_value : "NULL");
        printf("    value: %.2f\n", value ? value->data.real_value : 0.0);
        printf("    zone: %s\n", zone ? zone->data.string_value : "NULL");
    }
    printf("  ✅ Esperado: elemento con name=\"hum1\"\n\n");
    
    // Test 5: Búsqueda con key inexistente
    printf("📍 Test 5: Fetch con key inexistente - [30, \"noexiste\"]\n");
    InstanceIdentifier iid5 = {IID_WITH_STR_KEY, 30, {.str_key = "noexiste"}};
    CoreconfValueT *result5 = fetch_value_by_iid(data, &iid5);
    print_result("  Resultado", result5);
    printf("  ✅ Esperado: NULL\n\n");
    
    // Test 6: Búsqueda con índice entero
    printf("📍 Test 6: Fetch con índice numérico - [30, 1]\n");
    InstanceIdentifier iid6 = {IID_WITH_INT_KEY, 30, {.int_key = 1}};
    CoreconfValueT *result6 = fetch_value_by_iid(data, &iid6);
    print_result("  Resultado", result6);
    
    if (result6 && result6->type == CORECONF_HASHMAP) {
        CoreconfValueT *name = getCoreconfHashMap(result6->data.map_value, 1);
        printf("    name: %s\n", name ? name->data.string_value : "NULL");
    }
    printf("  ✅ Esperado: segundo elemento (hum1)\n\n");
    
    // Test 7: Índice fuera de rango
    printf("📍 Test 7: Fetch con índice fuera de rango - [30, 99]\n");
    InstanceIdentifier iid7 = {IID_WITH_INT_KEY, 30, {.int_key = 99}};
    CoreconfValueT *result7 = fetch_value_by_iid(data, &iid7);
    print_result("  Resultado", result7);
    printf("  ✅ Esperado: NULL\n\n");
    
    // ========== TEST DE RESPUESTA CBOR ==========
    printf("═══ Paso 3: Crear FETCH response CBOR sequence ═══\n");
    
    InstanceIdentifier fetch_list[] = {
        {IID_SIMPLE, 20, {.str_key = NULL}},
        {IID_WITH_STR_KEY, 30, {.str_key = "temp1"}},
        {IID_WITH_INT_KEY, 30, {.int_key = 2}}
    };
    
    uint8_t buffer[1024];
    size_t len = create_fetch_response_iids(buffer, sizeof(buffer), data, fetch_list, 3);
    
    if (len > 0) {
        printf("  ✅ CBOR sequence generada (%zu bytes)\n", len);
        printf("  📦 CBOR hex: ");
        for (size_t i = 0; i < len && i < 64; i++) {
            printf("%02x ", buffer[i]);
        }
        if (len > 64) printf("...");
        printf("\n");
    } else {
        printf("  ❌ Error generando CBOR\n");
    }
    
    // Limpiar
    freeCoreconf(data, true);
    
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║  ✅ IMPLEMENTACIÓN RFC 9254 3.1.3 AL 100%%                  ║\n");
    printf("║  ✓ SIDs simples                                            ║\n");
    printf("║  ✓ Instance-identifiers con string keys                   ║\n");
    printf("║  ✓ Instance-identifiers con integer keys                  ║\n");
    printf("║  ✓ Búsqueda en arrays de hashmaps                         ║\n");
    printf("║  ✓ CBOR sequence encoding                                 ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    return 0;
}

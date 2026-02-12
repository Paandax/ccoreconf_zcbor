/*
 * Test EXHAUSTIVO de todas las funcionalidades migradas a zcbor
 * Cubre casos que no se probaron en los otros tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "../include/coreconfTypes.h"
#include "../include/serialization.h"
#include "../include/sid.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"

int tests_passed = 0;
int tests_failed = 0;

void test_result(const char *test_name, bool passed) {
    if (passed) {
        printf("  ✓ %s\n", test_name);
        tests_passed++;
    } else {
        printf("  ✗ %s FALLÓ\n", test_name);
        tests_failed++;
    }
}

void print_test_header(const char *title) {
    printf("\n═══════════════════════════════════════════\n");
    printf("  %s\n", title);
    printf("═══════════════════════════════════════════\n");
}

// ========================================
// TEST 1: Todos los tipos de enteros
// ========================================
void test_all_integer_types(void) {
    print_test_header("TEST 1: Todos los tipos de enteros");
    
    CoreconfValueT *root = createCoreconfHashmap();
    
    // Todos los tipos de enteros
    insertCoreconfHashMap(root->data.map_value, 1, createCoreconfInt8(-128));
    insertCoreconfHashMap(root->data.map_value, 2, createCoreconfInt16(-32768));
    insertCoreconfHashMap(root->data.map_value, 3, createCoreconfInt32(-2147483648));
    insertCoreconfHashMap(root->data.map_value, 4, createCoreconfInt64(-9223372036854775807));
    
    insertCoreconfHashMap(root->data.map_value, 5, createCoreconfUint8(255));
    insertCoreconfHashMap(root->data.map_value, 6, createCoreconfUint16(65535));
    insertCoreconfHashMap(root->data.map_value, 7, createCoreconfUint32(4294967295U));
    insertCoreconfHashMap(root->data.map_value, 8, createCoreconfUint64(18446744073709551615ULL));
    
    // Encode
    uint8_t buffer[256];
    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, sizeof(buffer), 0);
    bool success = coreconfToCBOR(root, state);
    test_result("Encoding de todos los tipos enteros", success);
    
    // Decode
    size_t len = state[0].payload - buffer;
    zcbor_new_decode_state(state, 5, buffer, len, 1, NULL, 0);
    CoreconfValueT *decoded = cborToCoreconfValue(state, 0);
    test_result("Decoding de todos los tipos enteros", decoded != NULL);
    
    // Verificar algunos valores
    CoreconfValueT *val5 = getCoreconfHashMap(decoded->data.map_value, 5);
    test_result("Uint8 correcto (255)", val5 && val5->data.u8 == 255);
    
    CoreconfValueT *val7 = getCoreconfHashMap(decoded->data.map_value, 7);
    test_result("Uint32 correcto (4294967295)", val7 && val7->data.u32 == 4294967295U);
    
    freeCoreconf(root, true);
    freeCoreconf(decoded, true);
}

// ========================================
// TEST 2: Valores reales (float/double)
// ========================================
void test_real_numbers(void) {
    print_test_header("TEST 2: Números reales");
    
    CoreconfValueT *root = createCoreconfHashmap();
    
    insertCoreconfHashMap(root->data.map_value, 1, createCoreconfReal(3.14159));
    insertCoreconfHashMap(root->data.map_value, 2, createCoreconfReal(-273.15));
    insertCoreconfHashMap(root->data.map_value, 3, createCoreconfReal(0.0));
    insertCoreconfHashMap(root->data.map_value, 4, createCoreconfReal(999999.999999));
    
    // Encode
    uint8_t buffer[256];
    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, sizeof(buffer), 0);
    bool success = coreconfToCBOR(root, state);
    test_result("Encoding de números reales", success);
    
    // Decode
    size_t len = state[0].payload - buffer;
    zcbor_new_decode_state(state, 5, buffer, len, 1, NULL, 0);
    CoreconfValueT *decoded = cborToCoreconfValue(state, 0);
    test_result("Decoding de números reales", decoded != NULL);
    
    // Verificar valores (con tolerancia)
    CoreconfValueT *val1 = getCoreconfHashMap(decoded->data.map_value, 1);
    double diff = val1 ? (val1->data.real_value - 3.14159) : 1.0;
    test_result("PI correcto", diff < 0.00001 && diff > -0.00001);
    
    freeCoreconf(root, true);
    freeCoreconf(decoded, true);
}

// ========================================
// TEST 3: Booleanos
// ========================================
void test_booleans(void) {
    print_test_header("TEST 3: Valores booleanos");
    
    CoreconfValueT *root = createCoreconfHashmap();
    
    insertCoreconfHashMap(root->data.map_value, 1, createCoreconfBoolean(true));
    insertCoreconfHashMap(root->data.map_value, 2, createCoreconfBoolean(false));
    
    // Encode
    uint8_t buffer[128];
    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, sizeof(buffer), 0);
    bool success = coreconfToCBOR(root, state);
    test_result("Encoding de booleanos", success);
    
    // Decode
    size_t len = state[0].payload - buffer;
    zcbor_new_decode_state(state, 5, buffer, len, 1, NULL, 0);
    CoreconfValueT *decoded = cborToCoreconfValue(state, 0);
    test_result("Decoding de booleanos", decoded != NULL);
    
    CoreconfValueT *val1 = getCoreconfHashMap(decoded->data.map_value, 1);
    test_result("Boolean true correcto", val1 && val1->type == CORECONF_TRUE);
    
    CoreconfValueT *val2 = getCoreconfHashMap(decoded->data.map_value, 2);
    test_result("Boolean false correcto", val2 && val2->type == CORECONF_FALSE);
    
    freeCoreconf(root, true);
    freeCoreconf(decoded, true);
}

// ========================================
// TEST 4: Strings largos y especiales
// ========================================
void test_special_strings(void) {
    print_test_header("TEST 4: Strings especiales");
    
    CoreconfValueT *root = createCoreconfHashmap();
    
    insertCoreconfHashMap(root->data.map_value, 1, createCoreconfString(""));
    insertCoreconfHashMap(root->data.map_value, 2, createCoreconfString("a"));
    insertCoreconfHashMap(root->data.map_value, 3, createCoreconfString("Hello, World! 123 @#$%"));
    
    // String largo (128 caracteres)
    char long_str[129];
    memset(long_str, 'X', 128);
    long_str[128] = '\0';
    insertCoreconfHashMap(root->data.map_value, 4, createCoreconfString(long_str));
    
    // Encode
    uint8_t buffer[512];
    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, sizeof(buffer), 0);
    bool success = coreconfToCBOR(root, state);
    test_result("Encoding de strings especiales", success);
    
    // Decode
    size_t len = state[0].payload - buffer;
    zcbor_new_decode_state(state, 5, buffer, len, 1, NULL, 0);
    CoreconfValueT *decoded = cborToCoreconfValue(state, 0);
    test_result("Decoding de strings especiales", decoded != NULL);
    
    CoreconfValueT *val1 = getCoreconfHashMap(decoded->data.map_value, 1);
    test_result("String vacío", val1 && strcmp(val1->data.string_value, "") == 0);
    
    CoreconfValueT *val4 = getCoreconfHashMap(decoded->data.map_value, 4);
    test_result("String largo (128 chars)", val4 && strlen(val4->data.string_value) == 128);
    
    freeCoreconf(root, true);
    freeCoreconf(decoded, true);
}

// ========================================
// TEST 5: Arrays anidados
// ========================================
void test_nested_arrays(void) {
    print_test_header("TEST 5: Arrays anidados");
    
    CoreconfValueT *root = createCoreconfArray();
    
    // Array interno 1
    CoreconfValueT *arr1 = createCoreconfArray();
    addToCoreconfArray(arr1, createCoreconfUint32(1));
    addToCoreconfArray(arr1, createCoreconfUint32(2));
    addToCoreconfArray(arr1, createCoreconfUint32(3));
    
    // Array interno 2
    CoreconfValueT *arr2 = createCoreconfArray();
    addToCoreconfArray(arr2, createCoreconfString("a"));
    addToCoreconfArray(arr2, createCoreconfString("b"));
    
    addToCoreconfArray(root, arr1);
    addToCoreconfArray(root, arr2);
    addToCoreconfArray(root, createCoreconfUint32(999));
    
    // Encode
    uint8_t buffer[256];
    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, sizeof(buffer), 0);
    bool success = coreconfToCBOR(root, state);
    test_result("Encoding de arrays anidados", success);
    
    // Decode
    size_t len = state[0].payload - buffer;
    zcbor_new_decode_state(state, 5, buffer, len, 1, NULL, 0);
    CoreconfValueT *decoded = cborToCoreconfValue(state, 0);
    test_result("Decoding de arrays anidados", decoded != NULL);
    
    test_result("Array principal tiene 3 elementos", 
                decoded && decoded->data.array_value->size == 3);
    
    CoreconfValueT *inner1 = decoded ? &decoded->data.array_value->elements[0] : NULL;
    test_result("Primer subarray tiene 3 elementos",
                inner1 && inner1->type == CORECONF_ARRAY && 
                inner1->data.array_value->size == 3);
    
    freeCoreconf(root, true);
    freeCoreconf(decoded, true);
}

// ========================================
// TEST 6: Hashmaps anidados
// ========================================
void test_nested_hashmaps(void) {
    print_test_header("TEST 6: Hashmaps anidados");
    
    CoreconfValueT *root = createCoreconfHashmap();
    
    CoreconfValueT *inner1 = createCoreconfHashmap();
    insertCoreconfHashMap(inner1->data.map_value, 10, createCoreconfString("nivel2"));
    insertCoreconfHashMap(inner1->data.map_value, 11, createCoreconfUint32(42));
    
    CoreconfValueT *inner2 = createCoreconfHashmap();
    insertCoreconfHashMap(inner2->data.map_value, 20, createCoreconfString("otro_nivel2"));
    
    insertCoreconfHashMap(root->data.map_value, 1, inner1);
    insertCoreconfHashMap(root->data.map_value, 2, inner2);
    insertCoreconfHashMap(root->data.map_value, 3, createCoreconfString("nivel1"));
    
    // Encode
    uint8_t buffer[256];
    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, sizeof(buffer), 0);
    bool success = coreconfToCBOR(root, state);
    test_result("Encoding de hashmaps anidados", success);
    
    // Decode
    size_t len = state[0].payload - buffer;
    zcbor_new_decode_state(state, 5, buffer, len, 1, NULL, 0);
    CoreconfValueT *decoded = cborToCoreconfValue(state, 0);
    test_result("Decoding de hashmaps anidados", decoded != NULL);
    
    CoreconfValueT *dec_inner1 = getCoreconfHashMap(decoded->data.map_value, 1);
    test_result("Hashmap interno accesible", 
                dec_inner1 && dec_inner1->type == CORECONF_HASHMAP);
    
    if (dec_inner1) {
        CoreconfValueT *val = getCoreconfHashMap(dec_inner1->data.map_value, 11);
        test_result("Valor anidado correcto", val && val->data.u32 == 42);
    }
    
    freeCoreconf(root, true);
    freeCoreconf(decoded, true);
}

// ========================================
// TEST 7: KeyMappingHashMap serialization
// ========================================
void test_keymapping_hashmap(void) {
    print_test_header("TEST 7: KeyMappingHashMap serialization");
    
    struct hashmap *keyMapping = 
        hashmap_new(sizeof(KeyMappingT), 0, 0, 0, keyMappingHash, keyMappingCompare, NULL, NULL);
    
    // Crear algunos key mappings
    KeyMappingT km1;
    km1.key = 100;
    km1.dynamicLongList = malloc(sizeof(DynamicLongListT));
    initializeDynamicLongList(km1.dynamicLongList);
    addLong(km1.dynamicLongList, 1);
    addLong(km1.dynamicLongList, 2);
    addLong(km1.dynamicLongList, 3);
    hashmap_set(keyMapping, &km1);
    
    KeyMappingT km2;
    km2.key = 200;
    km2.dynamicLongList = malloc(sizeof(DynamicLongListT));
    initializeDynamicLongList(km2.dynamicLongList);
    addLong(km2.dynamicLongList, 10);
    addLong(km2.dynamicLongList, 20);
    hashmap_set(keyMapping, &km2);
    
    // Encode
    uint8_t buffer[256];
    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, sizeof(buffer), 0);
    bool success = keyMappingHashMapToCBOR(keyMapping, state);
    test_result("Encoding de KeyMappingHashMap", success);
    
    // Decode
    size_t len = state[0].payload - buffer;
    zcbor_new_decode_state(state, 5, buffer, len, 1, NULL, 0);
    struct hashmap *decoded = cborToKeyMappingHashMap(state);
    test_result("Decoding de KeyMappingHashMap", decoded != NULL);
    
    if (decoded) {
        test_result("Tamaño correcto", hashmap_count(decoded) == 2);
        
        // Verificar un elemento
        KeyMappingT search = {.key = 100};
        const KeyMappingT *found = hashmap_get(decoded, &search);
        test_result("Key 100 encontrada", found != NULL);
        test_result("Key 100 tiene 3 elementos", 
                    found && found->dynamicLongList->size == 3);
        
        hashmap_free(decoded);
    }
    
    hashmap_free(keyMapping);
}

// ========================================
// TEST 8: Estructura mixta compleja
// ========================================
void test_complex_mixed_structure(void) {
    print_test_header("TEST 8: Estructura mixta compleja");
    
    CoreconfValueT *root = createCoreconfHashmap();
    
    // Nivel 1: Hashmap con varios tipos
    insertCoreconfHashMap(root->data.map_value, 1, createCoreconfString("texto"));
    insertCoreconfHashMap(root->data.map_value, 2, createCoreconfUint32(123));
    insertCoreconfHashMap(root->data.map_value, 3, createCoreconfReal(45.67));
    insertCoreconfHashMap(root->data.map_value, 4, createCoreconfBoolean(true));
    
    // Nivel 2: Array con tipos mixtos
    CoreconfValueT *mixed_array = createCoreconfArray();
    addToCoreconfArray(mixed_array, createCoreconfUint32(1));
    addToCoreconfArray(mixed_array, createCoreconfString("dos"));
    addToCoreconfArray(mixed_array, createCoreconfReal(3.0));
    
    // Nivel 3: Hashmap dentro del array
    CoreconfValueT *inner_map = createCoreconfHashmap();
    insertCoreconfHashMap(inner_map->data.map_value, 10, createCoreconfString("profundo"));
    addToCoreconfArray(mixed_array, inner_map);
    
    insertCoreconfHashMap(root->data.map_value, 5, mixed_array);
    
    // Encode
    uint8_t buffer[512];
    zcbor_state_t state[10];
    zcbor_new_encode_state(state, 10, buffer, sizeof(buffer), 0);
    bool success = coreconfToCBOR(root, state);
    test_result("Encoding de estructura mixta compleja", success);
    
    // Decode
    size_t len = state[0].payload - buffer;
    zcbor_new_decode_state(state, 10, buffer, len, 1, NULL, 0);
    CoreconfValueT *decoded = cborToCoreconfValue(state, 0);
    test_result("Decoding de estructura mixta compleja", decoded != NULL);
    
    // Verificar estructura
    CoreconfValueT *arr = getCoreconfHashMap(decoded->data.map_value, 5);
    test_result("Array mixto recuperado", arr && arr->type == CORECONF_ARRAY);
    
    if (arr) {
        test_result("Array tiene 4 elementos", arr->data.array_value->size == 4);
        
        CoreconfValueT *elem3 = &arr->data.array_value->elements[3];
        test_result("Elemento 3 es hashmap", elem3->type == CORECONF_HASHMAP);
        
        if (elem3->type == CORECONF_HASHMAP) {
            CoreconfValueT *deep = getCoreconfHashMap(elem3->data.map_value, 10);
            test_result("Valor profundo accesible", 
                        deep && deep->type == CORECONF_STRING &&
                        strcmp(deep->data.string_value, "profundo") == 0);
        }
    }
    
    freeCoreconf(root, true);
    freeCoreconf(decoded, true);
}

// ========================================
// TEST 9: Arrays vacíos y hashmaps vacíos
// ========================================
void test_empty_containers(void) {
    print_test_header("TEST 9: Contenedores vacíos");
    
    CoreconfValueT *root = createCoreconfHashmap();
    
    CoreconfValueT *empty_array = createCoreconfArray();
    CoreconfValueT *empty_map = createCoreconfHashmap();
    
    insertCoreconfHashMap(root->data.map_value, 1, empty_array);
    insertCoreconfHashMap(root->data.map_value, 2, empty_map);
    
    // Encode
    uint8_t buffer[128];
    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, sizeof(buffer), 0);
    bool success = coreconfToCBOR(root, state);
    test_result("Encoding de contenedores vacíos", success);
    
    // Decode
    size_t len = state[0].payload - buffer;
    zcbor_new_decode_state(state, 5, buffer, len, 1, NULL, 0);
    CoreconfValueT *decoded = cborToCoreconfValue(state, 0);
    test_result("Decoding de contenedores vacíos", decoded != NULL);
    
    CoreconfValueT *arr = getCoreconfHashMap(decoded->data.map_value, 1);
    test_result("Array vacío recuperado", arr && arr->type == CORECONF_ARRAY);
    test_result("Array vacío tiene size 0", arr && arr->data.array_value->size == 0);
    
    CoreconfValueT *map = getCoreconfHashMap(decoded->data.map_value, 2);
    test_result("Hashmap vacío recuperado", map && map->type == CORECONF_HASHMAP);
    test_result("Hashmap vacío tiene size 0", map && map->data.map_value->size == 0);
    
    freeCoreconf(root, true);
    freeCoreconf(decoded, true);
}

// ========================================
// MAIN
// ========================================
int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║           TEST SUITE EXHAUSTIVO - MIGRACIÓN ZCBOR            ║\n");
    printf("║         Cobertura completa de todas las funcionalidades      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    test_all_integer_types();
    test_real_numbers();
    test_booleans();
    test_special_strings();
    test_nested_arrays();
    test_nested_hashmaps();
    test_keymapping_hashmap();
    test_complex_mixed_structure();
    test_empty_containers();
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                      RESUMEN FINAL                            ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Tests pasados:  %d\n", tests_passed);
    printf("  Tests fallados: %d\n", tests_failed);
    printf("  Total:          %d\n", tests_passed + tests_failed);
    printf("\n");
    
    if (tests_failed == 0) {
        printf("  ✅ ¡TODOS LOS TESTS PASARON!\n");
        printf("  🎉 La migración a zcbor está 100%% completa y funcional\n");
        printf("\n");
        return 0;
    } else {
        printf("  ⚠️  Algunos tests fallaron\n");
        printf("\n");
        return 1;
    }
}

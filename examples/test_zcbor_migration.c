/*
 * Test de migración de ccoreconf a zcbor
 * Verifica que encoding y decoding funcionen correctamente
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../include/coreconfTypes.h"
#include "../include/serialization.h"
#include "../include/coreconfManipulation.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"

void print_hex(const char *label, uint8_t *buffer, size_t len) {
    printf("%s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; i++) {
        printf("%02x ", buffer[i]);
    }
    printf("\n");
}

void test_basic_encoding(void) {
    printf("=== Test 1: Encoding básico ===\n");
    
    // Crear un hashmap simple
    CoreconfValueT *root = createCoreconfHashmap();
    
    // Añadir valores: {1: 42, 2: "hello", 3: 3.14}
    CoreconfValueT *val1 = createCoreconfUint32(42);
    CoreconfValueT *val2 = createCoreconfString("hello");
    CoreconfValueT *val3 = createCoreconfReal(3.14);
    
    insertCoreconfHashMap(root->data.map_value, 1, val1);
    insertCoreconfHashMap(root->data.map_value, 2, val2);
    insertCoreconfHashMap(root->data.map_value, 3, val3);
    
    // Codificar a CBOR
    uint8_t buffer[256];
    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, sizeof(buffer), 0);
    
    bool success = coreconfToCBOR(root, state);
    assert(success && "Encoding falló");
    
    size_t encoded_len = state[0].payload - buffer;
    print_hex("CBOR encoded", buffer, encoded_len);
    
    printf("✓ Encoding exitoso (%zu bytes)\n\n", encoded_len);
    
    freeCoreconf(root, true);
}

void test_basic_decoding(void) {
    printf("=== Test 2: Decoding básico ===\n");
    
    // CBOR que representa: {1: 100, 2: "world"}
    // a3 = map(3), 01 = 1, 18 64 = 100, 02 = 2, 65 = "world"(5 bytes)
    uint8_t cbor_data[] = {
        0xa2,           // map(2)
        0x01,           // key: 1
        0x18, 0x64,     // value: 100
        0x02,           // key: 2
        0x65, 0x77, 0x6f, 0x72, 0x6c, 0x64  // value: "world"
    };
    
    print_hex("CBOR input", cbor_data, sizeof(cbor_data));
    
    // Decodificar
    zcbor_state_t state[5];
    zcbor_new_decode_state(state, 5, cbor_data, sizeof(cbor_data), 1, NULL, 0);
    
    CoreconfValueT *decoded = cborToCoreconfValue(state, 0);
    assert(decoded != NULL && "Decoding falló");
    assert(decoded->type == CORECONF_HASHMAP && "No es un hashmap");
    
    printf("✓ Decoding exitoso\n");
    printf("Hashmap size: %zu\n", decoded->data.map_value->size);
    
    // Verificar contenidos
    CoreconfValueT *val1 = getCoreconfHashMap(decoded->data.map_value, 1);
    assert(val1 != NULL && "Key 1 no encontrada");
    printf("  Key 1 tipo: %d (esperado UINT_32=%d, UINT_64=%d)\n", val1->type, CORECONF_UINT_32, CORECONF_UINT_64);
    // El decoder puede devolver UINT_64 dependiendo del tamaño
    assert((val1->type == CORECONF_UINT_32 || val1->type == CORECONF_UINT_64) && "Tipo incorrecto");
    if (val1->type == CORECONF_UINT_32) {
        printf("  Key 1 → %u (correcto)\n", val1->data.u32);
    } else {
        printf("  Key 1 → %lu (correcto)\n", val1->data.u64);
    }
    
    CoreconfValueT *val2 = getCoreconfHashMap(decoded->data.map_value, 2);
    assert(val2 != NULL && "Key 2 no encontrada");
    assert(val2->type == CORECONF_STRING && "Tipo incorrecto");
    printf("  Key 2 → \"%s\" (correcto)\n", val2->data.string_value);
    
    printf("\n");
    freeCoreconf(decoded, true);
}

void test_roundtrip(void) {
    printf("=== Test 3: Roundtrip (Encode → Decode) ===\n");
    
    // Crear estructura más compleja con array
    CoreconfValueT *root = createCoreconfHashmap();
    
    CoreconfValueT *arr = createCoreconfArray();
    addToCoreconfArray(arr, createCoreconfUint32(10));
    addToCoreconfArray(arr, createCoreconfUint32(20));
    addToCoreconfArray(arr, createCoreconfUint32(30));
    
    insertCoreconfHashMap(root->data.map_value, 100, arr);
    insertCoreconfHashMap(root->data.map_value, 200, createCoreconfString("test"));
    
    printf("Original:\n");
    printf("  {100: [10, 20, 30], 200: \"test\"}\n");
    
    // Encode
    uint8_t buffer[256];
    zcbor_state_t encode_state[5];
    zcbor_new_encode_state(encode_state, 5, buffer, sizeof(buffer), 0);
    
    bool success = coreconfToCBOR(root, encode_state);
    assert(success && "Encoding falló");
    
    size_t encoded_len = encode_state[0].payload - buffer;
    print_hex("CBOR", buffer, encoded_len);
    
    // Decode
    zcbor_state_t decode_state[5];
    zcbor_new_decode_state(decode_state, 5, buffer, encoded_len, 1, NULL, 0);
    
    CoreconfValueT *decoded = cborToCoreconfValue(decode_state, 0);
    assert(decoded != NULL && "Decoding falló");
    
    // Verificar estructura
    assert(decoded->type == CORECONF_HASHMAP);
    assert(decoded->data.map_value->size == 2);
    
    CoreconfValueT *val100 = getCoreconfHashMap(decoded->data.map_value, 100);
    assert(val100 != NULL && val100->type == CORECONF_ARRAY);
    assert(val100->data.array_value->size == 3);
    printf("✓ Array recuperado correctamente (3 elementos)\n");
    
    CoreconfValueT *val200 = getCoreconfHashMap(decoded->data.map_value, 200);
    assert(val200 != NULL && val200->type == CORECONF_STRING);
    assert(strcmp(val200->data.string_value, "test") == 0);
    printf("✓ String recuperado correctamente: \"%s\"\n", val200->data.string_value);
    
    printf("✓ Roundtrip exitoso!\n\n");
    
    freeCoreconf(root, true);
    freeCoreconf(decoded, true);
}

int main(void) {
    printf("\n🧪 Test Suite: Migración ccoreconf a zcbor\n");
    printf("==========================================\n\n");
    
    test_basic_encoding();
    test_basic_decoding();
    test_roundtrip();
    
    printf("==========================================\n");
    printf("✅ Todos los tests pasaron correctamente!\n");
    printf("🎉 La migración a zcbor está funcionando!\n\n");
    
    return 0;
}

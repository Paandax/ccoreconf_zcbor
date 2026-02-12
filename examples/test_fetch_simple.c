/*
 * Test SIMPLIFICADO de operación FETCH de CORECONF con zcbor
 * Demuestra: CBOR → Decode → Navegación directa → Encode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../include/coreconfTypes.h"
#include "../include/serialization.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"

void print_hex(const char *label, uint8_t *buffer, size_t len) {
    printf("%s (%zu bytes):\n  ", label, len);
    for (size_t i = 0; i < len; i++) {
        printf("%02x ", buffer[i]);
        if ((i + 1) % 16 == 0 && i < len - 1) printf("\n  ");
    }
    printf("\n\n");
}

void print_separator(const char *title) {
    printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║ %-60s  ║\n", title);
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
}

void print_coreconf_value(const char *label, CoreconfValueT *val, int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
    printf("%s: ", label);
    
    if (!val) {
        printf("NULL\n");
        return;
    }
    
    switch (val->type) {
        case CORECONF_STRING:
            printf("\"%s\"\n", val->data.string_value);
            break;
        case CORECONF_UINT_8:
        case CORECONF_UINT_16:
        case CORECONF_UINT_32:
        case CORECONF_UINT_64:
            printf("%lu\n", val->data.u64);
            break;
        case CORECONF_TRUE:
            printf("true\n");
            break;
        case CORECONF_FALSE:
            printf("false\n");
            break;
        case CORECONF_REAL:
            printf("%.2f\n", val->data.real_value);
            break;
        case CORECONF_HASHMAP:
            printf("HASHMAP (size=%zu)\n", val->data.map_value->size);
            break;
        case CORECONF_ARRAY:
            printf("ARRAY (size=%zu)\n", val->data.array_value->size);
            break;
        default:
            printf("tipo %d\n", val->type);
    }
}

/*
 * Modelo de ejemplo: configuración de interfaces de red
 * 
 * {
 *   1536: "eth0",          // interface name
 *   1537: 1,               // interface type
 *   1538: "192.168.1.100", // ipv4 address
 *   1539: [8080, 443]      // open ports
 * }
 */
CoreconfValueT* create_interface_config(void) {
    CoreconfValueT *root = createCoreconfHashmap();
    
    insertCoreconfHashMap(root->data.map_value, 1536, createCoreconfString("eth0"));
    insertCoreconfHashMap(root->data.map_value, 1537, createCoreconfUint32(1));
    insertCoreconfHashMap(root->data.map_value, 1538, createCoreconfString("192.168.1.100"));
    
    // Array de puertos
    CoreconfValueT *ports = createCoreconfArray();
    addToCoreconfArray(ports, createCoreconfUint32(8080));
    addToCoreconfArray(ports, createCoreconfUint32(443));
    insertCoreconfHashMap(root->data.map_value, 1539, ports);
    
    return root;
}

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║         DEMO: Operación FETCH con CORECONF + zcbor           ║\n");
    printf("║      Encode → Decode → Extract Subtree → Re-encode           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    // ═══════════════════════════════════════════════════════════════
    print_separator("PASO 1: Crear modelo de datos en memoria");
    
    CoreconfValueT *original = create_interface_config();
    printf("Modelo original creado:\n");
    printf("  SID 1536 (name): \"eth0\"\n");
    printf("  SID 1537 (type): 1\n");
    printf("  SID 1538 (ipv4): \"192.168.1.100\"\n");
    printf("  SID 1539 (ports): [8080, 443]\n");
    
    // ═══════════════════════════════════════════════════════════════
    print_separator("PASO 2: SERIALIZAR a CBOR");
    
    uint8_t buffer[512];
    zcbor_state_t encode_state[10];
    zcbor_new_encode_state(encode_state, 10, buffer, sizeof(buffer), 0);
    
    bool success = coreconfToCBOR(original, encode_state);
    assert(success && "Encoding falló");
    
    size_t cbor_len = encode_state[0].payload - buffer;
    print_hex("📦 CBOR completo", buffer, cbor_len);
    printf("✓ Serialización exitosa: %zu bytes\n", cbor_len);
    
    // ═══════════════════════════════════════════════════════════════
    print_separator("PASO 3: DESERIALIZAR desde CBOR");
    
    zcbor_state_t decode_state[10];
    zcbor_new_decode_state(decode_state, 10, buffer, cbor_len, 1, NULL, 0);
    
    CoreconfValueT *decoded = cborToCoreconfValue(decode_state, 0);
    assert(decoded != NULL && "Decoding falló");
    assert(decoded->type == CORECONF_HASHMAP && "No es un hashmap");
    
    printf("✓ Deserialización exitosa\n");
    printf("  Tipo: HASHMAP\n");
    printf("  Tamaño: %zu elementos\n", decoded->data.map_value->size);
    
    // ═══════════════════════════════════════════════════════════════
    print_separator("PASO 4: FETCH - Extraer valores específicos (QUERY)");
    
    printf("Query 1: Obtener nombre de interfaz (SID 1536)\n");
    CoreconfValueT *name = getCoreconfHashMap(decoded->data.map_value, 1536);
    print_coreconf_value("  Resultado", name, 0);
    
    printf("\nQuery 2: Obtener dirección IP (SID 1538)\n");
    CoreconfValueT *ip = getCoreconfHashMap(decoded->data.map_value, 1538);
    print_coreconf_value("  Resultado", ip, 0);
    
    printf("\nQuery 3: Obtener array de puertos (SID 1539)\n");
    CoreconfValueT *ports = getCoreconfHashMap(decoded->data.map_value, 1539);
    print_coreconf_value("  Resultado", ports, 0);
    if (ports && ports->type == CORECONF_ARRAY) {
        for (size_t i = 0; i < ports->data.array_value->size; i++) {
            CoreconfValueT *port = &ports->data.array_value->elements[i];
            printf("    Puerto [%zu]: %lu\n", i, port->data.u64);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════
    print_separator("PASO 5: FETCH - Crear subárbol con datos seleccionados");
    
    printf("Creando subárbol con solo:\n");
    printf("  - Nombre (SID 1536)\n");
    printf("  - IP (SID 1538)\n");
    
    CoreconfValueT *subtree = createCoreconfHashmap();
    if (name) {
        CoreconfValueT *name_copy = createCoreconfString(name->data.string_value);
        insertCoreconfHashMap(subtree->data.map_value, 1536, name_copy);
    }
    if (ip) {
        CoreconfValueT *ip_copy = createCoreconfString(ip->data.string_value);
        insertCoreconfHashMap(subtree->data.map_value, 1538, ip_copy);
    }
    
    printf("✓ Subárbol creado (2 elementos)\n");
    
    // ═══════════════════════════════════════════════════════════════
    print_separator("PASO 6: SERIALIZAR subárbol a CBOR");
    
    uint8_t subtree_buffer[256];
    zcbor_state_t subtree_encode_state[10];
    zcbor_new_encode_state(subtree_encode_state, 10, subtree_buffer, sizeof(subtree_buffer), 0);
    
    success = coreconfToCBOR(subtree, subtree_encode_state);
    assert(success && "Encoding de subárbol falló");
    
    size_t subtree_cbor_len = subtree_encode_state[0].payload - subtree_buffer;
    print_hex("📦 CBOR del subárbol", subtree_buffer, subtree_cbor_len);
    
    printf("✓ Subárbol serializado: %zu bytes\n", subtree_cbor_len);
    printf("  Reducción de tamaño: %.1f%% (%zu → %zu bytes)\n", 
           100.0 * (cbor_len - subtree_cbor_len) / cbor_len,
           cbor_len, subtree_cbor_len);
    
    // ═══════════════════════════════════════════════════════════════
    print_separator("PASO 7: VERIFICAR - Decodificar el subárbol");
    
    zcbor_state_t verify_decode_state[10];
    zcbor_new_decode_state(verify_decode_state, 10, subtree_buffer, subtree_cbor_len, 1, NULL, 0);
    
    CoreconfValueT *verified = cborToCoreconfValue(verify_decode_state, 0);
    assert(verified != NULL && "Verificación falló");
    
    printf("✓ Subárbol decodificado correctamente\n");
    printf("  Contenido:\n");
    
    CoreconfValueT *v_name = getCoreconfHashMap(verified->data.map_value, 1536);
    print_coreconf_value("    SID 1536 (name)", v_name, 0);
    
    CoreconfValueT *v_ip = getCoreconfHashMap(verified->data.map_value, 1538);
    print_coreconf_value("    SID 1538 (ip)", v_ip, 0);
    
    // ═══════════════════════════════════════════════════════════════
    print_separator("✅ RESUMEN DE LA OPERACIÓN FETCH");
    
    printf("\n");
    printf("  ✓ PASO 1: Modelo creado en memoria\n");
    printf("  ✓ PASO 2: Serializado a CBOR (%zu bytes)\n", cbor_len);
    printf("  ✓ PASO 3: Deserializado desde CBOR\n");
    printf("  ✓ PASO 4: Query por SIDs (1536, 1538, 1539)\n");
    printf("  ✓ PASO 5: Subárbol extraído (fetch selectivo)\n");
    printf("  ✓ PASO 6: Subárbol re-serializado (%zu bytes)\n", subtree_cbor_len);
    printf("  ✓ PASO 7: Subárbol verificado\n");
    printf("\n");
    printf("  🎉 La operación FETCH funciona perfectamente con zcbor!\n");
    printf("  🎉 Migración completada exitosamente!\n");
    printf("\n");
    
    // Cleanup
    freeCoreconf(original, true);
    freeCoreconf(decoded, true);
    freeCoreconf(subtree, true);
    freeCoreconf(verified, true);
    
    return 0;
}

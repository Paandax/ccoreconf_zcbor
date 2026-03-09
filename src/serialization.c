#include "../include/serialization.h"

#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"
#include "../coreconf_zcbor_generated/zcbor_common.h"
#include <stdio.h>
#include <string.h>

#include "../include/sid.h"

// Funciones internas para parsear arrays y maps
bool _parse_array(zcbor_state_t *state, CoreconfValueT *coreconfValue, unsigned indent);
bool _parse_map(zcbor_state_t *state, CoreconfValueT *coreconfValue, unsigned indent);

// Serializa un objeto CORECONF con su clave y valor
void serializeCoreconfObject(CoreconfObjectT *object, void *state_) {
    zcbor_state_t *state = (zcbor_state_t *)state_;
    // Primero pongo la clave (SID)
    zcbor_uint64_put(state, object->key);
    // Luego serializo el valor
    coreconfToCBOR(object->value, state);
}

// Función principal para convertir valores CORECONF a CBOR usando zcbor
bool coreconfToCBOR(CoreconfValueT *coreconfValue, zcbor_state_t *state) {
    bool res = true;
    
    // Switch según el tipo de dato CORECONF que tengo que serializar
    switch (coreconfValue->type) {
        case CORECONF_HASHMAP: {
            // Para hashmaps primero inicio el mapa con el tamaño
            res = zcbor_map_start_encode(state, coreconfValue->data.map_value->size);
            if (!res) return false;
            // Itero por todos los elementos y los serializo
            iterateCoreconfHashMap(coreconfValue->data.map_value, (void *)state, serializeCoreconfObject);
            res = zcbor_map_end_encode(state, coreconfValue->data.map_value->size);
            break;
        }
        case CORECONF_ARRAY: {
            // Arrays: inicio la lista y voy serializando cada elemento
            size_t arrayLength = coreconfValue->data.array_value->size;
            res = zcbor_list_start_encode(state, arrayLength);
            if (!res) return false;
            for (size_t i = 0; i < arrayLength; i++) {
                res = coreconfToCBOR(&coreconfValue->data.array_value->elements[i], state);
                if (!res) return false;
            }
            res = zcbor_list_end_encode(state, arrayLength);
            break;
        }
        case CORECONF_REAL:
            res = zcbor_float64_put(state, coreconfValue->data.real_value);
            break;
        case CORECONF_INT_8:
            res = zcbor_int32_put(state, coreconfValue->data.i8);
            break;
        case CORECONF_INT_16:
            res = zcbor_int32_put(state, coreconfValue->data.i16);
            break;
        case CORECONF_INT_32:
            res = zcbor_int32_put(state, coreconfValue->data.i32);
            break;
        case CORECONF_INT_64:
            res = zcbor_int64_put(state, coreconfValue->data.i64);
            break;

        case CORECONF_UINT_8:
            res = zcbor_uint32_put(state, coreconfValue->data.u8);
            break;
        case CORECONF_UINT_16:
            res = zcbor_uint32_put(state, coreconfValue->data.u16);
            break;
        case CORECONF_UINT_32:
            res = zcbor_uint32_put(state, coreconfValue->data.u32);
            break;
        case CORECONF_UINT_64:
            res = zcbor_uint64_put(state, coreconfValue->data.u64);
            break;
        case CORECONF_STRING: {
            // Para strings necesito crear una estructura zcbor_string
            struct zcbor_string zstr = {
                .value = (const uint8_t *)coreconfValue->data.string_value,
                .len = strlen(coreconfValue->data.string_value)
            };
            res = zcbor_tstr_encode(state, &zstr);
            break;
        }
        case CORECONF_TRUE:
            res = zcbor_bool_put(state, true);
            break;
        case CORECONF_FALSE:
            res = zcbor_bool_put(state, false);
            break;
        case CORECONF_NULL:
            /* CBOR null (0xf6) — usado en iPATCH para indicar eliminación de un nodo */
            res = zcbor_nil_put(state, NULL);
            break;
        default:
            // Si llego aquí es que hay algún tipo no soportado
            return false;
    }

    return res;
}

// Función para deserializar desde CBOR a estructuras CORECONF
CoreconfValueT *cborToCoreconfValue(zcbor_state_t *state, unsigned indent) {
    CoreconfValueT *coreconfValue = NULL;
    
    // Protección contra recursión infinita
    if (indent > CORECONF_MAX_DEPTH) {
        return NULL;
    }
    
    // Leo el tipo principal del CBOR mirando el primer byte
    if (state->payload >= state->payload_end) {
        return NULL;
    }
    zcbor_major_type_t major_type = ZCBOR_MAJOR_TYPE(*state->payload);
    
    // Dependiendo del tipo CBOR creo la estructura correspondiente
    switch (major_type) {
        case ZCBOR_MAJOR_TYPE_PINT: {  // Entero positivo (uint)
            uint64_t unsignedInteger = 0;
            if (zcbor_uint64_decode(state, &unsignedInteger)) {
                coreconfValue = createCoreconfUint64(unsignedInteger);
            }
            break;
        }
        case ZCBOR_MAJOR_TYPE_NINT: {  // Entero negativo
            int64_t nint = 0;
            if (zcbor_int64_decode(state, &nint)) {
                coreconfValue = createCoreconfInt64(nint);
            }
            break;
        }
        case ZCBOR_MAJOR_TYPE_BSTR: {  // Cadena de bytes
            struct zcbor_string zstr;
            if (zcbor_bstr_decode(state, &zstr)) {
                char *buf = malloc(zstr.len + 1);
                memcpy(buf, zstr.value, zstr.len);
                buf[zstr.len] = '\0';
                coreconfValue = createCoreconfString(buf);
                free(buf);
            }
            break;
        }
        case ZCBOR_MAJOR_TYPE_TSTR: {  // Cadena de texto
            struct zcbor_string zstr;
            if (zcbor_tstr_decode(state, &zstr)) {
                char formattedString[zstr.len + 1];
                snprintf(formattedString, zstr.len + 1, "%.*s", (int)zstr.len, zstr.value);
                coreconfValue = createCoreconfString(formattedString);
            }
            break;
        }
        case ZCBOR_MAJOR_TYPE_LIST: {  // Lista (array)
            coreconfValue = createCoreconfArray();
            _parse_array(state, coreconfValue, indent);
            break;
        }
        case ZCBOR_MAJOR_TYPE_MAP: {  // Mapa (diccionario)
            coreconfValue = createCoreconfHashmap();
            _parse_map(state, coreconfValue, indent);
            break;
        }
        case ZCBOR_MAJOR_TYPE_SIMPLE: {  // Tipos simples: float, bool, null...
            // Primero intento decodificar como float64
            double doubleValue = 0;
            if (zcbor_float64_decode(state, &doubleValue)) {
                coreconfValue = createCoreconfReal(doubleValue);
            } else {
                // Si no es float, pruebo con booleano
                bool boolValue;
                if (zcbor_bool_decode(state, &boolValue)) {
                    coreconfValue = createCoreconfBoolean(boolValue);
                } else if (zcbor_nil_expect(state, NULL)) {
                    /* CBOR null (0xf6): en iPATCH significa "borrar este nodo" */
                    coreconfValue = malloc(sizeof(CoreconfValueT));
                    if (coreconfValue) {
                        coreconfValue->type = CORECONF_NULL;
                        coreconfValue->data.u64 = 0;
                    }
                }
            }
            break;
        }
        default:
            break;
    }
    return coreconfValue;
}

// Función auxiliar para parsear arrays CBOR
bool _parse_array(zcbor_state_t *state, CoreconfValueT *coreconfValue, unsigned indent) {
    if (!zcbor_list_start_decode(state)) {
        printf("Error entrando en el array\n");
        return false;
    }
    
    // Voy decodificando elementos hasta llegar al final
    while (!zcbor_array_at_end(state)) {
        CoreconfValueT *arrayValue = cborToCoreconfValue(state, indent + 1);
        if (arrayValue) {
            addToCoreconfArray(coreconfValue, arrayValue);
        }
    }
    
    if (!zcbor_list_end_decode(state)) {
        return false;
    }
    return true;
}

// Función auxiliar para parsear mapas CBOR
bool _parse_map(zcbor_state_t *state, CoreconfValueT *coreconfValue, unsigned indent) {
    int loopCount = 0;
    
    if (!zcbor_map_start_decode(state)) {
        printf("Error entrando en el map\n");
        return false;
    }

    // Itero sobre el mapa extrayendo clave-valor
    while (!zcbor_array_at_end(state)) {
        if (loopCount > CORECONF_MAX_LOOP) return false;  // Protección anti-bucle infinito

        uint64_t coreconfKey = 0;
        if (!zcbor_uint64_decode(state, &coreconfKey)) {
            printf("Error parseando la clave del map\n");
            return false;
        }
        
        CoreconfValueT *value = cborToCoreconfValue(state, indent + 1);
        if (value) {
            insertCoreconfHashMap(coreconfValue->data.map_value, coreconfKey, value);
        }
        loopCount++;
    }
    
    if (!zcbor_map_end_decode(state)) {
        return false;
    }
    return true;
}

// Serializa el hashmap de mapeos de claves a CBOR
bool keyMappingHashMapToCBOR(struct hashmap *keyMappingHashMap, zcbor_state_t *state) {
    size_t iter = 0;
    void *item;

    // Inicio el mapa CBOR con el tamaño del hashmap
    size_t map_size = hashmap_count(keyMappingHashMap);
    if (!zcbor_map_start_encode(state, map_size)) return false;

    // Recorro todos los elementos del hashmap
    while (hashmap_iter(keyMappingHashMap, &iter, &item)) {
        const KeyMappingT *keyMapping = item;
        // Añado la clave
        if (!zcbor_uint64_put(state, keyMapping->key)) return false;
        
        // Añado el array de SIDs asociados
        size_t array_size = keyMapping->dynamicLongList->size;
        if (!zcbor_list_start_encode(state, array_size)) return false;
        
        for (size_t i = 0; i < array_size; i++) {
            uint64_t sidKey = *(keyMapping->dynamicLongList->longList + i);
            if (!zcbor_uint64_put(state, sidKey)) return false;
        }
        
        if (!zcbor_list_end_encode(state, array_size)) return false;
    }
    
    if (!zcbor_map_end_encode(state, map_size)) return false;
    return true;
}

// Deserializa un buffer CBOR y crea un hashmap de mapeos de claves
struct hashmap *cborToKeyMappingHashMap(zcbor_state_t *state) {
    // Creo el hashmap vacío
    struct hashmap *keyMappingHashMap =
        hashmap_new(sizeof(KeyMappingT), 0, 0, 0, keyMappingHash, keyMappingCompare, NULL, NULL);
    
    if (!zcbor_map_start_decode(state)) return NULL;
    
    int loopCounter = 0;

    while (!zcbor_array_at_end(state)) {
        // Protección para evitar bucles infinitos
        if (loopCounter > CORECONF_MAX_LOOP) return NULL;

        uint64_t key = 0;
        if (!zcbor_uint64_decode(state, &key)) {
            printf("Error parseando clave del mapa\n");
            return NULL;
        }
        
        KeyMappingT *keyMapping = malloc(sizeof(KeyMappingT));
        keyMapping->key = key;
        keyMapping->dynamicLongList = malloc(sizeof(DynamicLongListT));
        initializeDynamicLongList(keyMapping->dynamicLongList);

        if (!zcbor_list_start_decode(state)) return NULL;
        
        while (!zcbor_array_at_end(state)) {
            // Safety mechanism to avoid infinite loops
            if (loopCounter > CORECONF_MAX_LOOP) return NULL;

            uint64_t sidKey = 0;
            if (!zcbor_uint64_decode(state, &sidKey)) {
                printf("Error parseando valor del array\n");
                return NULL;
            }
            // Lo añado a la lista dinámica
            addLong(keyMapping->dynamicLongList, sidKey);
            loopCounter++;
        }
        
        if (!zcbor_list_end_decode(state)) return NULL;

        // Inserto el keyMapping en el hashmap
        hashmap_set(keyMappingHashMap, keyMapping);
        loopCounter++;
    }
    
    if (!zcbor_map_end_decode(state)) return NULL;
    return keyMappingHashMap;
}

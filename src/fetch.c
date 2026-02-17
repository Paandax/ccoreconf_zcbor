/*
 * fetch.c - Implementación de operaciones FETCH según RFC 9254
 * 
 * Este archivo contiene todas las funciones para crear, parsear y procesar
 * peticiones y respuestas FETCH del protocolo CORECONF.
 * 
 * Autor: Pablo (TFG)
 * RFC: 9254 Sección 3.1.3 - FETCH Operation
 * CBOR: RFC 8949 (formato binario compacto)
 */

#include "fetch.h"
#include "serialization.h"
#include "coreconfManipulation.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"
#include <stdlib.h>   // Para malloc, free, calloc
#include <stdint.h>   // Para uint64_t, int64_t, size_t
#include <string.h>   // Para strcmp, memcpy

/**
 * create_fetch_request - Crear petición FETCH con SIDs simples
 * 
 * Esta función genera una petición FETCH en formato CBOR sequence.
 * Un CBOR sequence es una concatenación de elementos CBOR sin array envolvente.
 * 
 * Ejemplo de uso:
 *   uint64_t sids[] = {20, 21, 30};  // Quiero temperatura, humedad, presión
 *   uint8_t buffer[256];
 *   size_t len = create_fetch_request(buffer, 256, sids, 3);
 *   send(socket, buffer, len, 0);  // Enviar al servidor
 * 
 * @param buffer: Buffer de salida donde escribir los bytes CBOR
 * @param buffer_size: Tamaño del buffer (para evitar buffer overflow)
 * @param sids: Array de SIDs que queremos solicitar
 * @param sid_count: Número de SIDs en el array
 * @return: Número de bytes escritos, o 0 si hay error
 */
size_t create_fetch_request(uint8_t *buffer, size_t buffer_size,
                             const uint64_t *sids, size_t sid_count) {
    // Validación de entrada: verificar que los parámetros no sean NULL
    if (!buffer || !sids || sid_count == 0) {
        return 0;  // Error: parámetros inválidos
    }
    
    size_t offset = 0;  // Posición actual en el buffer
    zcbor_state_t state[5];  // Estado de zcbor (stack de 5 niveles de anidamiento)
    
    // Iterar sobre cada SID y codificarlo como elemento CBOR separado
    // CBOR sequence: no hay array envolvente, solo concatenamos elementos
    for (size_t i = 0; i < sid_count; i++) {
        // Inicializar el estado de encoding de zcbor
        // state[5]: Stack para manejar anidamiento de estructuras CBOR
        // buffer + offset: Puntero a donde empezar a escribir (aritmética de punteros)
        // buffer_size - offset: Espacio restante en el buffer
        zcbor_new_encode_state(state, 5, buffer + offset, buffer_size - offset, 0);
        
        // Codificar el SID como uint64 en CBOR
        // zcbor elige automáticamente la codificación más compacta:
        // - Valores 0-23: 1 byte
        // - Valores 24-255: 2 bytes
        // - Valores 256-65535: 3 bytes, etc.
        if (!zcbor_uint64_put(state, sids[i])) {
            return 0;  // Error en encoding
        }
        
        // Actualizar offset con los bytes que escribió zcbor
        // state[0].payload apunta al final de lo escrito
        // La diferencia nos da los bytes consumidos
        offset += (state[0].payload - (buffer + offset));
    }
    
    return offset;  // Retornar total de bytes escritos
}

/**
 * create_fetch_request_with_iids - Crear petición FETCH con instance-identifiers completos
 * 
 * Esta función es más avanzada que create_fetch_request() porque soporta
 * instance-identifiers con keys (según RFC 9254):
 *   - IID_SIMPLE: Solo SID → codifica como: 20
 *   - IID_WITH_STR_KEY: SID + string → codifica como: [30, "eth0"]
 *   - IID_WITH_INT_KEY: SID + índice → codifica como: [30, 2]
 * 
 * Ejemplo de uso:
 *   InstanceIdentifier iids[] = {
 *       {IID_SIMPLE, 20, {.str_key = NULL}},           // Temperatura
 *       {IID_WITH_STR_KEY, 1533, {.str_key = "eth0"}}, // Interfaz específica
 *       {IID_WITH_INT_KEY, 30, {.int_key = 2}}         // Tercer elemento de array
 *   };
 *   uint8_t buffer[512];
 *   size_t len = create_fetch_request_with_iids(buffer, 512, iids, 3);
 * 
 * @param buffer: Buffer de salida para bytes CBOR
 * @param buffer_size: Tamaño del buffer
 * @param iids: Array de instance-identifiers
 * @param iid_count: Número de identificadores
 * @return: Bytes escritos o 0 si error
 */
size_t create_fetch_request_with_iids(uint8_t *buffer, size_t buffer_size,
                                       const InstanceIdentifier *iids, size_t iid_count) {
    if (!buffer || !iids || iid_count == 0) {
        return 0;
    }
    
    size_t offset = 0;
    zcbor_state_t state[5];
    
    // Procesar cada instance-identifier según su tipo
    for (size_t i = 0; i < iid_count; i++) {
        zcbor_new_encode_state(state, 5, buffer + offset, buffer_size - offset, 0);
        
        // Switch para manejar los 3 tipos posibles
        // Switch para manejar los 3 tipos posibles
        switch (iids[i].type) {
            case IID_SIMPLE:
                // Caso 1: SID simple, solo codificar el número
                // Ejemplo: 20 → CBOR: 0x14
                if (!zcbor_uint64_put(state, iids[i].sid)) {
                    return 0;
                }
                break;
                
            case IID_WITH_STR_KEY:
                // Caso 2: [SID, "key"] - Codificar como array de 2 elementos
                // Ejemplo: [1533, "eth0"] → CBOR: 0x82 0x19 0x05FD 0x64 "eth0"
                if (!zcbor_list_start_encode(state, 2)) return 0;  // Iniciar array
                if (!zcbor_uint64_put(state, iids[i].sid)) return 0;  // Elemento 1: SID
                if (!zcbor_tstr_put_term(state, iids[i].key.str_key, SIZE_MAX)) return 0;  // Elemento 2: string
                if (!zcbor_list_end_encode(state, 2)) return 0;  // Cerrar array
                break;
                
            case IID_WITH_INT_KEY:
                // Caso 3: [SID, índice] - Para acceso a arrays por posición
                // Ejemplo: [30, 2] → CBOR: 0x82 0x1E 0x02
                if (!zcbor_list_start_encode(state, 2)) return 0;  // Iniciar array
                if (!zcbor_uint64_put(state, iids[i].sid)) return 0;  // Elemento 1: SID
                if (!zcbor_int64_put(state, iids[i].key.int_key)) return 0;  // Elemento 2: índice
                if (!zcbor_list_end_encode(state, 2)) return 0;  // Cerrar array
                break;
                
            default:
                // Tipo desconocido - esto no debería pasar
                return 0;
        }
        
        offset += (state[0].payload - (buffer + offset));
    }
    
    return offset;
}

/**
 * create_fetch_response - Crear respuesta FETCH del servidor (SIDs simples)
 * 
 * Esta función genera la respuesta del servidor a una petición FETCH.
 * El formato es un CBOR sequence de mapas: {SID: value}, {SID: value}, ...
 * 
 * Si un SID no existe en los datos, se devuelve {SID: null}
 * 
 * Ejemplo:
 *   Cliente pidió: [20, 30, 99]
 *   Servidor tiene: {20: 25.5, 30: "temp", 40: true}
 *   Respuesta: {20: 25.5}, {30: "temp"}, {99: null}
 * 
 * @param buffer: Buffer de salida para CBOR
 * @param buffer_size: Tamaño del buffer
 * @param data_source: Datos del dispositivo (debe ser CORECONF_HASHMAP)
 * @param sids: Array de SIDs solicitados
 * @param sid_count: Número de SIDs
 * @return: Bytes escritos o 0 si error
 */
size_t create_fetch_response(uint8_t *buffer, size_t buffer_size,
                              CoreconfValueT *data_source,
                              const uint64_t *sids, size_t sid_count) {
    if (!buffer || !sids || sid_count == 0) {
        return 0;
    }
    
    size_t offset = 0;
    zcbor_state_t state[5];
    
    // Para cada SID solicitado, crear un mapa {SID: valor}
    for (size_t i = 0; i < sid_count; i++) {
        zcbor_new_encode_state(state, 5, buffer + offset, buffer_size - offset, 0);
        
        // Buscar el valor correspondiente a este SID en nuestros datos
        // getCoreconfHashMap() busca en la hash table por SID
        CoreconfValueT *value = NULL;
        if (data_source && data_source->type == CORECONF_HASHMAP) {
            value = getCoreconfHashMap(data_source->data.map_value, sids[i]);
        }
        
        // Create map {SID: value} or {SID: null}
        if (!zcbor_map_start_encode(state, 1)) return 0;
        
        if (!zcbor_uint64_put(state, sids[i])) return 0;
        
        if (value) {
            // Encode the found value
            if (!coreconfToCBOR(value, state)) return 0;
        } else {
            // Value not found: encode null
            if (!zcbor_nil_put(state, NULL)) return 0;
        }
        
        if (!zcbor_map_end_encode(state, 1)) return 0;
        
        offset += (state[0].payload - (buffer + offset));
    }
    
    return offset;
}

/**
 * fetch_value_by_iid - ⭐ BÚSQUEDA SEMÁNTICA de valores (función clave del RFC 9254)
 * 
 * Esta es LA FUNCIÓN MÁS IMPORTANTE del archivo. Implementa la resolución
 * semántica de instance-identifiers según RFC 9254.
 * 
 * No solo busca por SID, sino que ENTIENDE y PROCESA las keys:
 *   - [1533, "eth0"] → Busca en array de interfaces la que se llama "eth0"
 *   - [30, 2] → Accede al elemento en índice 2 del array
 * 
 * Casos de uso:
 *   1. IID_SIMPLE: {20} → Retorna valor directo
 *   2. IID_WITH_STR_KEY: {1533, "eth0"} → Busca en array el elemento con name="eth0"
 *   3. IID_WITH_INT_KEY: {30, 2} → Accede a arr[2]
 * 
 * @param data_source: Datos del dispositivo (hashmap)
 * @param iid: Instance-identifier a resolver
 * @return: Puntero al valor encontrado, o NULL si no existe
 */
CoreconfValueT* fetch_value_by_iid(CoreconfValueT *data_source, 
                                    const InstanceIdentifier *iid) {
    // Validar parámetros de entrada
    if (!data_source || !iid || data_source->type != CORECONF_HASHMAP) {
        return NULL;
    }
    
    // Paso 1: Obtener el hashmap principal del dispositivo
    CoreconfHashMapT *map = data_source->data.map_value;
    
    // Paso 2: Buscar el SID base en el hashmap
    // Esto nos da el "contenedor" (puede ser un valor directo, un array, etc.)
    CoreconfValueT *value = getCoreconfHashMap(map, iid->sid);
    
    if (!value) {
        return NULL;  // SID no existe en nuestros datos
    }
    
    // Caso 1: IID_SIMPLE - Solo queremos el valor del SID, sin procesar keys
    if (iid->type == IID_SIMPLE) {
        return value;  // Retornar directamente
    }
    
    // Caso 2: IID_WITH_STR_KEY - Búsqueda por string en array
    // Ejemplo: [1533, "eth0"] significa "dame la interfaz que se llama eth0"
    if (iid->type == IID_WITH_STR_KEY) {
        // Según RFC 9254, la string key se usa para buscar en arrays
        // Buscamos elementos cuyo "nombre" o algún campo coincida con la key
        
        if (value->type == CORECONF_ARRAY) {
            // El SID apunta a un array, vamos a buscar dentro
            CoreconfArrayT *arr = value->data.array_value;
            
            // Iterar sobre todos los elementos del array
            for (size_t i = 0; i < arr->size; i++) {
                CoreconfValueT *elem = &arr->elements[i];
                
                // Solo buscamos en elementos que sean hashmaps
                // (porque necesitamos campos donde buscar)
                if (elem->type == CORECONF_HASHMAP) {
                    CoreconfHashMapT *elem_map = elem->data.map_value;
                    
                    // Buscar en TODOS los campos del hashmap
                    // Si algún campo string coincide con nuestra key, es match
                    // Esto permite flexibilidad: el string puede estar en cualquier campo
                    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
                        CoreconfObjectT *obj = elem_map->table[t];
                        
                        // Como es una hash table, puede haber colisiones (lista enlazada)
                        while (obj) {
                            // Verificar si este campo es un string
                            if (obj->value && obj->value->type == CORECONF_STRING) {
                                // Comparar el valor del string con nuestra key
                                if (strcmp(obj->value->data.string_value, 
                                          iid->key.str_key) == 0) {
                                    // ¡MATCH! Retornar el elemento completo
                                    return elem;
                                }
                            }
                            obj = obj->next;  // Siguiente en la cadena de colisiones
                        }
                    }
                }
            }
            // Recorrimos todo el array y no encontramos match
            return NULL;
        }
        
        // Si el valor no es un array, no podemos buscar con string key
        // En nuestro modelo, los SIDs siempre mapean a números, no strings
        return NULL;
    }
    
    // Caso 3: IID_WITH_INT_KEY - Acceso por índice a array
    // Ejemplo: [30, 2] significa "dame el elemento en posición 2 del array"
    if (iid->type == IID_WITH_INT_KEY) {
        // Verificar que el valor sea un array
        if (value->type == CORECONF_ARRAY) {
            CoreconfArrayT *arr = value->data.array_value;
            
            // Verificación de límites (bounds checking)
            // Prevenir acceso fuera del array (importante para seguridad)
            if (iid->key.int_key >= 0 &&  // No negativo
                (size_t)iid->key.int_key < arr->size) {  // Dentro del tamaño
                
                // Acceso directo al elemento por índice
                return &arr->elements[iid->key.int_key];
            }
        }
        // Índice fuera de límites o no es un array
        return NULL;
    }
    
    // No debería llegar aquí (todos los tipos están cubiertos)
    return NULL;
}

/**
 * create_fetch_response_iids - Crear respuesta FETCH con resolución semántica
 * 
 * Esta función es como create_fetch_response() pero usa fetch_value_by_iid()
 * para resolver instance-identifiers complejos.
 * 
 * Diferencias clave:
 *   - create_fetch_response(): Solo busca por SID directo
 *   - create_fetch_response_iids(): Resuelve [SID, key] semánticamente
 * 
 * Ejemplo:
 *   Cliente pidió: [{IID_SIMPLE, 20}, {IID_WITH_STR_KEY, 1533, "eth0"}]
 *   Respuesta: {20: 25.5}, {1533: {name:"eth0", ip:"192.168.1.1"}}
 *   └─────────┘  └──────────────────────────────────┘
 *   Lookup      Búsqueda semántica en array
 *   directo
 * 
 * @param buffer: Buffer de salida
 * @param buffer_size: Tamaño del buffer
 * @param data_source: Datos del dispositivo
 * @param iids: Array de instance-identifiers
 * @param iid_count: Número de identificadores
 * @return: Bytes escritos o 0 si error
 */
size_t create_fetch_response_iids(uint8_t *buffer, size_t buffer_size,
                                   CoreconfValueT *data_source,
                                   const InstanceIdentifier *iids, size_t iid_count) {
    if (!buffer || !iids || iid_count == 0) {
        return 0;
    }
    
    size_t offset = 0;
    zcbor_state_t state[5];
    
    // Para cada instance-identifier, resolver y codificar
    for (size_t i = 0; i < iid_count; i++) {
        zcbor_new_encode_state(state, 5, buffer + offset, buffer_size - offset, 0);
        
        // Esta es la parte mágica: fetch_value_by_iid() hace la búsqueda semántica
        // Si es [1533, "eth0"], busca en el array y retorna el elemento específico
        CoreconfValueT *value = fetch_value_by_iid(data_source, &iids[i]);
        
        // Create map {SID: value} or {SID: null}
        if (!zcbor_map_start_encode(state, 1)) return 0;
        
        if (!zcbor_uint64_put(state, iids[i].sid)) return 0;
        
        if (value) {
            // Encode the found value
            if (!coreconfToCBOR(value, state)) return 0;
        } else {
            // Value not found: encode null
            if (!zcbor_nil_put(state, NULL)) return 0;
        }
        
        if (!zcbor_map_end_encode(state, 1)) return 0;
        
        offset += (state[0].payload - (buffer + offset));
    }
    
    return offset;
}

/**
 * parse_fetch_request - Parsear petición FETCH simple (solo SIDs)
 * 
 * Decodifica una CBOR sequence de SIDs: 20, 21, 30
 * 
 * Usa estrategia de "dos pasadas":
 *   1. Primera pasada: Contar cuántos SIDs hay
 *   2. Alojar memoria: malloc(count * sizeof(uint64_t))
 *   3. Segunda pasada: Llenar el array con los valores
 * 
 * ¿Por qué dos pasadas? Porque en C necesitamos saber el tamaño antes de malloc.
 * 
 * ⚠️ IMPORTANTE: El caller debe hacer free(out_sids) después de usar.
 * 
 * @param cbor_data: Bytes CBOR recibidos
 * @param cbor_size: Tamaño de los datos
 * @param out_sids: [OUTPUT] Puntero donde guardar el array alojado
 * @param out_count: [OUTPUT] Puntero donde guardar el count
 * @return: true si éxito, false si error
 */
bool parse_fetch_request(const uint8_t *cbor_data, size_t cbor_size,
                          uint64_t **out_sids, size_t *out_count) {
    if (!cbor_data || !out_sids || !out_count) {
        return false;
    }
    
    // === PRIMERA PASADA: Contar elementos ===
    size_t count = 0;
    size_t offset = 0;
    zcbor_state_t state[5];
    
    // Recorrer la CBOR sequence contando uints
    while (offset < cbor_size) {
        zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
        
        uint64_t temp_sid;
        if (!zcbor_uint64_decode(state, &temp_sid)) {
            break;  // No hay más SIDs válidos
        }
        
        count++;  // Incrementar contador
        offset += (state[0].payload - (cbor_data + offset));  // Avanzar offset
    }
    
    // Verificar que encontramos al menos 1 SID
    if (count == 0) {
        return false;  // CBOR sequence vacío o inválido
    }
    
    // === Alojar memoria para el array ===
    uint64_t *sids = (uint64_t *)malloc(sizeof(uint64_t) * count);
    if (!sids) {
        return false;  // malloc falló (sin memoria)
    }
    
    // Second pass: decode SIDs
    offset = 0;
    for (size_t i = 0; i < count; i++) {
        zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
        
        if (!zcbor_uint64_decode(state, &sids[i])) {
            free(sids);
            return false;
        }
        
        offset += (state[0].payload - (cbor_data + offset));
    }
    
    *out_sids = sids;
    *out_count = count;
    return true;
}

/**
 * is_fetch_request - Detectar si los datos CBOR son FETCH o STORE
 * 
 * Diferencia entre operaciones:
 *   FETCH:  Empieza con uint → [SID, SID, ...]
 *   STORE:  Empieza con map → {SID: value, ...}
 * 
 * Esta función simplemente intenta decodificar un uint.
 * Si tiene éxito, es FETCH. Si falla, probablemente es STORE.
 * 
 * Uso típico en servidor:
 *   if (is_fetch_request(buffer, size)) {
 *       handle_fetch();
 *   } else {
 *       handle_store();
 *   }
 * 
 * @param cbor_data: Bytes CBOR recibidos
 * @param cbor_size: Tamaño de los datos
 * @return: true si es FETCH, false si no
 */
bool is_fetch_request(const uint8_t *cbor_data, size_t cbor_size) {
    if (!cbor_data || cbor_size == 0) {
        return false;
    }
    
    zcbor_state_t state[5];
    zcbor_new_decode_state(state, 5, cbor_data, cbor_size, 1, NULL, 0);
    
    // Intentar decodificar un uint64
    uint64_t temp_sid;
    return zcbor_uint64_decode(state, &temp_sid);
    // └─────┬────┘
    //   true = es uint = FETCH
    //   false = no es uint = probablemente STORE
}

/**
 * parse_fetch_request_iids - Parsear petición FETCH completa con instance-identifiers
 * 
 * Esta es LA FUNCIÓN DE PARSING MÁS COMPLEJA del archivo.
 * Debe manejar una mezcla de:
 *   - SIDs simples: 20
 *   - Arrays con string: [1533, "eth0"]
 *   - Arrays con int: [30, 2]
 * Todo en la misma CBOR sequence.
 * 
 * Ejemplo de entrada (CBOR):
 *   0x14 0x82 0x19 0x05FD 0x64 "eth0" 0x82 0x1E 0x02
 *   └─┘  └──────────array──────────┘  └───array────┘
 *   20   [1533, "eth0"]              [30, 2]
 * 
 * Salida:
 *   iids[0] = {IID_SIMPLE, 20, ...}
 *   iids[1] = {IID_WITH_STR_KEY, 1533, "eth0"}
 *   iids[2] = {IID_WITH_INT_KEY, 30, 2}
 * 
 * Estrategia: Dos pasadas (contar + decodificar)
 * 
 * ⚠️ IMPORTANTE: El caller debe llamar free_instance_identifiers() después.
 * 
 * @param cbor_data: Bytes CBOR de la petición
 * @param cbor_size: Tamaño de los datos
 * @param out_iids: [OUTPUT] Puntero donde guardar el array alojado
 * @param out_count: [OUTPUT] Puntero donde guardar el count
 * @return: true si éxito, false si error
 */
bool parse_fetch_request_iids(const uint8_t *cbor_data, size_t cbor_size,
                                InstanceIdentifier **out_iids, size_t *out_count) {
    if (!cbor_data || !out_iids || !out_count) {
        return false;
    }
    
    // === PRIMERA PASADA: Contar elementos ===
    size_t count = 0;
    size_t offset = 0;
    zcbor_state_t state[5];
    
    // Recorrer la CBOR sequence contando elementos
    while (offset < cbor_size) {
        zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
        
        // Intentar decodificar como SID simple (uint)
        uint64_t temp_sid;
        if (zcbor_uint64_decode(state, &temp_sid)) {
            // Es un SID simple
            count++;
        } else {
            // No es uint, intentar como array [SID, key]
            zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
            if (zcbor_list_start_decode(state)) {
                count++;
                
                // Saltar hasta el final de este array
                // Usamos un mini-parser de CBOR para navegar la estructura
                int depth = 1;  // Profundidad de anidamiento
                while (depth > 0 && state[0].payload < cbor_data + cbor_size) {
                    uint8_t major = state[0].payload[0] >> 5;  // Tipo CBOR (bits 5-7)
                    uint8_t additional = state[0].payload[0] & 0x1f;  // Info adicional (bits 0-4)
                    
                    // Detectar inicio de estructuras anidadas
                    if (major == 4 || major == 5) {  // Array (4) o Map (5)
                        depth++;
                    } else if (additional == 31) {  // Break byte (fin de indefinite)
                        depth--;
                    }
                    
                    state[0].payload++;  // Avanzar un byte
                }
                zcbor_list_end_decode(state);
            } else {
                break;  // No es ni uint ni array, terminar
            }
        }
        
        // Actualizar offset para el siguiente elemento
        offset += (state[0].payload - (cbor_data + offset));
    }
    
    // Verificar que encontramos al menos 1 elemento
    if (count == 0) {
        return false;
    }
    
    // === Alojar memoria para el array ===
    // calloc() inicializa todo a 0 (más seguro que malloc)
    InstanceIdentifier *iids = (InstanceIdentifier *)calloc(count, sizeof(InstanceIdentifier));
    if (!iids) {
        return false;  // Sin memoria
    }
    
    // === SEGUNDA PASADA: Decodificar valores ===
    offset = 0;  // Reset offset
    for (size_t i = 0; i < count; i++) {
        zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
        
        uint64_t sid;
        
        // Intentar decodificar como SID simple
        if (zcbor_uint64_decode(state, &sid)) {
            // Caso 1: SID simple
            iids[i].type = IID_SIMPLE;
            iids[i].sid = sid;
            iids[i].key.str_key = NULL;  // No hay key
        } else {
            // Caso 2 o 3: Array [SID, key]
            // Caso 2 o 3: Array [SID, key]
            // Reiniciar estado para decodificar el array
            zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
            
            // Abrir array
            if (!zcbor_list_start_decode(state)) {
                free_instance_identifiers(iids, i);  // Error: liberar lo que llevamos
                return false;
            }
            
            // Decodificar primer elemento del array: el SID
            if (!zcbor_uint64_decode(state, &sid)) {
                free_instance_identifiers(iids, i);
                return false;
            }
            
            iids[i].sid = sid;  // Guardar el SID
            
            // Decodificar segundo elemento: puede ser string o int
            
            // Intentar string primero
            struct zcbor_string zstr;
            if (zcbor_tstr_decode(state, &zstr)) {
                // Es un string key
                iids[i].type = IID_WITH_STR_KEY;
                
                // ⚠️ IMPORTANTE: zcbor_string NO está null-terminated
                // Necesitamos copiar a un nuevo buffer con \0 al final
                iids[i].key.str_key = (char *)malloc(zstr.len + 1);  // +1 para \0
                if (!iids[i].key.str_key) {
                    free_instance_identifiers(iids, i);
                    return false;  // malloc falló
                }
                
                // Copiar el string desde el buffer CBOR
                memcpy(iids[i].key.str_key, zstr.value, zstr.len);
                iids[i].key.str_key[zstr.len] = '\0';  // Añadir null terminator
            } else {
                // No es string, intentar int
                int64_t int_key;
                if (zcbor_int64_decode(state, &int_key)) {
                    // Es un int key
                    iids[i].type = IID_WITH_INT_KEY;
                    iids[i].key.int_key = int_key;
                } else {
                    // No es ni string ni int -> error
                    free_instance_identifiers(iids, i);
                    return false;
                }
            }
            
            // Cerrar array
            if (!zcbor_list_end_decode(state)) {
                free_instance_identifiers(iids, i + 1);
                return false;
            }
        }
        
        // Actualizar offset para el siguiente elemento
        offset += (state[0].payload - (cbor_data + offset));
    }
    
    // Devolver resultados
    *out_iids = iids;  // El caller es dueño de este puntero ahora
    *out_count = count;
    return true;  // Éxito
}

/**
 * free_instance_identifiers - Liberar memoria de instance-identifiers
 * 
 * Esta función SIEMPRE debe llamarse después de usar parse_fetch_request_iids().
 * 
 * ¿Qué libera?
 *   1. Los strings en IID_WITH_STR_KEY (alojados con malloc)
 *   2. El array principal de estructuras (alojado con calloc)
 * 
 * ⚠️ ORDEN IMPORTANTE:
 *   - Primero liberar strings dentro (contenido)
 *   - Luego liberar array (contenedor)
 *   Si lo haces al revés, pierdes acceso a los strings -> memory leak
 * 
 * Uso típico:
 *   InstanceIdentifier *iids = NULL;
 *   size_t count = 0;
 *   parse_fetch_request_iids(cbor, size, &iids, &count);
 *   // ... usar iids ...
 *   free_instance_identifiers(iids, count);  // ¡No olvidar!
 * 
 * @param iids: Array de identificadores a liberar
 * @param count: Número de elementos en el array
 */
void free_instance_identifiers(InstanceIdentifier *iids, size_t count) {
    if (!iids) return;  // NULL check por seguridad
    
    // Paso 1: Liberar strings dentro de las estructuras
    for (size_t i = 0; i < count; i++) {
        // Solo los IID_WITH_STR_KEY tienen strings alojados
        if (iids[i].type == IID_WITH_STR_KEY && iids[i].key.str_key) {
            free(iids[i].key.str_key);  // Liberar el string
        }
    }
    
    // Paso 2: Liberar el array principal
    free(iids);
    
    // Nota: No hacemos iids = NULL aquí porque iids es copia local del puntero
    // El caller debe hacer: iids = NULL; si quiere
}

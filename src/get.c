/**
 * get.c - Implementación de la operación GET de CORECONF
 *
 * RFC Reference: draft-ietf-core-comi
 * Sección: 3.3 - GET operation
 *
 * Flujo CoAP:
 *   GET /c  →  2.05 Content  CF=112  { SID: value, ... }
 *
 * Diferencias con fetch.c:
 *   - GET no tiene payload en la petición (no hay nada que parsear del cliente)
 *   - La respuesta es un único mapa CBOR (no un cbor-seq de mapas)
 *   - Se usa CF=112 (yang-data+cbor) en lugar de CF=142 (yang-instances+cbor-seq)
 *
 * Funciones exportadas (ver get.h):
 *   - create_get_response()  →  servidor: serializa todo el datastore → mapa CBOR
 *   - parse_get_response()   →  cliente: decodifica mapa CBOR → CoreconfValueT
 *
 * Author: Generated following fetch.c pattern
 */

#include "../include/get.h"
#include "../include/serialization.h"
#include "../include/coreconfManipulation.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* =========================================================================
 * create_get_response - Crear respuesta GET con el datastore completo
 *
 * Esta es la función principal del lado SERVIDOR para la operación GET.
 *
 * ¿Qué hace?
 *   Recorre TODOS los nodos del hashmap del dispositivo y los serializa
 *   como un único mapa CBOR: { SID1: value1, SID2: value2, ... }
 *
 * Formato CBOR resultante (CF=112):
 *   {
 *     20: 22.5,          <- temperatura
 *     30: 60,            <- humedad
 *     1533: "active"     <- estado de interfaz
 *   }
 *
 * Diferencia con create_fetch_response():
 *   - FETCH: devuelve un CBOR-sequence de mapas individuales: {SID:v}, {SID:v}
 *   - GET:   devuelve UN ÚNICO mapa con todos los SIDs: {SID:v, SID:v, ...}
 *
 * Implementación:
 *   - Usamos la hash table interna directamente (CoreconfHashMapT.table)
 *   - Iteramos slot a slot, siguiendo las cadenas de colisión (lista enlazada)
 *   - Usamos zcbor para codificar el mapa completo en un solo estado
 *
 * @param buffer:      Buffer de salida para los bytes CBOR
 * @param buffer_size: Tamaño del buffer
 * @param datastore:   CoreconfValueT de tipo CORECONF_HASHMAP con los datos
 * @return:            Bytes escritos o 0 si error
 * ========================================================================= */
size_t create_get_response(uint8_t *buffer, size_t buffer_size,
                            CoreconfValueT *datastore)
{
    /* --- Validación de parámetros --- */
    if (!buffer || !datastore || datastore->type != CORECONF_HASHMAP) {
        return 0;
    }

    CoreconfHashMapT *map = datastore->data.map_value;
    if (!map) {
        return 0;
    }

    /* --- Contar entradas del hashmap ---
     * CoreconfHashMapT.size contiene el número total de entradas.
     * Lo necesitamos para el header del mapa CBOR (definite-length).
     */
    size_t entry_count = map->size;
    if (entry_count == 0) {
        /* Datastore vacío: devolver mapa CBOR vacío {} */
        zcbor_state_t state[5];
        zcbor_new_encode_state(state, 5, buffer, buffer_size, 0);
        if (!zcbor_map_start_encode(state, 0)) return 0;
        if (!zcbor_map_end_encode(state, 0)) return 0;
        return (size_t)(state[0].payload - buffer);
    }

    /* --- Inicializar estado zcbor para codificar ---
     * Un único estado cubre todo el buffer (a diferencia de fetch.c
     * que usa un estado por entrada porque el formato es un cbor-seq).
     */
    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, buffer_size, 0);

    /* --- Abrir mapa CBOR con entry_count entradas ---
     * zcbor_map_start_encode escribe el header CBOR del mapa.
     * El segundo parámetro es el máximo de pares clave-valor permitidos.
     */
    if (!zcbor_map_start_encode(state, entry_count)) {
        return 0;
    }

    /* --- Iterar sobre toda la hash table ---
     *
     * Estructura de CoreconfHashMapT:
     *   table[0..HASHMAP_TABLE_SIZE-1] → punteros a CoreconfObjectT
     *
     * Cada slot puede tener una lista enlazada (colisiones de hash):
     *   table[slot] → obj1 → obj2 → ... → NULL
     *
     * Cada CoreconfObjectT tiene:
     *   .key   → uint64_t (el SID)
     *   .value → CoreconfValueT* (el valor del nodo yang)
     *   .next  → puntero al siguiente en la cadena
     */
    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
        CoreconfObjectT *obj = map->table[t];

        while (obj) {
            /* Codificar la clave: el SID como uint64 */
            if (!zcbor_uint64_put(state, obj->key)) {
                return 0;
            }

            /* Codificar el valor: delegar a coreconfToCBOR() que maneja
             * todos los tipos (string, int, real, bool, null, array, hashmap)
             */
            if (obj->value) {
                if (!coreconfToCBOR(obj->value, state)) {
                    return 0;
                }
            } else {
                /* Valor NULL: codificar como CBOR null */
                if (!zcbor_nil_put(state, NULL)) {
                    return 0;
                }
            }

            obj = obj->next;  /* Siguiente en la cadena de colisión */
        }
    }

    /* --- Cerrar el mapa CBOR ---
     * zcbor_map_end_encode cierra el mapa y verifica que se codificaron
     * exactamente entry_count pares clave-valor.
     */
    if (!zcbor_map_end_encode(state, entry_count)) {
        return 0;
    }

    /* --- Retornar bytes escritos --- */
    return (size_t)(state[0].payload - buffer);
}

/* =========================================================================
 * parse_get_response - Parsear respuesta GET recibida (lado cliente)
 *
 * El cliente recibe un payload CBOR con CF=112 que contiene un mapa:
 *   { SID1: value1, SID2: value2, ... }
 *
 * Esta función lo decodifica y devuelve un CoreconfValueT de tipo
 * CORECONF_HASHMAP con todos los nodos, listo para usar con
 * getCoreconfHashMap(), printCoreconf(), etc.
 *
 * Delega la decodificación a cborToCoreconfValue() que ya maneja
 * recursivamente todos los tipos CBOR → tipos CORECONF.
 *
 * ⚠️  El llamador DEBE liberar la memoria con freeCoreconf(result, true)
 *
 * Ejemplo de uso:
 *   CoreconfValueT *ds = parse_get_response(payload, payload_len);
 *   if (!ds) { fprintf(stderr, "Error parseando GET response\n"); return; }
 *
 *   // Leer un SID concreto del resultado
 *   CoreconfValueT *temp = getCoreconfHashMap(ds->data.map_value, 20);
 *   if (temp) printCoreconf(temp);
 *
 *   freeCoreconf(ds, true);  // ← OBLIGATORIO
 *
 * @param data: Buffer con los bytes CBOR de la respuesta
 * @param len:  Longitud del buffer
 * @return:     CoreconfValueT (CORECONF_HASHMAP), o NULL si error
 * ========================================================================= */
CoreconfValueT *parse_get_response(const uint8_t *data, size_t len)
{
    /* --- Validación --- */
    if (!data || len == 0) {
        return NULL;
    }

    /* --- Inicializar estado zcbor para decodificar ---
     * zcbor_new_decode_state(state, n_states, payload, payload_len,
     *                        max_elements, value_limits, n_limits)
     * max_elements=1 porque el payload raíz es un único mapa CBOR.
     */
    zcbor_state_t state[5];
    zcbor_new_decode_state(state, 5, data, len, 1, NULL, 0);

    /* --- Delegar a cborToCoreconfValue ---
     * Esta función ya implementa la decodificación completa de CBOR:
     *   - CBOR map → CORECONF_HASHMAP  (nuestro caso para GET)
     *   - CBOR array → CORECONF_ARRAY
     *   - CBOR text string → CORECONF_STRING
     *   - CBOR uint → CORECONF_UINT_*
     *   - CBOR int → CORECONF_INT_*
     *   - CBOR float → CORECONF_REAL
     *   - CBOR true/false → CORECONF_TRUE / CORECONF_FALSE
     *   - CBOR null → CORECONF_NULL
     *
     * El segundo argumento (indent=0) se usa internamente para debug.
     */
    return cborToCoreconfValue(state, 0);
}

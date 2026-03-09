/**
 * put.c - Implementación de la operación PUT de CORECONF
 *
 * RFC Reference: draft-ietf-core-comi
 * Sección: 4.3 - PUT operation
 *
 * Flujo CoAP:
 *   PUT /c  CF=142  { SID: val, ... }  →  2.04 Changed / 2.01 Created
 *
 * La operación PUT reemplaza el datastore COMPLETO:
 *   - El servidor descarta el estado anterior
 *   - El nuevo datastore contiene EXACTAMENTE los SIDs del payload
 *   - SIDs anteriores no incluidos en el PUT desaparecen
 *
 * Funciones exportadas (ver put.h):
 *   - parse_put_request()   → servidor: decodifica el payload CBOR del request
 *   - create_put_request()  → cliente:  serializa el datastore completo como CBOR
 *
 * Author: Generated following ipatch.c / get.c pattern
 */

#include "../include/put.h"
#include "../include/serialization.h"
#include "../include/coreconfManipulation.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* =========================================================================
 * parse_put_request - Decodificar payload PUT (lado servidor)
 *
 * El servidor recibe CF=142  { SID1: val1, SID2: val2, ... }
 *
 * El formato es idéntico al de la respuesta GET (CF=142) y al de iPATCH
 * (CF=141 cambia el número pero el CBOR es el mismo mapa).
 *
 * Tras el parse, el servidor REEMPLAZA su datastore completo con este valor.
 * (ver ipatch_server.c → handle_put_coreconf para ver la sustitución)
 *
 * ⚠️  El llamador DEBE liberar con freeCoreconf(result, true)
 *
 * @param data  Buffer con bytes CBOR del request PUT
 * @param len   Longitud del buffer
 * @return      CoreconfValueT (CORECONF_HASHMAP), o NULL si error
 * ========================================================================= */
CoreconfValueT *parse_put_request(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return NULL;

    zcbor_state_t state[8];
    zcbor_new_decode_state(state, 8, data, len, 1, NULL, 0);

    /* cborToCoreconfValue maneja todos los tipos CBOR → CORECONF.
     * Para PUT el payload raíz es un mapa → retorna CORECONF_HASHMAP. */
    return cborToCoreconfValue(state, 0);
}

/* =========================================================================
 * create_put_request - Crear payload PUT para enviar (lado cliente)
 *
 * Serializa el datastore completo como un único mapa CBOR (CF=142):
 *   { SID1: val1, SID2: val2, ... }
 *
 * Funcionalmente idéntico a create_get_response() — mismo CBOR, misma CF.
 * La diferencia está en el método CoAP usado: PUT en vez de GET.
 *
 * El payload incluye TODOS los SIDs del datastore pasado. El servidor
 * usará este contenido para REEMPLAZAR su estado completo.
 *
 * @param buffer       Buffer de salida para los bytes CBOR
 * @param buffer_size  Tamaño del buffer
 * @param datastore    CORECONF_HASHMAP con el nuevo estado completo
 * @return             Bytes escritos, o 0 si error
 * ========================================================================= */
size_t create_put_request(uint8_t *buffer, size_t buffer_size,
                           const CoreconfValueT *datastore)
{
    if (!buffer || !datastore || datastore->type != CORECONF_HASHMAP) return 0;

    CoreconfHashMapT *map = datastore->data.map_value;
    if (!map) return 0;

    size_t entry_count = map->size;

    zcbor_state_t state[8];
    zcbor_new_encode_state(state, 8, buffer, buffer_size, 1);

    if (entry_count == 0) {
        /* Datastore vacío → mapa CBOR vacío {} */
        if (!zcbor_map_start_encode(state, 0)) return 0;
        if (!zcbor_map_end_encode(state, 0))   return 0;
        return (size_t)(state[0].payload - buffer);
    }

    if (!zcbor_map_start_encode(state, entry_count)) return 0;

    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
        CoreconfObjectT *obj = map->table[t];
        while (obj) {
            if (!zcbor_uint64_put(state, obj->key)) return 0;
            if (obj->value) {
                if (!coreconfToCBOR(obj->value, state)) return 0;
            } else {
                if (!zcbor_nil_put(state, NULL)) return 0;
            }
            obj = obj->next;
        }
    }

    if (!zcbor_map_end_encode(state, entry_count)) return 0;

    return (size_t)(state[0].payload - buffer);
}

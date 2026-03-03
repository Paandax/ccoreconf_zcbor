/**
 * delete.h - DELETE CORECONF (RFC 9254 §4.4)
 *
 * DELETE /c?k=<SID>[&k=<SID>...]
 *   → elimina los SIDs indicados del datastore
 *   → sin query params: elimina el datastore entero
 *   → respuesta: 2.02 Deleted, sin payload
 *   → si no existe: 4.04 Not Found
 */
#ifndef DELETE_H
#define DELETE_H

#include <stdint.h>
#include <stddef.h>
#include "coreconfTypes.h"
#include "hashmap.h"

/* CF=142: application/yang-data+cbor; id=sid */
#define COAP_MEDIA_TYPE_YANG_DATA_CBOR 142

/* Máximo de SIDs que se pueden borrar en una sola petición */
#define DELETE_MAX_SIDS 32

/**
 * parse_delete_query - Extrae SIDs de la query string de un DELETE
 *
 * Formato query: "k=10&k=20&k=21"
 * Cada parámetro 'k' es un SID a eliminar.
 *
 * @query:    query string (bytes, sin '\0' necesario)
 * @qlen:     longitud de query
 * @sids_out: array de salida donde se escriben los SIDs
 * @max_sids: tamaño máximo de sids_out
 * @return:   número de SIDs encontrados (0 = borrar todo)
 */
int parse_delete_query(const uint8_t *query, size_t qlen,
                       uint64_t *sids_out, int max_sids);

/**
 * apply_delete - Elimina SIDs concretos del hashmap
 *
 * @map:     hashmap del datastore
 * @sids:    array de SIDs a eliminar
 * @n_sids:  número de SIDs
 * @return:  número de SIDs efectivamente eliminados
 */
int apply_delete(CoreconfHashMapT *map, const uint64_t *sids, int n_sids);

#endif /* DELETE_H */

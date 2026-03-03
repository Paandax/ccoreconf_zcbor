/**
 * get.h - Operación GET de CORECONF (RFC draft-ietf-core-comi §3.3)
 *
 * RFC Reference: draft-ietf-core-comi
 * Sección: 3.3 - GET operation
 *
 * Descripción:
 *   El GET de CORECONF permite leer todo el datastore de un dispositivo.
 *   A diferencia del FETCH (que pide SIDs concretos), el GET devuelve
 *   TODOS los nodos yang del dispositivo en un único mapa CBOR.
 *
 * Flujo RFC §3.3:
 *   Cliente                           Servidor
 *     |  GET /c                           |
 *     |  (sin payload)                    |
 *     |---------------------------------->|
 *     |                                   |
 *     |  2.05 Content                     |
 *     |  Content-Format: 112              |
 *     |  Payload: { SID: value, ... }     |
 *     |<----------------------------------|
 *
 * Content-Formats (RFC §2.3):
 *   - Request:  (ninguno, GET no lleva payload)
 *   - Response: CF=112  application/yang-data+cbor; id=sid
 *
 * Payload de respuesta:
 *   Un único mapa CBOR con todos los SIDs del datastore:
 *     { SID1: value1, SID2: value2, ..., SIDn: valuen }
 *
 * Diferencias con FETCH:
 *   - GET: sin request payload → respuesta = mapa completo CF=112
 *   - FETCH: request = lista de SIDs CF=141 → respuesta = cbor-seq de mapas CF=142
 *
 * Author: Generated following fetch.h pattern
 */

#ifndef GET_H
#define GET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "coreconfTypes.h"

/* =========================================================================
 * Content-Format para GET (RFC draft-ietf-core-comi §2.3)
 * ========================================================================= */

/** CF=112: application/yang-data+cbor; id=sid  (GET response, PUT request) */
#define COAP_MEDIA_TYPE_YANG_DATA_CBOR 142  /* application/yang-data+cbor; id=sid (RFC 9254) */

/* =========================================================================
 * Funciones de servidor (crear respuesta GET)
 * ========================================================================= */

/**
 * @brief Crear respuesta GET con el datastore completo (lado servidor)
 *
 * Serializa todos los nodos del datastore como un único mapa CBOR:
 *   { SID1: value1, SID2: value2, ..., SIDn: valuen }
 *
 * Se usa con Content-Format: 112 (application/yang-data+cbor; id=sid)
 *
 * Ejemplo:
 *   // El servidor tiene: temperatura=22.5, humedad=60, estado="on"
 *   uint8_t buf[1024];
 *   size_t len = create_get_response(buf, sizeof(buf), datastore);
 *   // len > 0 → responder con CF=112 y payload buf[0..len-1]
 *
 * @param buffer       Buffer de salida para los bytes CBOR
 * @param buffer_size  Tamaño del buffer
 * @param datastore    CoreconfValueT de tipo CORECONF_HASHMAP con los datos
 * @return             Número de bytes escritos, o 0 en caso de error
 */
size_t create_get_response(uint8_t *buffer, size_t buffer_size,
                            CoreconfValueT *datastore);

/* =========================================================================
 * Funciones de cliente (parsear respuesta GET)
 * ========================================================================= */

/**
 * @brief Parsear la respuesta GET recibida (lado cliente)
 *
 * Decodifica el mapa CBOR de una respuesta GET y devuelve un
 * CoreconfValueT de tipo CORECONF_HASHMAP con todos los SIDs y valores.
 *
 * El llamador es responsable de liberar la memoria con freeCoreconf().
 *
 * Ejemplo:
 *   CoreconfValueT *ds = parse_get_response(payload, payload_len);
 *   if (ds) {
 *       printCoreconf(ds);
 *       freeCoreconf(ds, true);
 *   }
 *
 * @param data  Buffer con bytes CBOR de la respuesta GET
 * @param len   Longitud del buffer
 * @return      CoreconfValueT (CORECONF_HASHMAP) con los datos,
 *              o NULL en caso de error
 */
CoreconfValueT *parse_get_response(const uint8_t *data, size_t len);

#endif /* GET_H */

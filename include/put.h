/**
 * put.h - Operación PUT de CORECONF (RFC draft-ietf-core-comi §4.3)
 *
 * RFC Reference: draft-ietf-core-comi
 * Sección: 4.3 - PUT operation
 *
 * Descripción:
 *   PUT reemplaza el datastore COMPLETO de un dispositivo.
 *   A diferencia de iPATCH (parcial), PUT sobrescribe todos los SIDs:
 *   el datastore queda exactamente con el contenido del payload, nada más.
 *
 * Flujo RFC §4.3:
 *   Cliente                               Servidor
 *     |  PUT /c                              |
 *     |  Content-Format: 142                 |
 *     |  Payload: { SID: val, SID: val, ... }|  ← datastore COMPLETO nuevo
 *     |------------------------------------->|
 *     |                                      |
 *     |  2.04 Changed  (recurso existía)     |  ← sin payload
 *     |  2.01 Created  (recurso nuevo)       |  ← sin payload
 *     |<-------------------------------------|
 *
 * Content-Formats (RFC §2.3):
 *   - Request:  CF=142  application/yang-data+cbor; id=sid
 *   - Response: (ninguno en éxito)
 *
 * Diferencia clave vs iPATCH:
 *   iPATCH { 20: 99.9 }
 *     → solo SID 20 cambia, SID 10/11/21 permanecen
 *
 *   PUT { 20: 99.9 }
 *     → el datastore queda SOLO con SID 20, SID 10/11/21 desaparecen
 *
 * Errores RFC §6 (con payload CBOR CF=142):
 *   - 4.00 Bad Request:    CBOR malformado → malformed-message (1012)
 *   - 5.00 Internal Error: fallo al reemplazar → operation-failed (1019)
 *
 * Author: Generated following ipatch.h pattern
 */

#ifndef PUT_H
#define PUT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "coreconfTypes.h"

/* =========================================================================
 * Content-Formats para PUT (RFC draft-ietf-core-comi §2.3 / RFC 9254)
 * ========================================================================= */

/** CF=142: application/yang-data+cbor; id=sid  (PUT request y GET response) */
#define COAP_MEDIA_TYPE_YANG_DATA_CBOR  142

/* =========================================================================
 * Funciones de servidor
 * ========================================================================= */

/**
 * @brief Parsear payload PUT recibido (lado servidor)
 *
 * Decodifica el mapa CBOR del request PUT (CF=142):
 *   { SID1: val1, SID2: val2, ... }  ← datastore completo nuevo
 *
 * Idéntico en formato al de la respuesta GET — mismo CF, mismo CBOR.
 * La diferencia es semántica: este payload REEMPLAZA el datastore completo.
 *
 * ⚠️  El llamador DEBE liberar con freeCoreconf(result, true)
 *
 * @param data   Buffer con bytes CBOR del payload PUT
 * @param len    Longitud del buffer
 * @return       CoreconfValueT (CORECONF_HASHMAP), o NULL si error
 */
CoreconfValueT *parse_put_request(const uint8_t *data, size_t len);

/* =========================================================================
 * Funciones de cliente
 * ========================================================================= */

/**
 * @brief Crear payload PUT para enviar (lado cliente)
 *
 * Serializa el datastore completo como CBOR (CF=142):
 *   { SID1: val1, SID2: val2, ... }
 *
 * Idéntico en formato a create_get_response() — mismo CBOR.
 * La diferencia es el uso: este se envía en un PUT para REEMPLAZAR.
 *
 * @param buffer       Buffer de salida para los bytes CBOR
 * @param buffer_size  Tamaño del buffer
 * @param datastore    CoreconfValueT (CORECONF_HASHMAP) con el nuevo estado completo
 * @return             Bytes escritos, o 0 si error
 */
size_t create_put_request(uint8_t *buffer, size_t buffer_size,
                           const CoreconfValueT *datastore);

#endif /* PUT_H */

/**
 * ipatch.h - Operación iPATCH de CORECONF (RFC draft-ietf-core-comi §4.5)
 *
 * RFC Reference: draft-ietf-core-comi
 * Sección: 4.5 - iPATCH operation
 *
 * Descripción:
 *   iPATCH permite actualizar PARCIALMENTE el datastore de un dispositivo.
 *   Solo se actualizan los SIDs presentes en el payload; el resto queda intacto.
 *   Es la operación de escritura eficiente de CORECONF (vs PUT que sobrescribe todo).
 *
 * Flujo RFC §4.5:
 *   Cliente                               Servidor
 *     |  iPATCH /c                           |
 *     |  Content-Format: 141                 |
 *     |  Payload: { SID: new_val, ... }      |  ← solo los SIDs a modificar
 *     |------------------------------------->|
 *     |                                      |
 *     |  2.04 Changed                        |  ← sin payload si éxito
 *     |<-------------------------------------|
 *
 * Content-Formats (RFC §2.3):
 *   - Request:  CF=141  application/yang-patch+cbor; id=sid
 *   - Response: (ninguno en éxito) / CF=142 en error (RFC §6)
 *
 * Payload iPATCH (CF=141):
 *   Un único mapa CBOR con los SIDs a actualizar:
 *     { SID1: new_value, SID2: new_value, ... }
 *
 *   Ejemplo: actualizar solo la temperatura (SID 20):
 *     { 20: 25.3 }
 *
 *   Ejemplo: actualizar temperatura y unidad (SIDs 20 y 21):
 *     { 20: 18.0, 21: "fahrenheit" }
 *
 * Diferencias con otras operaciones CORECONF:
 *   - GET:    sin payload request → responde mapa completo (CF=142)
 *   - FETCH:  lista de SIDs a leer (CF=141) → responde solo esos (CF=142)
 *   - iPATCH: mapa {SID:valor} a ESCRIBIR (CF=141) → 2.04 Changed
 *   - PUT:    reemplaza el datastore completo (CF=142)
 *
 * Errores RFC §6 (con payload CBOR CF=142):
 *   - 4.00 Bad Request:      CBOR malformado → error-tag: malformed-message (1012)
 *   - 4.22 Unprocessable:    valor inválido para el SID → error-tag: invalid-value (1011)
 *   - 5.00 Internal Error:   fallo al aplicar el parche → error-tag: operation-failed (1019)
 *
 * Author: Generated following get.h / fetch.h pattern
 */

#ifndef IPATCH_H
#define IPATCH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "coreconfTypes.h"

/* =========================================================================
 * Content-Formats para iPATCH (RFC draft-ietf-core-comi §2.3 / RFC 9254)
 * ========================================================================= */

/** CF=141: application/yang-patch+cbor; id=sid  (iPATCH request payload) */
#define COAP_MEDIA_TYPE_YANG_PATCH_CBOR  141

/** CF=142: application/yang-data+cbor; id=sid   (GET/FETCH response & error payloads) */
#define COAP_MEDIA_TYPE_YANG_DATA_CBOR   142

/* =========================================================================
 * Funciones de servidor
 * ========================================================================= */

/**
 * @brief Parsear payload iPATCH recibido (lado servidor)
 *
 * Decodifica el mapa CBOR del request iPATCH:
 *   CF=141  Payload: { SID1: new_val, SID2: new_val, ... }
 *
 * El resultado es un CORECONF_HASHMAP con los SIDs a actualizar.
 * Usar junto a apply_ipatch() para aplicar el parche al datastore.
 *
 * ⚠️  El llamador DEBE liberar la memoria con freeCoreconf(result, true)
 *
 * @param data   Buffer con bytes CBOR del payload iPATCH
 * @param len    Longitud del buffer
 * @return       CoreconfValueT (CORECONF_HASHMAP), o NULL si error
 */
CoreconfValueT *parse_ipatch_request(const uint8_t *data, size_t len);

/**
 * @brief Aplicar parche iPATCH al datastore (actualización parcial)
 *
 * Itera los (SID, valor) del parche e inserta/sobrescribe en el datastore.
 * Los SIDs NO presentes en el parche quedan sin cambios.
 *
 * Ejemplo:
 *   Datastore antes:  { 10: "temperature", 20: 22.5, 21: "celsius" }
 *   Parche iPATCH:    { 20: 25.0 }
 *   Datastore después:{ 10: "temperature", 20: 25.0, 21: "celsius" }
 *
 * @param datastore  CoreconfValueT (CORECONF_HASHMAP) destino — se modifica in-place
 * @param patch      CoreconfValueT (CORECONF_HASHMAP) con las actualizaciones
 * @return           0 en éxito, -1 si algún parámetro es inválido
 */
int apply_ipatch(CoreconfValueT *datastore, const CoreconfValueT *patch);

/* =========================================================================
 * Funciones de cliente
 * ========================================================================= */

/**
 * @brief Crear payload iPATCH para enviar (lado cliente)
 *
 * Serializa un mapa CORECONF como payload iPATCH (CF=141):
 *   { SID1: new_val, SID2: new_val, ... }
 *
 * Uso típico del cliente:
 *   1. Crear CoreconfValueT hashmap con los SIDs a actualizar
 *   2. Llamar create_ipatch_request() para obtener los bytes CBOR
 *   3. Enviar con iPATCH /c CF=141
 *
 * Ejemplo:
 *   CoreconfValueT *patch = createCoreconfHashmap();
 *   insertCoreconfHashMap(patch->data.map_value, 20, createCoreconfReal(25.0));
 *   uint8_t buf[256];
 *   size_t len = create_ipatch_request(buf, sizeof(buf), patch);
 *
 * @param buffer       Buffer de salida para los bytes CBOR
 * @param buffer_size  Tamaño del buffer
 * @param patch        CoreconfValueT (CORECONF_HASHMAP) con los SIDs a actualizar
 * @return             Bytes escritos, o 0 en error
 */
size_t create_ipatch_request(uint8_t *buffer, size_t buffer_size,
                              const CoreconfValueT *patch);

#endif /* IPATCH_H */

/**
 * ipatch.c - Implementación de la operación iPATCH de CORECONF
 *
 * RFC Reference: draft-ietf-core-comi
 * Sección: 4.5 - iPATCH operation
 *
 * Flujo CoAP:
 *   iPATCH /c  CF=141 { SID: new_val, ... }  →  2.04 Changed
 *
 * La operación iPATCH actualiza PARCIALMENTE el datastore:
 *   - Solo se modifican los SIDs presentes en el payload
 *   - Los SIDs no incluidos conservan su valor anterior
 *
 * Funciones exportadas (ver ipatch.h):
 *   - parse_ipatch_request()  → servidor: decodifica el payload CBOR del request
 *   - apply_ipatch()          → servidor: aplica el parche al datastore existente
 *   - create_ipatch_request() → cliente:  serializa el parche como CBOR
 *
 * Author: Generated following get.c pattern
 */

#include "../include/ipatch.h"
#include "../include/serialization.h"
#include "../include/coreconfManipulation.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* =========================================================================
 * Contexto interno para apply_ipatch usando iterateCoreconfHashMap
 * ========================================================================= */
typedef struct {
    CoreconfHashMapT *target;   /* hashmap del datastore a actualizar */
    int              error;     /* != 0 si hubo un fallo durante la iteración */
} ApplyPatchCtx;

/* Callback para iterateCoreconfHashMap: inserta/sobreescribe un SID en el datastore */
static void apply_one_sid(CoreconfObjectT *obj, void *udata) {
    ApplyPatchCtx *ctx = (ApplyPatchCtx *)udata;
    if (!obj || !obj->value) return;

    /* Hacer copia profunda del valor para que el datastore sea independiente del parche */
    CoreconfValueT *copy = malloc(sizeof(CoreconfValueT));
    if (!copy) { ctx->error = 1; return; }
    memcpy(copy, obj->value, sizeof(CoreconfValueT));

    /* Copia profunda de strings */
    if (obj->value->type == CORECONF_STRING && obj->value->data.string_value) {
        copy->data.string_value = strdup(obj->value->data.string_value);
        if (!copy->data.string_value) { free(copy); ctx->error = 1; return; }
    }

    /* insertCoreconfHashMap sobrescribe si la clave ya existe */
    if (insertCoreconfHashMap(ctx->target, obj->key, copy) != 0) {
        ctx->error = 1;
    }
}

/* =========================================================================
 * parse_ipatch_request - Decodificar payload iPATCH (lado servidor)
 *
 * El servidor recibe un payload CBOR con CF=141 que contiene un mapa:
 *   { SID1: new_val1, SID2: new_val2, ... }
 *
 * El formato es idéntico al de la respuesta GET (CF=142), solo cambia
 * el Content-Format header del CoAP option — el CBOR es el mismo.
 *
 * Delega a cborToCoreconfValue() que ya maneja todos los tipos CBOR.
 *
 * ⚠️  El llamador DEBE liberar con freeCoreconf(result, true)
 *
 * @param data  Buffer con bytes CBOR del request iPATCH
 * @param len   Longitud del buffer
 * @return      CoreconfValueT (CORECONF_HASHMAP), o NULL si error
 * ========================================================================= */
CoreconfValueT *parse_ipatch_request(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return NULL;

    /* zcbor_new_decode_state:
     *   state, n_states, payload, payload_len, max_elements, limits, n_limits
     * max_elements=1: el payload raíz es un único mapa CBOR
     */
    zcbor_state_t state[5];
    zcbor_new_decode_state(state, 5, data, len, 1, NULL, 0);

    return cborToCoreconfValue(state, 0);
}

/* =========================================================================
 * apply_ipatch - Aplicar parche iPATCH al datastore (actualización parcial)
 *
 * Para cada (SID, valor) del parche:
 *   - Si el SID ya existe en el datastore → sobreescribe su valor
 *   - Si el SID no existe               → inserta el nuevo par
 *
 * Los SIDs del datastore no presentes en el parche no se modifican.
 *
 * @param datastore  CORECONF_HASHMAP destino (se modifica in-place)
 * @param patch      CORECONF_HASHMAP con las actualizaciones del cliente
 * @return           0 en éxito, -1 si parámetros inválidos o fallo interno
 * ========================================================================= */
int apply_ipatch(CoreconfValueT *datastore, const CoreconfValueT *patch)
{
    if (!datastore || datastore->type != CORECONF_HASHMAP) return -1;
    if (!patch     || patch->type     != CORECONF_HASHMAP) return -1;

    CoreconfHashMapT *target_map = datastore->data.map_value;
    CoreconfHashMapT *patch_map  = patch->data.map_value;
    if (!target_map || !patch_map) return -1;

    ApplyPatchCtx ctx = { .target = target_map, .error = 0 };
    iterateCoreconfHashMap(patch_map, &ctx, apply_one_sid);

    return ctx.error ? -1 : 0;
}

/* =========================================================================
 * create_ipatch_request - Crear payload iPATCH para enviar (lado cliente)
 *
 * Serializa el mapa de parche como un único mapa CBOR (CF=141):
 *   { SID1: new_val1, SID2: new_val2, ... }
 *
 * Idéntico en formato a create_get_response() — el CBOR es el mismo,
 * solo cambia el Content-Format CoAP (141 vs 142).
 *
 * @param buffer       Buffer de salida para los bytes CBOR
 * @param buffer_size  Tamaño del buffer
 * @param patch        CORECONF_HASHMAP con los SIDs a actualizar
 * @return             Bytes escritos, o 0 si error
 * ========================================================================= */
size_t create_ipatch_request(uint8_t *buffer, size_t buffer_size,
                              const CoreconfValueT *patch)
{
    if (!buffer || !patch || patch->type != CORECONF_HASHMAP) return 0;

    CoreconfHashMapT *map = patch->data.map_value;
    if (!map) return 0;

    size_t entry_count = map->size;

    zcbor_state_t state[5];
    zcbor_new_encode_state(state, 5, buffer, buffer_size, 0);

    if (entry_count == 0) {
        /* Parche vacío: mapa CBOR vacío {} */
        if (!zcbor_map_start_encode(state, 0)) return 0;
        if (!zcbor_map_end_encode(state, 0))   return 0;
        return (size_t)(state[0].payload - buffer);
    }

    /* Abrir mapa CBOR definitivo con entry_count pares */
    if (!zcbor_map_start_encode(state, entry_count)) return 0;

    /* Iterar todos los slots de la hash table */
    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
        CoreconfObjectT *obj = map->table[t];
        while (obj) {
            /* Clave: SID como uint64 */
            if (!zcbor_uint64_put(state, obj->key)) return 0;

            /* Valor: delegar a coreconfToCBOR (maneja todos los tipos) */
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

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
#include "../include/coreconfTypes.h"
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

    /* draft §3.2.3: si el valor del parche es null → eliminar ese SID del datastore */
    if (obj->value->type == CORECONF_NULL) {
        deleteFromCoreconfHashMap(ctx->target, obj->key);
        return;
    }

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
 * Helpers para operaciones sobre listas (instance-identifiers)
 * ========================================================================= */

/* Busca en un array CORECONF la primera entrada que contenga key_str como
 * valor string en cualquiera de sus campos. Retorna el índice o -1. */
static int find_list_entry_by_key(CoreconfValueT *arr, const char *key_str) {
    if (!arr || arr->type != CORECONF_ARRAY) return -1;
    CoreconfArrayT *a = arr->data.array_value;
    for (size_t i = 0; i < a->size; i++) {
        CoreconfValueT *entry = &a->elements[i];
        if (entry->type != CORECONF_HASHMAP) continue;
        CoreconfHashMapT *m = entry->data.map_value;
        for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
            CoreconfObjectT *obj = m->table[t];
            while (obj) {
                if (obj->value && obj->value->type == CORECONF_STRING &&
                    strcmp(obj->value->data.string_value, key_str) == 0) {
                    return (int)i;
                }
                obj = obj->next;
            }
        }
    }
    return -1;
}

/* Elimina el elemento en idx del array (desplaza los restantes). */
static void remove_array_entry(CoreconfValueT *arr, size_t idx) {
    CoreconfArrayT *a = arr->data.array_value;
    if (idx >= a->size) return;
    freeCoreconf(&a->elements[idx], false);
    for (size_t i = idx; i < a->size - 1; i++)
        a->elements[i] = a->elements[i + 1];
    a->size--;
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
    zcbor_state_t state[8];
    zcbor_new_decode_state(state, 8, data, len, 1, NULL, 0);

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

    zcbor_state_t state[8];
    zcbor_new_encode_state(state, 8, buffer, buffer_size, 1);

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
/* =========================================================================
 * apply_ipatch_raw - Aplicar parche iPATCH con soporte de instance-identifiers
 *
 * Decodifica directamente el buffer CBOR y aplica los cambios al datastore.
 * Soporta claves uint (SID simple) y claves array [SID, key_str] (lista).
 *
 * { SID: valor }             → actualizar/borrar nodo simple
 * { [SID, "key"]: null }     → borrar instancia de lista
 * { [SID, "key"]: {mapa} }   → añadir/actualizar instancia de lista
 *
 * @param datastore  CORECONF_HASHMAP del servidor (modificado in-place)
 * @param data       Buffer CBOR del payload iPATCH (CF=142)
 * @param len        Longitud del buffer
 * @return           Número de entradas aplicadas, o -1 si error
 * ========================================================================= */
int apply_ipatch_raw(CoreconfValueT *datastore, const uint8_t *data, size_t len)
{
    if (!datastore || datastore->type != CORECONF_HASHMAP || !data || len == 0) return -1;

    int count = 0;
    const uint8_t *ptr     = data;
    const uint8_t *end_ptr = data + len;

    /* cbor-seq: puede haber varios mapas concatenados (draft §3.2.3) */
    while (ptr < end_ptr) {
        size_t remaining = (size_t)(end_ptr - ptr);
        zcbor_state_t state[8];
        zcbor_new_decode_state(state, 8, ptr, remaining, 1, NULL, 0);

        if (!zcbor_map_start_decode(state)) break;

        while (!zcbor_array_at_end(state)) {
            zcbor_major_type_t key_type = ZCBOR_MAJOR_TYPE(*state->payload);

            if (key_type == ZCBOR_MAJOR_TYPE_PINT) {
                /* ── Clave uint: { SID: valor } ── */
                uint64_t sid = 0;
                if (!zcbor_uint64_decode(state, &sid)) goto next_map;
                CoreconfValueT *val = cborToCoreconfValue(state, 0);
                if (!val) goto next_map;

                if (val->type == CORECONF_NULL) {
                    deleteFromCoreconfHashMap(datastore->data.map_value, sid);
                    freeCoreconf(val, true);
                    printf("[iPATCH] deleted SID %"PRIu64"\n", sid);
                } else {
                    /* Si el valor existente es una lista y el nuevo valor es un mapa,
                     * envolverlo en un array para mantener el tipo lista */
                    CoreconfValueT *existing = getCoreconfHashMap(datastore->data.map_value, sid);
                    if (existing && existing->type == CORECONF_ARRAY && val->type == CORECONF_HASHMAP) {
                        CoreconfValueT *arr = createCoreconfArray();
                        addToCoreconfArray(arr, val);
                        free(val);
                        val = arr;
                    }
                    insertCoreconfHashMap(datastore->data.map_value, sid, val);
                    printf("[iPATCH] updated SID %"PRIu64"\n", sid);
                }
                count++;

            } else if (key_type == ZCBOR_MAJOR_TYPE_LIST) {
                /* ── Clave array: { [SID, "key"]: valor } (instance-identifier) ── */
                if (!zcbor_list_start_decode(state)) goto next_map;

                uint64_t sid = 0;
                if (!zcbor_uint64_decode(state, &sid)) { zcbor_list_end_decode(state); goto next_map; }

                char key_str[128] = {0};
                if (ZCBOR_MAJOR_TYPE(*state->payload) == ZCBOR_MAJOR_TYPE_TSTR) {
                    struct zcbor_string zs;
                    if (!zcbor_tstr_decode(state, &zs)) { zcbor_list_end_decode(state); goto next_map; }
                    size_t klen = zs.len < 127 ? zs.len : 127;
                    memcpy(key_str, zs.value, klen);
                    key_str[klen] = '\0';
                } else {
                    zcbor_list_end_decode(state);
                    goto next_map;
                }

                if (!zcbor_list_end_decode(state)) goto next_map;

                CoreconfValueT *val = cborToCoreconfValue(state, 0);
                if (!val) goto next_map;

                CoreconfValueT *list = getCoreconfHashMap(datastore->data.map_value, sid);
                if (list && list->type == CORECONF_ARRAY) {
                    int idx = find_list_entry_by_key(list, key_str);
                    if (val->type == CORECONF_NULL) {
                        if (idx >= 0) {
                            remove_array_entry(list, (size_t)idx);
                            printf("[iPATCH] deleted list entry [%"PRIu64", \"%s\"]\n", sid, key_str);
                        }
                        freeCoreconf(val, true);
                    } else {
                        if (idx >= 0) {
                            /* Reemplazar entrada existente */
                            freeCoreconf(&list->data.array_value->elements[idx], false);
                            list->data.array_value->elements[idx] = *val;
                            free(val);
                            printf("[iPATCH] updated list entry [%"PRIu64", \"%s\"]\n", sid, key_str);
                        } else {
                            /* Añadir nueva entrada */
                            addToCoreconfArray(list, val);
                            free(val);
                            printf("[iPATCH] added list entry [%"PRIu64", \"%s\"]\n", sid, key_str);
                        }
                    }
                    count++;
                } else {
                    freeCoreconf(val, true);
                }

            } else {
                goto next_map;
            }
        }
        next_map:
        zcbor_map_end_decode(state);
        /* Avanzar ptr al siguiente ítem del cbor-seq */
        ptr = state[0].payload;
    }
    return count;
}

/* =========================================================================
 * create_ipatch_iid_request - Crear payload iPATCH con instance-identifier
 *
 * Encoda:  { [SID, "key_str"]: valor }
 *
 * Si val == NULL → CBOR null (borrar instancia)
 *
 * @param buffer       Buffer de salida
 * @param buffer_size  Tamaño del buffer
 * @param sid          SID de la lista YANG (p.ej. 1756 = ntp/server)
 * @param key_str      Valor de la clave (p.ej. "tac.nrc.ca")
 * @param val          Valor a escribir, o NULL para null (borrar)
 * @return             Bytes escritos, o 0 si error
 * ========================================================================= */
size_t create_ipatch_iid_request(uint8_t *buffer, size_t buffer_size,
                                  uint64_t sid, const char *key_str,
                                  CoreconfValueT *val)
{
    if (!buffer || !key_str) return 0;

    zcbor_state_t state[8];
    zcbor_new_encode_state(state, 8, buffer, buffer_size, 1);

    if (!zcbor_map_start_encode(state, 1)) return 0;

    /* Clave: [SID, "key_str"] */
    if (!zcbor_list_start_encode(state, 2)) return 0;
    if (!zcbor_uint64_put(state, sid)) return 0;
    struct zcbor_string zk = { .value = (const uint8_t *)key_str, .len = strlen(key_str) };
    if (!zcbor_tstr_encode(state, &zk)) return 0;
    if (!zcbor_list_end_encode(state, 2)) return 0;

    /* Valor */
    if (val) {
        if (!coreconfToCBOR(val, state)) return 0;
    } else {
        if (!zcbor_nil_put(state, NULL)) return 0;
    }

    if (!zcbor_map_end_encode(state, 1)) return 0;

    return (size_t)(state[0].payload - buffer);
}

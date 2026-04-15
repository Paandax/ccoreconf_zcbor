

/* CORECONF GET response encoding and payload decoding helpers. */
#include "../include/get.h"
#include "../include/serialization.h"
#include "../include/coreconfManipulation.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


size_t create_get_response(uint8_t *buffer, size_t buffer_size,
                            CoreconfValueT *datastore)
{
    
    if (!buffer || !datastore || datastore->type != CORECONF_HASHMAP) {
        return 0;
    }

    CoreconfHashMapT *map = datastore->data.map_value;
    if (!map) {
        return 0;
    }

    
    size_t entry_count = map->size;
    if (entry_count == 0) {
        
        zcbor_state_t state[8];
        zcbor_new_encode_state(state, 8, buffer, buffer_size, 1);
        if (!zcbor_map_start_encode(state, 0)) return 0;
        if (!zcbor_map_end_encode(state, 0)) return 0;
        return (size_t)(state[0].payload - buffer);
    }

    
    zcbor_state_t state[8];
    zcbor_new_encode_state(state, 8, buffer, buffer_size, 1);

    
    if (!zcbor_map_start_encode(state, entry_count)) {
        return 0;
    }

    
    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
        CoreconfObjectT *obj = map->table[t];

        while (obj) {
            
            if (!zcbor_uint64_put(state, obj->key)) {
                return 0;
            }

            
            if (obj->value) {
                if (!coreconfToCBOR(obj->value, state)) {
                    return 0;
                }
            } else {
                
                if (!zcbor_nil_put(state, NULL)) {
                    return 0;
                }
            }

            obj = obj->next;  
        }
    }

    
    if (!zcbor_map_end_encode(state, entry_count)) {
        return 0;
    }

    
    return (size_t)(state[0].payload - buffer);
}


CoreconfValueT *parse_get_response(const uint8_t *data, size_t len)
{
    
    if (!data || len == 0) {
        return NULL;
    }

    
    zcbor_state_t state[8];
    zcbor_new_decode_state(state, 8, data, len, 1, NULL, 0);

    
    return cborToCoreconfValue(state, 0);
}

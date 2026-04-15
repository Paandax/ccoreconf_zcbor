

/* CORECONF FETCH request/response encode/decode and IID resolution logic. */
#include "fetch.h"
#include "serialization.h"
#include "coreconfManipulation.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"
#include <stdlib.h>   
#include <stdint.h>   
#include <string.h>   



size_t create_fetch_request(uint8_t *buffer, size_t buffer_size,
                             const uint64_t *sids, size_t sid_count) {
    
    if (!buffer || !sids || sid_count == 0) {
        return 0;  
    }
    
    size_t offset = 0;  
    zcbor_state_t state[5];  
    
    
    for (size_t i = 0; i < sid_count; i++) {
        
        
        
        
        zcbor_new_encode_state(state, 5, buffer + offset, buffer_size - offset, 0);
        
        
        
        
        
        
        if (!zcbor_uint64_put(state, sids[i])) {
            return 0;  
        }
        
        
        
        
        offset += (state[0].payload - (buffer + offset));
    }
    
    return offset;  
}



size_t create_fetch_request_with_iids(uint8_t *buffer, size_t buffer_size,
                                       const InstanceIdentifier *iids, size_t iid_count) {
    if (!buffer || !iids || iid_count == 0) {
        return 0;
    }
    
    size_t offset = 0;
    zcbor_state_t state[5];
    
    
    for (size_t i = 0; i < iid_count; i++) {
        zcbor_new_encode_state(state, 5, buffer + offset, buffer_size - offset, 0);
        
        
        
        switch (iids[i].type) {
            case IID_SIMPLE:
                
                
                if (!zcbor_uint64_put(state, iids[i].sid)) {
                    return 0;
                }
                break;
                
            case IID_WITH_STR_KEY:
                
                
                if (!zcbor_list_start_encode(state, 2)) return 0;  
                if (!zcbor_uint64_put(state, iids[i].sid)) return 0;  
                if (!zcbor_tstr_put_term(state, iids[i].key.str_key, SIZE_MAX)) return 0;  
                if (!zcbor_list_end_encode(state, 2)) return 0;  
                break;
                
            case IID_WITH_INT_KEY:
                
                
                if (!zcbor_list_start_encode(state, 2)) return 0;  
                if (!zcbor_uint64_put(state, iids[i].sid)) return 0;  
                if (!zcbor_int64_put(state, iids[i].key.int_key)) return 0;  
                if (!zcbor_list_end_encode(state, 2)) return 0;  
                break;
                
            default:
                
                return 0;
        }
        
        offset += (state[0].payload - (buffer + offset));
    }
    
    return offset;
}



size_t create_fetch_response(uint8_t *buffer, size_t buffer_size,
                              CoreconfValueT *data_source,
                              const uint64_t *sids, size_t sid_count) {
    if (!buffer || !sids || sid_count == 0) {
        return 0;
    }
    
    size_t offset = 0;
    zcbor_state_t state[5];
    
    
    for (size_t i = 0; i < sid_count; i++) {
        zcbor_new_encode_state(state, 5, buffer + offset, buffer_size - offset, 0);
        
        
        
        CoreconfValueT *value = NULL;
        if (data_source && data_source->type == CORECONF_HASHMAP) {
            value = getCoreconfHashMap(data_source->data.map_value, sids[i]);
        }
        
        
        if (!zcbor_map_start_encode(state, 1)) return 0;
        
        if (!zcbor_uint64_put(state, sids[i])) return 0;
        
        if (value) {
            
            if (!coreconfToCBOR(value, state)) return 0;
        } else {
            
            if (!zcbor_nil_put(state, NULL)) return 0;
        }
        
        if (!zcbor_map_end_encode(state, 1)) return 0;
        
        offset += (state[0].payload - (buffer + offset));
    }
    
    return offset;
}



CoreconfValueT* fetch_value_by_iid(CoreconfValueT *data_source, 
                                    const InstanceIdentifier *iid) {
    
    if (!data_source || !iid || data_source->type != CORECONF_HASHMAP) {
        return NULL;
    }
    
    
    CoreconfHashMapT *map = data_source->data.map_value;
    
    
    
    CoreconfValueT *value = getCoreconfHashMap(map, iid->sid);
    
    if (!value) {
        return NULL;  
    }
    
    
    if (iid->type == IID_SIMPLE) {
        return value;  
    }
    
    
    
    if (iid->type == IID_WITH_STR_KEY) {
        
        
        
        if (value->type == CORECONF_ARRAY) {
            
            CoreconfArrayT *arr = value->data.array_value;
            
            
            for (size_t i = 0; i < arr->size; i++) {
                CoreconfValueT *elem = &arr->elements[i];
                
                
                
                if (elem->type == CORECONF_HASHMAP) {
                    CoreconfHashMapT *elem_map = elem->data.map_value;
                    
                    
                    
                    
                    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
                        CoreconfObjectT *obj = elem_map->table[t];
                        
                        
                        while (obj) {
                            
                            if (obj->value && obj->value->type == CORECONF_STRING) {
                                
                                if (strcmp(obj->value->data.string_value, 
                                          iid->key.str_key) == 0) {
                                    
                                    return elem;
                                }
                            }
                            obj = obj->next;  
                        }
                    }
                }
            }
            
            return NULL;
        }
        
        
        
        return NULL;
    }
    
    
    
    if (iid->type == IID_WITH_INT_KEY) {
        
        if (value->type == CORECONF_ARRAY) {
            CoreconfArrayT *arr = value->data.array_value;
            
            
            
            if (iid->key.int_key >= 0 &&  
                (size_t)iid->key.int_key < arr->size) {  
                
                
                return &arr->elements[iid->key.int_key];
            }
        }
        
        return NULL;
    }
    
    
    return NULL;
}



size_t create_fetch_response_iids(uint8_t *buffer, size_t buffer_size,
                                   CoreconfValueT *data_source,
                                   const InstanceIdentifier *iids, size_t iid_count) {
    if (!buffer || !iids || iid_count == 0) {
        return 0;
    }
    
    size_t offset = 0;
    zcbor_state_t state[5];
    
    
    for (size_t i = 0; i < iid_count; i++) {
        zcbor_new_encode_state(state, 5, buffer + offset, buffer_size - offset, 0);
        
        
        
        CoreconfValueT *value = fetch_value_by_iid(data_source, &iids[i]);
        
        
        if (!zcbor_map_start_encode(state, 1)) return 0;
        
        if (!zcbor_uint64_put(state, iids[i].sid)) return 0;
        
        if (value) {
            
            if (!coreconfToCBOR(value, state)) return 0;
        } else {
            
            if (!zcbor_nil_put(state, NULL)) return 0;
        }
        
        if (!zcbor_map_end_encode(state, 1)) return 0;
        
        offset += (state[0].payload - (buffer + offset));
    }
    
    return offset;
}



bool parse_fetch_request(const uint8_t *cbor_data, size_t cbor_size,
                          uint64_t **out_sids, size_t *out_count) {
    if (!cbor_data || !out_sids || !out_count) {
        return false;
    }
    
    
    size_t count = 0;
    size_t offset = 0;
    zcbor_state_t state[5];
    
    
    while (offset < cbor_size) {
        zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
        
        uint64_t temp_sid;
        if (!zcbor_uint64_decode(state, &temp_sid)) {
            break;  
        }
        
        count++;  
        offset += (state[0].payload - (cbor_data + offset));  
    }
    
    
    if (count == 0) {
        return false;  
    }
    
    
    uint64_t *sids = (uint64_t *)malloc(sizeof(uint64_t) * count);
    if (!sids) {
        return false;  
    }
    
    
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



bool is_fetch_request(const uint8_t *cbor_data, size_t cbor_size) {
    if (!cbor_data || cbor_size == 0) {
        return false;
    }
    
    zcbor_state_t state[5];
    zcbor_new_decode_state(state, 5, cbor_data, cbor_size, 1, NULL, 0);
    
    
    uint64_t temp_sid;
    return zcbor_uint64_decode(state, &temp_sid);
    
    
    
}



bool parse_fetch_request_iids(const uint8_t *cbor_data, size_t cbor_size,
                                InstanceIdentifier **out_iids, size_t *out_count) {
    if (!cbor_data || !out_iids || !out_count) {
        return false;
    }
    
    
    size_t count = 0;
    size_t offset = 0;
    zcbor_state_t state[5];
    
    
    while (offset < cbor_size) {
        zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
        
        
        uint64_t temp_sid;
        if (zcbor_uint64_decode(state, &temp_sid)) {
            
            count++;
        } else {
            
            zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
            if (zcbor_list_start_decode(state)) {
                count++;
                
                
                
                int depth = 1;  
                while (depth > 0 && state[0].payload < cbor_data + cbor_size) {
                    uint8_t major = state[0].payload[0] >> 5;  
                    uint8_t additional = state[0].payload[0] & 0x1f;  
                    
                    
                    if (major == 4 || major == 5) {  
                        depth++;
                    } else if (additional == 31) {  
                        depth--;
                    }
                    
                    state[0].payload++;  
                }
                zcbor_list_end_decode(state);
            } else {
                break;  
            }
        }
        
        
        offset += (state[0].payload - (cbor_data + offset));
    }
    
    
    if (count == 0) {
        return false;
    }
    
    
    
    InstanceIdentifier *iids = (InstanceIdentifier *)calloc(count, sizeof(InstanceIdentifier));
    if (!iids) {
        return false;  
    }
    
    
    offset = 0;  
    for (size_t i = 0; i < count; i++) {
        zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
        
        uint64_t sid;
        
        
        if (zcbor_uint64_decode(state, &sid)) {
            
            iids[i].type = IID_SIMPLE;
            iids[i].sid = sid;
            iids[i].key.str_key = NULL;  
        } else {
            
            
            
            zcbor_new_decode_state(state, 5, cbor_data + offset, cbor_size - offset, 1, NULL, 0);
            
            
            if (!zcbor_list_start_decode(state)) {
                free_instance_identifiers(iids, i);  
                return false;
            }
            
            
            if (!zcbor_uint64_decode(state, &sid)) {
                free_instance_identifiers(iids, i);
                return false;
            }
            
            iids[i].sid = sid;  
            
            
            
            
            struct zcbor_string zstr;
            if (zcbor_tstr_decode(state, &zstr)) {
                
                iids[i].type = IID_WITH_STR_KEY;
                
                
                
                iids[i].key.str_key = (char *)malloc(zstr.len + 1);  
                if (!iids[i].key.str_key) {
                    free_instance_identifiers(iids, i);
                    return false;  
                }
                
                
                memcpy(iids[i].key.str_key, zstr.value, zstr.len);
                iids[i].key.str_key[zstr.len] = '\0';  
            } else {
                
                int64_t int_key;
                if (zcbor_int64_decode(state, &int_key)) {
                    
                    iids[i].type = IID_WITH_INT_KEY;
                    iids[i].key.int_key = int_key;
                } else {
                    
                    free_instance_identifiers(iids, i);
                    return false;
                }
            }
            
            
            if (!zcbor_list_end_decode(state)) {
                free_instance_identifiers(iids, i + 1);
                return false;
            }
        }
        
        
        offset += (state[0].payload - (cbor_data + offset));
    }
    
    
    *out_iids = iids;  
    *out_count = count;
    return true;  
}



void free_instance_identifiers(InstanceIdentifier *iids, size_t count) {
    if (!iids) return;  
    
    
    for (size_t i = 0; i < count; i++) {
        
        if (iids[i].type == IID_WITH_STR_KEY && iids[i].key.str_key) {
            free(iids[i].key.str_key);  
        }
    }
    
    
    free(iids);
    
    
    
}

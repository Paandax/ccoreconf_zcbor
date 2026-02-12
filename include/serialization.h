
#include "../coreconf_zcbor_generated/zcbor_common.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"
#include <stdio.h>
#include <stdbool.h>

#include "../include/coreconfTypes.h"
#include "hashmap.h"

void serializeCoreconfObject(CoreconfObjectT* object, void* state_);
bool coreconfToCBOR(CoreconfValueT* coreconfValue, zcbor_state_t* state);
CoreconfValueT* cborToCoreconfValue(zcbor_state_t* state, unsigned indent);

// Serialize KeyMappingHashMap to CBOR
bool keyMappingHashMapToCBOR(struct hashmap* keyMappingHashMap, zcbor_state_t* state);
// Deserialize KeyMappingHashMap from CBOR
struct hashmap* cborToKeyMappingHashMap(zcbor_state_t* state);

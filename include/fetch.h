#ifndef FETCH_H
#define FETCH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "coreconfTypes.h"

/**
 * @file fetch.h
 * @brief RFC 9254 Section 3.1.3 FETCH operation implementation
 * 
 * This module provides functions to create and process FETCH requests
 * and responses using CBOR sequences as defined in RFC 9254.
 */

/**
 * @brief Type of instance identifier
 */
typedef enum {
    IID_SIMPLE,          // Simple SID: uint
    IID_WITH_STR_KEY,    // SID with string key: [uint, tstr]
    IID_WITH_INT_KEY     // SID with int key: [uint, int]
} InstanceIdentifierType;

/**
 * @brief Instance identifier structure for FETCH requests
 * 
 * Supports both simple SIDs and SIDs with keys for accessing
 * list/array elements as per RFC 9254 3.1.3.
 */
typedef struct {
    InstanceIdentifierType type;
    uint64_t sid;
    union {
        char *str_key;    // For IID_WITH_STR_KEY
        int64_t int_key;  // For IID_WITH_INT_KEY
    } key;
} InstanceIdentifier;

/**
 * @brief Create a FETCH request with simple SIDs
 * 
 * Creates a CBOR sequence containing the requested SIDs.
 * Format: SID1, SID2, ..., SIDn (RFC 9254 3.1.3)
 * 
 * @param buffer Output buffer for the CBOR sequence
 * @param buffer_size Size of the output buffer
 * @param sids Array of SIDs to fetch
 * @param sid_count Number of SIDs in the array
 * @return Number of bytes written to buffer, or 0 on error
 */
size_t create_fetch_request(uint8_t *buffer, size_t buffer_size,
                             const uint64_t *sids, size_t sid_count);

/**
 * @brief Create a FETCH request with instance identifiers
 * 
 * Creates a CBOR sequence supporting both simple SIDs and SIDs with keys.
 * Format: IID1, IID2, ..., IIDn where IID can be:
 *   - uint (simple SID)
 *   - [uint, key] (SID with key for list access)
 * 
 * @param buffer Output buffer for the CBOR sequence
 * @param buffer_size Size of the output buffer
 * @param iids Array of instance identifiers
 * @param iid_count Number of identifiers in the array
 * @return Number of bytes written to buffer, or 0 on error
 */
size_t create_fetch_request_with_iids(uint8_t *buffer, size_t buffer_size,
                                       const InstanceIdentifier *iids, size_t iid_count);

/**
 * @brief Fetch a value using an instance identifier
 * 
 * Resolves an instance identifier to a value in the data source.
 * Supports:
 *   - Simple SID: returns getCoreconfHashMap(data_source, sid)
 *   - [SID, str_key]: searches in array for element with matching key
 *   - [SID, int_key]: accesses array element at index
 * 
 * @param data_source CoreconfValueT hashmap containing the data
 * @param iid Instance identifier to resolve
 * @return Pointer to the value, or NULL if not found (do not free)
 */
CoreconfValueT* fetch_value_by_iid(CoreconfValueT *data_source, 
                                    const InstanceIdentifier *iid);

/**
 * @brief Create a FETCH response from a data source (simple SIDs)
 * 
 * Creates a CBOR sequence of maps {SID: value} for each requested SID.
 * Format: {SID1: value1}, {SID2: value2}, ..., {SIDn: valuen}
 * 
 * SIDs not found in data_source will have null values.
 * 
 * @param buffer Output buffer for the CBOR sequence
 * @param buffer_size Size of the output buffer
 * @param data_source CoreconfValueT hashmap containing the data
 * @param sids Array of requested SIDs
 * @param sid_count Number of SIDs requested
 * @return Number of bytes written to buffer, or 0 on error
 */
size_t create_fetch_response(uint8_t *buffer, size_t buffer_size,
                              CoreconfValueT *data_source,
                              const uint64_t *sids, size_t sid_count);

/**
 * @brief Create a FETCH response using instance identifiers
 * 
 * Creates a CBOR sequence of maps {SID: value} for each instance identifier.
 * Supports keys for accessing array elements (RFC 9254 compliant).
 * 
 * @param buffer Output buffer for the CBOR sequence
 * @param buffer_size Size of the output buffer
 * @param data_source CoreconfValueT hashmap containing the data
 * @param iids Array of instance identifiers
 * @param iid_count Number of identifiers
 * @return Number of bytes written to buffer, or 0 on error
 */
size_t create_fetch_response_iids(uint8_t *buffer, size_t buffer_size,
                                   CoreconfValueT *data_source,
                                   const InstanceIdentifier *iids, size_t iid_count);

/**
 * @brief Parse a FETCH request from a CBOR sequence (simple SIDs only)
 * 
 * Parses a CBOR sequence of SIDs into an array.
 * Caller must free the returned array with free().
 * 
 * @param cbor_data Input CBOR sequence
 * @param cbor_size Size of the CBOR data
 * @param out_sids Pointer to receive the allocated SID array
 * @param out_count Pointer to receive the number of SIDs parsed
 * @return true on success, false on error
 */
bool parse_fetch_request(const uint8_t *cbor_data, size_t cbor_size,
                          uint64_t **out_sids, size_t *out_count);

/**
 * @brief Parse a FETCH request with instance identifiers
 * 
 * Parses a CBOR sequence that may contain:
 *   - Simple SIDs: uint
 *   - SIDs with keys: [uint, tstr] or [uint, int]
 * 
 * Caller must free the returned array and any string keys with:
 *   for (i = 0; i < count; i++) {
 *     if (iids[i].type == IID_WITH_STR_KEY && iids[i].key.str_key) {
 *       free(iids[i].key.str_key);
 *     }
 *   }
 *   free(iids);
 * 
 * @param cbor_data Input CBOR sequence
 * @param cbor_size Size of the CBOR data
 * @param out_iids Pointer to receive the allocated InstanceIdentifier array
 * @param out_count Pointer to receive the number of identifiers parsed
 * @return true on success, false on error
 */
bool parse_fetch_request_iids(const uint8_t *cbor_data, size_t cbor_size,
                                InstanceIdentifier **out_iids, size_t *out_count);

/**
 * @brief Free instance identifiers array
 * 
 * Frees an array of InstanceIdentifiers including any allocated string keys.
 * 
 * @param iids Array to free
 * @param count Number of elements
 */
void free_instance_identifiers(InstanceIdentifier *iids, size_t count);

/**
 * @brief Check if CBOR data is a FETCH request (sequence of uints)
 * 
 * @param cbor_data Input CBOR data
 * @param cbor_size Size of the CBOR data
 * @return true if data appears to be a FETCH request, false otherwise
 */
bool is_fetch_request(const uint8_t *cbor_data, size_t cbor_size);

#endif // FETCH_H

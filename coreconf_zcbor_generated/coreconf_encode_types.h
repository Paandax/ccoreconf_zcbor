/*
 * Generated using zcbor version 0.9.99
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 3
 */

#ifndef CORECONF_ENCODE_TYPES_H__
#define CORECONF_ENCODE_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

/** Which value for --default-max-qty this file was created with.
 *
 *  The define is used in the other generated file to do a build-time
 *  compatibility check.
 *
 *  See `zcbor --help` for more information about --default-max-qty
 */
#define DEFAULT_MAX_QTY 3

struct key_mapping_uint_l_r {
	uint32_t key_mapping_uint_l_key;
	uint32_t key_mapping_uint_l_uint[3];
	size_t key_mapping_uint_l_uint_count;
};

struct key_mapping {
	struct key_mapping_uint_l_r key_mapping_uint_l[3];
	size_t key_mapping_uint_l_count;
};

#ifdef __cplusplus
}
#endif

#endif /* CORECONF_ENCODE_TYPES_H__ */

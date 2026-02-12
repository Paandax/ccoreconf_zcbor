/*
 * Generated using zcbor version 0.9.99
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 3
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "zcbor_decode.h"
#include "coreconf_decode.h"
#include "zcbor_print.h"

#if DEFAULT_MAX_QTY != 3
#error "The type file was generated with a different default_max_qty than this file"
#endif

#define ZCBOR_CUSTOM_CAST_FP(func) _Generic((func), \
	bool(*)(zcbor_state_t *, struct key_mapping_uint_l_r *): ((zcbor_decoder_t *)func), \
	bool(*)(zcbor_state_t *, struct key_mapping *):          ((zcbor_decoder_t *)func), \
	default: ZCBOR_CAST_FP(func))

#define log_result(state, result, func) do { \
	if (!result) { \
		zcbor_trace_file(state); \
		zcbor_log("%s error: %s\r\n", func, zcbor_error_str(zcbor_peek_error(state))); \
	} else { \
		zcbor_log("%s success\r\n", func); \
	} \
} while(0)

static bool decode_repeated_key_mapping_uint_l(zcbor_state_t *state, struct key_mapping_uint_l_r *result);
static bool decode_key_mapping(zcbor_state_t *state, struct key_mapping *result);


static bool decode_repeated_key_mapping_uint_l(
		zcbor_state_t *state, struct key_mapping_uint_l_r *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_decode(state, (&(*result).key_mapping_uint_l_key))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(1, 3, &(*result).key_mapping_uint_l_uint_count, ZCBOR_CUSTOM_CAST_FP(zcbor_uint32_decode), state, (*&(*result).key_mapping_uint_l_uint), sizeof(uint32_t))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		zcbor_uint32_decode(state, (*&(*result).key_mapping_uint_l_uint));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_key_mapping(
		zcbor_state_t *state, struct key_mapping *result)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_decode(state) && ((zcbor_multi_decode(0, 3, &(*result).key_mapping_uint_l_count, ZCBOR_CUSTOM_CAST_FP(decode_repeated_key_mapping_uint_l), state, (*&(*result).key_mapping_uint_l), sizeof(struct key_mapping_uint_l_r))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_key_mapping_uint_l(state, (*&(*result).key_mapping_uint_l));
	}

	log_result(state, res, __func__);
	return res;
}



int cbor_decode_key_mapping(
		const uint8_t *payload, size_t payload_len,
		struct key_mapping *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[4];

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_key_mapping(states, result);
	}

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)ZCBOR_CUSTOM_CAST_FP(decode_key_mapping), sizeof(states) / sizeof(zcbor_state_t), 1);
}

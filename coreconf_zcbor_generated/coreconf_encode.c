/*
 * Generated using zcbor version 0.9.99
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 3
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "zcbor_encode.h"
#include "coreconf_encode.h"
#include "zcbor_print.h"

#if DEFAULT_MAX_QTY != 3
#error "The type file was generated with a different default_max_qty than this file"
#endif

#define ZCBOR_CUSTOM_CAST_FP(func) _Generic((func), \
	bool(*)(zcbor_state_t *, const struct key_mapping_uint_l_r *): ((zcbor_encoder_t *)func), \
	bool(*)(zcbor_state_t *, const struct key_mapping *):          ((zcbor_encoder_t *)func), \
	default: ZCBOR_CAST_FP(func))

#define log_result(state, result, func) do { \
	if (!result) { \
		zcbor_trace_file(state); \
		zcbor_log("%s error: %s\r\n", func, zcbor_error_str(zcbor_peek_error(state))); \
	} else { \
		zcbor_log("%s success\r\n", func); \
	} \
} while(0)

static bool encode_repeated_key_mapping_uint_l(zcbor_state_t *state, const struct key_mapping_uint_l_r *input);
static bool encode_key_mapping(zcbor_state_t *state, const struct key_mapping *input);


static bool encode_repeated_key_mapping_uint_l(
		zcbor_state_t *state, const struct key_mapping_uint_l_r *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = ((((zcbor_uint32_encode(state, (&(*input).key_mapping_uint_l_key))))
	&& (zcbor_list_start_encode(state, 3) && ((zcbor_multi_encode_minmax(1, 3, &(*input).key_mapping_uint_l_uint_count, ZCBOR_CUSTOM_CAST_FP(zcbor_uint32_encode), state, (*&(*input).key_mapping_uint_l_uint), sizeof(uint32_t))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 3))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		zcbor_uint32_encode(state, (*&(*input).key_mapping_uint_l_uint));
	}

	log_result(state, res, __func__);
	return res;
}

static bool encode_key_mapping(
		zcbor_state_t *state, const struct key_mapping *input)
{
	zcbor_log("%s\r\n", __func__);

	bool res = (((zcbor_map_start_encode(state, 3) && ((zcbor_multi_encode_minmax(0, 3, &(*input).key_mapping_uint_l_count, ZCBOR_CUSTOM_CAST_FP(encode_repeated_key_mapping_uint_l), state, (*&(*input).key_mapping_uint_l), sizeof(struct key_mapping_uint_l_r))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		encode_repeated_key_mapping_uint_l(state, (*&(*input).key_mapping_uint_l));
	}

	log_result(state, res, __func__);
	return res;
}



int cbor_encode_key_mapping(
		uint8_t *payload, size_t payload_len,
		const struct key_mapping *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[4];

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		encode_key_mapping(states, input);
	}

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)ZCBOR_CUSTOM_CAST_FP(encode_key_mapping), sizeof(states) / sizeof(zcbor_state_t), 1);
}

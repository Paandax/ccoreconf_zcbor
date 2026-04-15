#include "../include/post.h"

#include <stdlib.h>
#include <string.h>

#include "../include/serialization.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"

static bool decode_post_key(zcbor_state_t *state, PostItem *item) {
    zcbor_major_type_t key_type = ZCBOR_MAJOR_TYPE(*state[0].payload);

    if (key_type == ZCBOR_MAJOR_TYPE_PINT) {
        item->iid_type = POST_IID_SID;
        item->str_key = NULL;
        return zcbor_uint64_decode(state, &item->sid);
    }

    if (key_type == ZCBOR_MAJOR_TYPE_LIST) {
        struct zcbor_string key = {0};
        if (!zcbor_list_start_decode(state) ||
            !zcbor_uint64_decode(state, &item->sid) ||
            !zcbor_tstr_decode(state, &key) ||
            !zcbor_list_end_decode(state)) {
            return false;
        }

        item->iid_type = POST_IID_SID_STR_KEY;
        item->str_key = (char *)malloc(key.len + 1);
        if (!item->str_key) {
            return false;
        }

        memcpy(item->str_key, key.value, key.len);
        item->str_key[key.len] = '\0';
        return true;
    }

    return false;
}

static bool encode_post_key(zcbor_state_t *state, const PostItem *item) {
    if (item->iid_type == POST_IID_SID) {
        return zcbor_uint64_put(state, item->sid);
    }

    if (item->iid_type == POST_IID_SID_STR_KEY) {
        struct zcbor_string key = {
            .value = (const uint8_t *)item->str_key,
            .len = item->str_key ? strlen(item->str_key) : 0,
        };

        return item->str_key &&
               zcbor_list_start_encode(state, 2) &&
               zcbor_uint64_put(state, item->sid) &&
               zcbor_tstr_encode(state, &key) &&
               zcbor_list_end_encode(state, 2);
    }

    return false;
}

size_t create_post_items(uint8_t *buffer, size_t buffer_size,
                         const PostItem *items, size_t item_count) {
    if (!buffer || !items || item_count == 0) {
        return 0;
    }

    size_t offset = 0;
    for (size_t i = 0; i < item_count; i++) {
        zcbor_state_t state[12];
        zcbor_new_encode_state(state, 12, buffer + offset, buffer_size - offset, 0);

        if (!zcbor_map_start_encode(state, 1) ||
            !encode_post_key(state, &items[i]) ||
            !((items[i].value && coreconfToCBOR(items[i].value, state)) ||
              (!items[i].value && zcbor_nil_put(state, NULL))) ||
            !zcbor_map_end_encode(state, 1)) {
            return 0;
        }

        offset += (size_t)(state[0].payload - (buffer + offset));
    }

    return offset;
}

bool parse_post_items(const uint8_t *data, size_t len,
                      PostItem **out_items, size_t *out_count) {
    if (!data || len == 0 || !out_items || !out_count) {
        return false;
    }

    PostItem *items = NULL;
    size_t count = 0;
    const uint8_t *ptr = data;
    const uint8_t *end = data + len;

    while (ptr < end) {
        PostItem item = {0};
        zcbor_state_t state[12];
        zcbor_new_decode_state(state, 12, ptr, (size_t)(end - ptr), 1, NULL, 0);

        if (!zcbor_map_start_decode(state) ||
            zcbor_array_at_end(state) ||
            !decode_post_key(state, &item)) {
            free(item.str_key);
            free_post_items(items, count);
            return false;
        }

        item.value = cborToCoreconfValue(state, 0);
        if (!item.value ||
            !zcbor_array_at_end(state) ||
            !zcbor_map_end_decode(state)) {
            if (item.value) {
                freeCoreconf(item.value, true);
            }
            free(item.str_key);
            free_post_items(items, count);
            return false;
        }

        PostItem *new_items = realloc(items, (count + 1) * sizeof(PostItem));
        if (!new_items) {
            freeCoreconf(item.value, true);
            free(item.str_key);
            free_post_items(items, count);
            return false;
        }

        items = new_items;
        items[count++] = item;

        const uint8_t *new_ptr = state[0].payload;
        if (new_ptr <= ptr) {
            free_post_items(items, count);
            return false;
        }
        ptr = new_ptr;
    }

    if (count == 0) {
        return false;
    }

    *out_items = items;
    *out_count = count;
    return true;
}

void free_post_items(PostItem *items, size_t item_count) {
    if (!items) {
        return;
    }

    for (size_t i = 0; i < item_count; i++) {
        if (items[i].value) {
            freeCoreconf(items[i].value, true);
        }
        free(items[i].str_key);
    }

    free(items);
}

size_t create_post_request_rpc(uint8_t *buffer, size_t buffer_size,
                               uint64_t rpc_sid, CoreconfValueT *input) {
    PostItem item = {
        .iid_type = POST_IID_SID,
        .sid = rpc_sid,
        .str_key = NULL,
        .value = input,
    };

    return create_post_items(buffer, buffer_size, &item, 1);
}

size_t create_post_request_action_str(uint8_t *buffer, size_t buffer_size,
                                      uint64_t action_sid,
                                      const char *instance_key,
                                      CoreconfValueT *input) {
    if (!instance_key) {
        return 0;
    }

    PostItem item = {
        .iid_type = POST_IID_SID_STR_KEY,
        .sid = action_sid,
        .str_key = (char *)instance_key,
        .value = input,
    };

    return create_post_items(buffer, buffer_size, &item, 1);
}

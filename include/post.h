#ifndef POST_H
#define POST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "coreconfTypes.h"

typedef enum {
    POST_IID_SID,
    POST_IID_SID_STR_KEY,
} PostIidType;

typedef struct {
    PostIidType iid_type;
    uint64_t sid;
    char *str_key;
    CoreconfValueT *value;
} PostItem;

size_t create_post_items(uint8_t *buffer, size_t buffer_size,
                         const PostItem *items, size_t item_count);

bool parse_post_items(const uint8_t *data, size_t len,
                      PostItem **out_items, size_t *out_count);

void free_post_items(PostItem *items, size_t item_count);

size_t create_post_request_rpc(uint8_t *buffer, size_t buffer_size,
                               uint64_t rpc_sid, CoreconfValueT *input);

size_t create_post_request_action_str(uint8_t *buffer, size_t buffer_size,
                                      uint64_t action_sid,
                                      const char *instance_key,
                                      CoreconfValueT *input);

#endif


/* CORECONF DELETE query parsing and SID deletion helpers. */
#include "delete.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int parse_delete_query(const uint8_t *query, size_t qlen,
                       uint64_t *sids_out, int max_sids) {
    if (!query || qlen == 0 || !sids_out || max_sids <= 0) return 0;

    
    char buf[512];
    size_t cplen = qlen < sizeof(buf) - 1 ? qlen : sizeof(buf) - 1;
    memcpy(buf, query, cplen);
    buf[cplen] = '\0';

    int n = 0;
    char *tok = buf;
    char *end = buf + cplen;

    while (tok < end && n < max_sids) {
        
        char *eq = memchr(tok, '=', (size_t)(end - tok));
        if (!eq) break;
        
        size_t key_len = (size_t)(eq - tok);
        if (key_len == 1 && tok[0] == 'k') {
            char *val_start = eq + 1;
            char *amp = memchr(val_start, '&', (size_t)(end - val_start));
            char *val_end = amp ? amp : end;
            
            char saved = *val_end;
            *val_end = '\0';
            char *endptr;
            uint64_t sid = (uint64_t)strtoull(val_start, &endptr, 10);
            *val_end = saved;
            if (endptr != val_start) {
                sids_out[n++] = sid;
                printf("[delete] SID a eliminar: %llu\n", (unsigned long long)sid);
            }
            tok = amp ? amp + 1 : end;
        } else {
            
            char *amp = memchr(tok, '&', (size_t)(end - tok));
            tok = amp ? amp + 1 : end;
        }
    }
    return n;
}


int apply_delete(CoreconfHashMapT *map, const uint64_t *sids, int n_sids) {
    if (!map || !sids || n_sids <= 0) return 0;
    int deleted = 0;
    for (int i = 0; i < n_sids; i++) {
        if (deleteFromCoreconfHashMap(map, sids[i]) == 0) {
            printf("[delete] SID %llu eliminado\n", (unsigned long long)sids[i]);
            deleted++;
        } else {
            printf("[delete] SID %llu no encontrado\n", (unsigned long long)sids[i]);
        }
    }
    return deleted;
}

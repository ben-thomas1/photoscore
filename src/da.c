#include "photoscore/da.h"
#include "photoscore/alloc.h"
#include "photoscore/status.h"

#include <stdint.h>
#include <string.h>

#define DA_INITIAL_CAP 64

static ps_status_e da_alloc(ps_da *da, size_t cap) {
    if (cap > SIZE_MAX / da->elem_size) { return PS_ERR_NOMEM; }

    u8 *items = PS_REALLOC(da->items, cap * da->elem_size);
    if (items == NULL) { return PS_ERR_NOMEM; }

    da->items = items;
    da->cap   = cap;

    return PS_OK;
}

ps_status_e ps_da_init(ps_da *out, size_t elem_size, size_t cap) {
    if (out == NULL || elem_size == 0) { return PS_ERR_INVALID; }

    *out = (ps_da){ .items = NULL, .len = 0, .cap = 0, .elem_size = elem_size };

    if (cap == 0) { return PS_OK; }

    return da_alloc(out, cap);
}

ps_status_e ps_da_reserve(ps_da *da, size_t cap) {
    if (da == NULL || da->elem_size == 0) { return PS_ERR_INVALID; }
    if (cap <= da->cap) { return PS_OK; }

    return da_alloc(da, cap);
}

ps_status_e ps_da_push(ps_da *da, const void *elem) {
    if (da == NULL || elem == NULL || da->elem_size == 0) { return PS_ERR_INVALID; }

    if (da->len == da->cap) {
        // An element borrowed from this array moves when realloc moves it.
        size_t source_offset = SIZE_MAX;
        if (da->items != NULL) {
            uintptr_t base = (uintptr_t)da->items;
            uintptr_t source = (uintptr_t)elem;
            size_t used = da->len * da->elem_size;
            if (source >= base && source - base < used) {
                source_offset = source - base;
                if (source_offset > used - da->elem_size) { return PS_ERR_INVALID; }
            }
        }

        size_t cap;

        if (da->cap == 0) {
            cap = DA_INITIAL_CAP;
        } else {
            if (da->cap > SIZE_MAX / 2) { return PS_ERR_NOMEM; }
            cap = da->cap * 2;
        }

        ps_status_e s = da_alloc(da, cap);
        if (s != PS_OK) { return s; }
        if (source_offset != SIZE_MAX) { elem = da->items + source_offset; }
    }

    memcpy(da->items + (da->len * da->elem_size), elem, da->elem_size);
    da->len += 1;

    return PS_OK;
}

void *ps_da_at(const ps_da *da, size_t i) {
    if (da == NULL || i >= da->len) { return NULL; }

    return da->items + (i * da->elem_size);
}

void ps_da_clear(ps_da *da) {
    if (da == NULL) { return; }

    da->len = 0;
}

void ps_da_free(ps_da *da) {
    if (da == NULL) { return; }

    PS_FREE(da->items);
    *da = (ps_da){ .items = NULL, .len = 0, .cap = 0, .elem_size = da->elem_size };
}

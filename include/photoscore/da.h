#pragma once

#include "photoscore/status.h"
#include "photoscore/types.h"

#include <stddef.h>

// Generic growable array. Elements are elem_size bytes each, copied in by
// value on push. The array owns its spine only; if elements are pointers,
// whatever they point at is the caller's problem.
//
// Caller owns the struct, the module owns items.
typedef struct {
    u8     *items;
    size_t  len;
    size_t  cap;
    size_t  elem_size;
} ps_da;

ps_status_e ps_da_init(ps_da *out, size_t elem_size, size_t cap);
ps_status_e ps_da_reserve(ps_da *da, size_t cap);
ps_status_e ps_da_push(ps_da *da, const void *elem);

// Address of element i, or NULL if out of range.
void *ps_da_at(const ps_da *da, size_t i);

void ps_da_clear(ps_da *da);   // len = 0, capacity kept
void ps_da_free(ps_da *da);

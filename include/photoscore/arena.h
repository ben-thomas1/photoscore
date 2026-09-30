#pragma once

#include "photoscore/status.h"

#include <stddef.h>

// Bump allocator over a chain of blocks. Individual allocations are never
// freed; the whole region is reclaimed with _reset (keeping the blocks) or
// released with _free.
//
// Backs strings and per-photo scratch whose lifetime matches a phase of the
// run, so those call sites need no ownership bookkeeping.

typedef struct ps_arena_block ps_arena_block;

typedef struct {
    ps_arena_block *head;
    size_t          block_size;   // capacity of each new block
    size_t          used;         // bytes handed out
    size_t          reserved;     // bytes held across all blocks
} ps_arena;

// block_size of 0 selects the default.
ps_status_e ps_arena_init(ps_arena *out, size_t block_size);

// align must be a power of two. Returns NULL on overflow or allocation
// failure; the arena is left usable.
void *ps_arena_alloc(ps_arena *a, size_t size, size_t align);

// Overflow-checked n * elem_size allocation.
void *ps_arena_array(ps_arena *a, size_t n, size_t elem_size, size_t align);

// Copy s into the arena. Returns NULL on failure.
char *ps_arena_strdup(ps_arena *a, const char *s);

void ps_arena_reset(ps_arena *a);   // reuse every block from the start
void ps_arena_free(ps_arena *a);

#define PS_ARENA_NEW(a, T) ((T *)ps_arena_alloc((a), sizeof(T), alignof(T)))

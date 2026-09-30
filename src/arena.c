#include "photoscore/arena.h"
#include "photoscore/alloc.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <stdint.h>
#include <string.h>

#define ARENA_DEFAULT_BLOCK (64u * 1024u)

struct ps_arena_block {
    ps_arena_block *next;
    size_t          cap;
    size_t          used;
    u8              data[];
};

static size_t padding_for(const void *p, size_t align) {
    return (align - ((uintptr_t)p & (align - 1))) & (align - 1);
}

static bool is_pow2(size_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

static ps_arena_block *block_new(size_t cap) {
    if (cap > SIZE_MAX - sizeof(ps_arena_block)) { return NULL; }

    ps_arena_block *b = PS_MALLOC(sizeof(ps_arena_block) + cap);
    if (b == NULL) { return NULL; }

    b->next = NULL;
    b->cap  = cap;
    b->used = 0;

    return b;
}

ps_status_e ps_arena_init(ps_arena *out, size_t block_size) {
    if (out == NULL) { return PS_ERR_INVALID; }

    *out = (ps_arena){
        .head       = NULL,
        .block_size = (block_size == 0) ? ARENA_DEFAULT_BLOCK : block_size,
        .used       = 0,
        .reserved   = 0,
    };

    return PS_OK;
}

void *ps_arena_alloc(ps_arena *a, size_t size, size_t align) {
    if (a == NULL || !is_pow2(align)) { return NULL; }
    if (size == 0) { size = 1; }
    if (size > SIZE_MAX - align) { return NULL; }

    // Try the current block, then any older block with room. Blocks are few
    // and the head is almost always the one that fits.
    for (ps_arena_block *b = a->head; b != NULL; b = b->next) {
        size_t padding = padding_for(b->data + b->used, align);
        if (padding > b->cap - b->used) { continue; }
        size_t off = b->used + padding;

        if (off <= b->cap && b->cap - off >= size) {
            b->used  = off + size;
            a->used += size;
            return b->data + off;
        }
    }

    size_t needed = size + align - 1;
    size_t cap = (needed > a->block_size) ? needed : a->block_size;
    if (cap > SIZE_MAX - a->reserved) { return NULL; }

    ps_arena_block *b = block_new(cap);
    if (b == NULL) { return NULL; }

    b->next      = a->head;
    a->head      = b;
    a->reserved += cap;

    size_t padding = padding_for(b->data, align);
    b->used  = padding + size;
    a->used += size;

    return b->data + padding;
}

void *ps_arena_array(ps_arena *a, size_t n, size_t elem_size, size_t align) {
    if (elem_size != 0 && n > SIZE_MAX / elem_size) { return NULL; }

    return ps_arena_alloc(a, n * elem_size, align);
}

char *ps_arena_strdup(ps_arena *a, const char *s) {
    if (s == NULL) { return NULL; }

    size_t n = strlen(s) + 1;

    char *p = ps_arena_alloc(a, n, alignof(char));
    if (p != NULL) { memcpy(p, s, n); }

    return p;
}

void ps_arena_reset(ps_arena *a) {
    if (a == NULL) { return; }

    for (ps_arena_block *b = a->head; b != NULL; b = b->next) { b->used = 0; }
    a->used = 0;
}

void ps_arena_free(ps_arena *a) {
    if (a == NULL) { return; }

    ps_arena_block *b = a->head;
    while (b != NULL) {
        ps_arena_block *next = b->next;
        PS_FREE(b);
        b = next;
    }

    a->head     = NULL;
    a->used     = 0;
    a->reserved = 0;
}

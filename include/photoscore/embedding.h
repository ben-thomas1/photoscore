#pragma once

#include "photoscore/status.h"
#include "photoscore/types.h"

#include <stddef.h>

// A feature print: a fixed-length descriptor of what an image looks like, so
// that two photographs of the same thing land near each other regardless of
// crop, exposure or recompression.
typedef struct {
    f32    *values;
    size_t  len;
} ps_embedding;

ps_status_e ps_embedding_init(ps_embedding *out, size_t len);
void        ps_embedding_free(ps_embedding *e);

// Euclidean distance. Returns a negative value when the two are not
// comparable, which keeps an unusable pair out of any threshold test.
f32 ps_embedding_distance(const ps_embedding *a, const ps_embedding *b);

#include "photoscore/embedding.h"
#include "photoscore/alloc.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <math.h>
#include <stdint.h>

ps_status_e ps_embedding_init(ps_embedding *out, size_t len) {
    if (out == NULL || len == 0) { return PS_ERR_INVALID; }
    if (len > SIZE_MAX / sizeof(f32)) { return PS_ERR_NOMEM; }

    f32 *values = PS_MALLOC(len * sizeof(f32));
    if (values == NULL) { return PS_ERR_NOMEM; }

    *out = (ps_embedding){ .values = values, .len = len };

    return PS_OK;
}

void ps_embedding_free(ps_embedding *e) {
    if (e == NULL) { return; }

    PS_FREE(e->values);
    *e = (ps_embedding){ .values = NULL, .len = 0 };
}

f32 ps_embedding_distance(const ps_embedding *a, const ps_embedding *b) {
    if (a == NULL || b == NULL) { return -1.0f; }
    if (a->values == NULL || b->values == NULL) { return -1.0f; }
    if (a->len == 0 || a->len != b->len) { return -1.0f; }

    f64 sum = 0.0;
    for (size_t i = 0; i < a->len; i++) {
        f64 d = (f64)a->values[i] - (f64)b->values[i];
        sum += d * d;
    }

    return (f32)sqrt(sum);
}

#include "photoscore/mask.h"
#include "photoscore/alloc.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <math.h>
#include <stdint.h>

ps_status_e ps_mask_init(ps_mask *out, size_t width, size_t height) {
    if (out == NULL) { return PS_ERR_INVALID; }
    if (width == 0 || height == 0) { return PS_ERR_INVALID; }
    if (width > SIZE_MAX / sizeof(f32)) { return PS_ERR_NOMEM; }
    if (height > SIZE_MAX / (width * sizeof(f32))) { return PS_ERR_NOMEM; }

    f32 *values = PS_MALLOC(width * height * sizeof(f32));
    if (values == NULL) { return PS_ERR_NOMEM; }

    *out = (ps_mask){ .values = values, .width = width, .height = height };

    return PS_OK;
}

void ps_mask_free(ps_mask *m) {
    if (m == NULL) { return; }

    PS_FREE(m->values);
    *m = (ps_mask){ .values = NULL, .width = 0, .height = 0 };
}

static f32 mask_at(const ps_mask *m, size_t x, size_t y) {
    if (x >= m->width)  { x = m->width - 1; }
    if (y >= m->height) { y = m->height - 1; }

    return m->values[(y * m->width) + x];
}

f32 ps_mask_sample(const ps_mask *m, size_t x, size_t y, size_t img_w, size_t img_h) {
    if (m == NULL || m->values == NULL || img_w == 0 || img_h == 0) { return 0.0f; }
    if (m->width == 1 && m->height == 1) { return m->values[0]; }

    // Map pixel centres onto mask centres, so the edges line up rather than
    // being offset by half a cell.
    f32 u = (((f32)x + 0.5f) / (f32)img_w * (f32)m->width) - 0.5f;
    f32 v = (((f32)y + 0.5f) / (f32)img_h * (f32)m->height) - 0.5f;

    if (u < 0.0f) { u = 0.0f; }
    if (v < 0.0f) { v = 0.0f; }

    f32 uf = floorf(u);
    f32 vf = floorf(v);

    size_t x0 = (size_t)uf;
    size_t y0 = (size_t)vf;

    f32 tx = u - uf;
    f32 ty = v - vf;

    f32 a = mask_at(m, x0,     y0);
    f32 b = mask_at(m, x0 + 1, y0);
    f32 c = mask_at(m, x0,     y0 + 1);
    f32 d = mask_at(m, x0 + 1, y0 + 1);

    f32 top    = a + ((b - a) * tx);
    f32 bottom = c + ((d - c) * tx);

    return top + ((bottom - top) * ty);
}

void ps_mask_stats_of(const ps_mask *m, ps_mask_stats *out) {
    if (out == NULL) { return; }

    *out = (ps_mask_stats){ 0 };

    if (m == NULL || m->values == NULL || m->width == 0 || m->height == 0) {
        out->centroid_x = 0.5f;
        out->centroid_y = 0.5f;
        return;
    }

    size_t n = m->width * m->height;

    f32 lo = m->values[0];
    f32 hi = m->values[0];
    f64 sum = 0.0;

    for (size_t i = 0; i < n; i++) {
        f32 v = m->values[i];

        if (v < lo) { lo = v; }
        if (v > hi) { hi = v; }
        sum += (f64)v;
    }

    f64 mean = sum / (f64)n;

    f64 var = 0.0;
    for (size_t i = 0; i < n; i++) {
        f64 d = (f64)m->values[i] - mean;
        var += d * d;
    }
    var /= (f64)n;

    // Split at the midpoint of this map's own range rather than a fixed value,
    // so a low-contrast map still yields a meaningful subject area.
    f32 threshold = (lo + hi) * 0.5f;

    f64 above = 0.0;
    f64 wsum  = 0.0;
    f64 wx    = 0.0;
    f64 wy    = 0.0;

    for (size_t y = 0; y < m->height; y++) {
        for (size_t x = 0; x < m->width; x++) {
            f32 v = m->values[(y * m->width) + x];

            if (v > threshold) { above += 1.0; }

            wsum += (f64)v;
            wx   += (f64)v * ((f64)x + 0.5);
            wy   += (f64)v * ((f64)y + 0.5);
        }
    }

    out->min    = lo;
    out->max    = hi;
    out->mean   = (f32)mean;
    out->stddev = (f32)sqrt(var);
    out->area   = (f32)(above / (f64)n);

    if (wsum > 0.0) {
        out->centroid_x = (f32)(wx / wsum / (f64)m->width);
        out->centroid_y = (f32)(wy / wsum / (f64)m->height);
    } else {
        out->centroid_x = 0.5f;
        out->centroid_y = 0.5f;
    }
}

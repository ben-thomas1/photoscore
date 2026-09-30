#pragma once

#include "photoscore/status.h"
#include "photoscore/types.h"

#include <stdbool.h>
#include <stddef.h>

// Saliency map, values in [0,1], row-major. Vision returns a small fixed grid
// regardless of input size, so this is always tiny and gets sampled with
// bilinear interpolation when weighting full-resolution pixels.
typedef struct {
    f32    *values;
    size_t  width;
    size_t  height;
} ps_mask;

ps_status_e ps_mask_init(ps_mask *out, size_t width, size_t height);
void        ps_mask_free(ps_mask *m);

// Saliency at pixel (x, y) of an image measuring img_w by img_h.
f32 ps_mask_sample(const ps_mask *m, size_t x, size_t y, size_t img_w, size_t img_h);

typedef struct {
    f32 min;
    f32 max;
    f32 mean;
    f32 stddev;

    f32 area;         // fraction of the map above the midpoint of its range
    f32 centroid_x;   // centre of mass, normalised to [0,1]
    f32 centroid_y;
} ps_mask_stats;

// There is deliberately no "no subject found" flag here. Vision never returns
// a flat map: given nothing to attend to it falls back to a smooth centre
// blob, whose spread (0.069..0.126 on blank test images) sits squarely inside
// the range real photographs produce (0.073..0.183). Sharpness ratio does not
// separate the cases either - subject-free images read exactly 1.00, but so do
// the ten of thirty-five real photographs that are uniformly detailed. A
// centre prior is a reasonable stand-in for a subject, so callers should just
// use the map they are given.

void ps_mask_stats_of(const ps_mask *m, ps_mask_stats *out);

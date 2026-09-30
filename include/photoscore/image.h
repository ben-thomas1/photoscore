#pragma once

#include "photoscore/status.h"
#include "photoscore/types.h"

#include <stddef.h>

// Tightly packed 8-bit RGB, rows top-down, stride bytes per row.
typedef struct {
    u8     *pixels;
    size_t  width;
    size_t  height;
    size_t  stride;
} ps_image;

ps_status_e ps_image_init(ps_image *out, size_t width, size_t height);
void        ps_image_free(ps_image *img);

// Rewrite img so the stored pixels read upright, per the EXIF orientation tag
// (1-8). Orientation 1 is a no-op; 5-8 swap width and height. Reallocates.
ps_status_e ps_image_apply_orientation(ps_image *img, u32 orientation);

// Box-filter src down so neither edge exceeds max_edge, into a freshly
// allocated, distinct out. Copies when src already fits. Uses a box filter;
// technical sharpness also uses a normalized scale, while tone/noise stay native.
ps_status_e ps_image_downscale(const ps_image *src, size_t max_edge, ps_image *out);

// Address of the pixel at (x, y). No bounds check.
static inline u8 *ps_image_at(const ps_image *img, size_t x, size_t y) {
    return img->pixels + (y * img->stride) + (x * 3);
}

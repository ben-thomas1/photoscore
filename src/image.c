#include "photoscore/image.h"
#include "photoscore/alloc.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <stdint.h>
#include <string.h>

#define PS_IMAGE_BPP 3

ps_status_e ps_image_init(ps_image *out, size_t width, size_t height) {
    if (out == NULL) { return PS_ERR_INVALID; }
    if (width == 0 || height == 0) { return PS_ERR_INVALID; }
    if (width > SIZE_MAX / PS_IMAGE_BPP) { return PS_ERR_NOMEM; }

    size_t stride = width * PS_IMAGE_BPP;
    if (height > SIZE_MAX / stride) { return PS_ERR_NOMEM; }

    u8 *pixels = PS_MALLOC(stride * height);
    if (pixels == NULL) { return PS_ERR_NOMEM; }

    *out = (ps_image){
        .pixels = pixels,
        .width  = width,
        .height = height,
        .stride = stride,
    };

    return PS_OK;
}

void ps_image_free(ps_image *img) {
    if (img == NULL) { return; }

    PS_FREE(img->pixels);
    *img = (ps_image){ .pixels = NULL, .width = 0, .height = 0, .stride = 0 };
}

ps_status_e ps_image_downscale(const ps_image *src, size_t max_edge, ps_image *out) {
    if (src == NULL || src->pixels == NULL || out == NULL) { return PS_ERR_INVALID; }
    if (max_edge == 0 || src == out) { return PS_ERR_INVALID; }

    size_t longest = (src->width > src->height) ? src->width : src->height;

    size_t dw = src->width;
    size_t dh = src->height;

    if (longest > max_edge) {
        dw = (src->width * max_edge) / longest;
        dh = (src->height * max_edge) / longest;

        if (dw == 0) { dw = 1; }
        if (dh == 0) { dh = 1; }
    }

    ps_status_e s = ps_image_init(out, dw, dh);
    if (s != PS_OK) { return s; }

    for (size_t y = 0; y < dh; y++) {
        // Footprint of this output row in the source.
        size_t sy0 = (y * src->height) / dh;
        size_t sy1 = (((y + 1) * src->height) + dh - 1) / dh;
        if (sy1 <= sy0)          { sy1 = sy0 + 1; }
        if (sy1 > src->height)   { sy1 = src->height; }

        u8 *drow = out->pixels + (y * out->stride);

        for (size_t x = 0; x < dw; x++) {
            size_t sx0 = (x * src->width) / dw;
            size_t sx1 = (((x + 1) * src->width) + dw - 1) / dw;
            if (sx1 <= sx0)         { sx1 = sx0 + 1; }
            if (sx1 > src->width)   { sx1 = src->width; }

            u64 acc[3] = { 0, 0, 0 };
            size_t n = 0;

            for (size_t sy = sy0; sy < sy1; sy++) {
                const u8 *srow = src->pixels + (sy * src->stride);

                for (size_t sx = sx0; sx < sx1; sx++) {
                    const u8 *sp = srow + (sx * PS_IMAGE_BPP);

                    acc[0] += sp[0];
                    acc[1] += sp[1];
                    acc[2] += sp[2];
                    n += 1;
                }
            }

            u8 *dp = drow + (x * PS_IMAGE_BPP);
            dp[0] = (u8)(acc[0] / n);
            dp[1] = (u8)(acc[1] / n);
            dp[2] = (u8)(acc[2] / n);
        }
    }

    return PS_OK;
}

// Source coordinate for a destination coordinate, as an affine map:
//   sx = ax*x + ay*y + a0
//   sy = bx*x + by*y + b0
// Every EXIF orientation is one of these with coefficients in {-1, 0, 1}.
typedef struct {
    i64 ax, ay, a0;
    i64 bx, by, b0;
} orient_map;

static orient_map orient_map_for(u32 orientation, i64 sw, i64 sh) {
    switch (orientation) {
        case 2:  return (orient_map){ -1, 0, sw - 1,   0,  1, 0      };  // mirror horizontal
        case 3:  return (orient_map){ -1, 0, sw - 1,   0, -1, sh - 1 };  // rotate 180
        case 4:  return (orient_map){  1, 0, 0,        0, -1, sh - 1 };  // mirror vertical
        case 5:  return (orient_map){  0, 1, 0,        1,  0, 0      };  // transpose
        case 6:  return (orient_map){  0, 1, 0,       -1,  0, sh - 1 };  // rotate 90 CW
        case 7:  return (orient_map){  0, -1, sw - 1, -1,  0, sh - 1 };  // transverse
        case 8:  return (orient_map){  0, -1, sw - 1,  1,  0, 0      };  // rotate 90 CCW
        default: return (orient_map){  1, 0, 0,        0,  1, 0      };  // identity
    }
}

// Orientations 5-8 transpose the axes, so the upright image swaps dimensions.
static bool orientation_swaps_axes(u32 orientation) {
    return orientation >= 5 && orientation <= 8;
}

ps_status_e ps_image_apply_orientation(ps_image *img, u32 orientation) {
    if (img == NULL || img->pixels == NULL) { return PS_ERR_INVALID; }
    if (orientation <= 1 || orientation > 8) { return PS_OK; }

    size_t sw = img->width;
    size_t sh = img->height;

    ps_image dst;
    ps_status_e s = orientation_swaps_axes(orientation)
        ? ps_image_init(&dst, sh, sw)
        : ps_image_init(&dst, sw, sh);
    if (s != PS_OK) { return s; }

    orient_map m = orient_map_for(orientation, (i64)sw, (i64)sh);

    for (size_t y = 0; y < dst.height; y++) {
        u8 *drow = dst.pixels + (y * dst.stride);
        i64 yi = (i64)y;

        for (size_t x = 0; x < dst.width; x++) {
            i64 xi = (i64)x;
            size_t sx = (size_t)(m.ax * xi + m.ay * yi + m.a0);
            size_t sy = (size_t)(m.bx * xi + m.by * yi + m.b0);

            const u8 *sp = img->pixels + (sy * img->stride) + (sx * PS_IMAGE_BPP);
            u8       *dp = drow + (x * PS_IMAGE_BPP);

            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
        }
    }

    ps_image_free(img);
    *img = dst;

    return PS_OK;
}

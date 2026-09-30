#include "photoscore/technical.h"
#include "photoscore/alloc.h"
#include "photoscore/image.h"
#include "photoscore/mask.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define LUMA_BINS  256
#define LAP_BINS   1024    // |4c - 4 neighbours| reaches 1020
#define NOISE_BINS 4096    // the noise kernel's absolute weights sum to 16

// Quartile of the absolute kernel response, chosen so that content-heavy
// frames still have enough flat area below it to estimate from.
#define NOISE_PERCENTILE 0.25

// Gaussian noise of deviation s produces a kernel response of deviation 6s,
// since the squared weights sum to 36. The quartile of the resulting
// half-normal sits at 6 * sqrt(2) * erfinv(0.25) = 1.9118 deviations.
#define NOISE_PERCENTILE_SCALE 1.911834

#define SHARP_PERCENTILE  0.95
#define CLIP_HIGH_LEVEL   254
#define CLIP_LOW_LEVEL    2
#define MID_GREY_LINEAR   0.18f

// Half-width of the band around the split point that belongs to neither
// region, as a fraction of the mask's own range.
#define BOUNDARY_GAP 0.10f

typedef enum {
    REGION_BACKGROUND = 0,
    REGION_BOUNDARY   = 1,
    REGION_SUBJECT    = 2,
} region_e;

static void build_linear_lut(f32 *lut) {
    for (size_t i = 0; i < LUMA_BINS; i++) {
        f32 c = (f32)i / 255.0f;

        lut[i] = (c <= 0.04045f) ? (c / 12.92f)
                                 : powf((c + 0.055f) / 1.055f, 2.4f);
    }
}

static f32 percentile_from_hist(const u64 *hist, size_t bins, u64 total, f64 p) {
    if (total == 0) { return 0.0f; }

    // One-based nearest rank: a nonempty population must select a sample.
    u64 target = (u64)ceil((f64)total * p);
    if (target == 0) { target = 1; }
    u64 cum    = 0;

    for (size_t i = 0; i < bins; i++) {
        cum += hist[i];
        if (cum >= target) { return (f32)i; }
    }

    return (f32)(bins - 1);
}

static region_e region_at(const ps_mask *mask, size_t x, size_t y,
                          size_t w, size_t h, f32 split, f32 gap) {
    if (mask == NULL || mask->values == NULL) { return REGION_SUBJECT; }

    f32 sal = ps_mask_sample(mask, x, y, w, h);

    if (sal >= split + gap) { return REGION_SUBJECT; }
    if (sal <= split - gap) { return REGION_BACKGROUND; }

    return REGION_BOUNDARY;
}

// Box-filter a single-channel plane so neither edge exceeds max_edge. Returns
// the source pointer unchanged, with owned set false, when it already fits.
static ps_status_e downscale_plane(const u8 *src, size_t sw, size_t sh, size_t max_edge,
                                   const u8 **out, size_t *out_w, size_t *out_h,
                                   bool *out_owned) {
    size_t longest = (sw > sh) ? sw : sh;

    if (longest <= max_edge) {
        *out       = src;
        *out_w     = sw;
        *out_h     = sh;
        *out_owned = false;
        return PS_OK;
    }

    size_t dw = (sw * max_edge) / longest;
    size_t dh = (sh * max_edge) / longest;
    if (dw == 0) { dw = 1; }
    if (dh == 0) { dh = 1; }

    u8 *dst = PS_MALLOC(dw * dh);
    if (dst == NULL) { return PS_ERR_NOMEM; }

    for (size_t y = 0; y < dh; y++) {
        size_t sy0 = (y * sh) / dh;
        size_t sy1 = (((y + 1) * sh) + dh - 1) / dh;
        if (sy1 <= sy0) { sy1 = sy0 + 1; }
        if (sy1 > sh)   { sy1 = sh; }

        for (size_t x = 0; x < dw; x++) {
            size_t sx0 = (x * sw) / dw;
            size_t sx1 = (((x + 1) * sw) + dw - 1) / dw;
            if (sx1 <= sx0) { sx1 = sx0 + 1; }
            if (sx1 > sw)   { sx1 = sw; }

            u64 acc = 0;
            size_t n = 0;

            for (size_t sy = sy0; sy < sy1; sy++) {
                for (size_t sx = sx0; sx < sx1; sx++) {
                    acc += src[(sy * sw) + sx];
                    n += 1;
                }
            }

            dst[(y * dw) + x] = (u8)(acc / n);
        }
    }

    *out       = dst;
    *out_w     = dw;
    *out_h     = dh;
    *out_owned = true;

    return PS_OK;
}

ps_status_e ps_technical_measure(const ps_image *img, const ps_mask *mask,
                                 ps_technical *out) {
    if (img == NULL || img->pixels == NULL || out == NULL) { return PS_ERR_INVALID; }
    if (img->width < 3 || img->height < 3) { return PS_ERR_INVALID; }

    *out = (ps_technical){ 0 };

    size_t w = img->width;
    size_t h = img->height;
    size_t n = w * h;

    u8 *luma = PS_MALLOC(n);
    if (luma == NULL) { return PS_ERR_NOMEM; }

    f32 lut[LUMA_BINS];
    build_linear_lut(lut);

    // Split at the midpoint of the mask's own range, so a low-contrast map
    // still yields a usable subject rather than collapsing to one region.
    f32 split = 0.5f;
    f32 gap   = 0.0f;

    if (mask != NULL && mask->values != NULL) {
        ps_mask_stats st;
        ps_mask_stats_of(mask, &st);

        split = (st.min + st.max) * 0.5f;
        gap   = (st.max - st.min) * BOUNDARY_GAP;
    }

    u64 luma_hist[LUMA_BINS] = { 0 };

    u64 n_subject    = 0;
    u64 n_background = 0;

    f64 luma_sum_subject    = 0.0;
    f64 luma_sum_background = 0.0;
    f64 linear_sum_subject  = 0.0;

    u64 clip_high = 0;
    u64 clip_low  = 0;
    u64 clip_high_subject = 0;
    u64 clip_low_subject  = 0;

    // Hasler and Suesstrunk work on two opponent-colour channels; their means
    // and spreads come free from accumulators in the pass already being made.
    f64 rg_sum  = 0.0;
    f64 rg_sum2 = 0.0;
    f64 yb_sum  = 0.0;
    f64 yb_sum2 = 0.0;

    // Tone statistics run at native resolution: clipping is a property of the
    // actual captured pixels and resampling would hide it.
    for (size_t y = 0; y < h; y++) {
        const u8 *row = img->pixels + (y * img->stride);

        for (size_t x = 0; x < w; x++) {
            const u8 *p = row + (x * 3);

            u32 r = p[0];
            u32 g = p[1];
            u32 b = p[2];

            // Rec. 601 luma, rounded.
            u32 l = ((299u * r) + (587u * g) + (114u * b) + 500u) / 1000u;
            if (l > 255u) { l = 255u; }

            luma[(y * w) + x] = (u8)l;
            luma_hist[l] += 1;

            // Highlight clipping is per-channel: a saturated red is clipped
            // even while the pixel reads mid-grey. Shadow clipping needs every
            // channel crushed, which is what the brightest one tests.
            u32 hi = (r > g) ? r : g;
            if (b > hi) { hi = b; }

            bool is_high = (hi >= CLIP_HIGH_LEVEL);
            bool is_low  = (hi <= CLIP_LOW_LEVEL);

            if (is_high) { clip_high += 1; }
            if (is_low)  { clip_low  += 1; }

            f64 rg = (f64)r - (f64)g;
            f64 yb = (0.5 * ((f64)r + (f64)g)) - (f64)b;

            rg_sum  += rg;
            rg_sum2 += rg * rg;
            yb_sum  += yb;
            yb_sum2 += yb * yb;

            region_e reg = region_at(mask, x, y, w, h, split, gap);

            if (reg == REGION_SUBJECT) {
                n_subject += 1;
                luma_sum_subject   += (f64)l;
                // Linearize channels before mixing: inverse-gamma of luma
                // badly underestimates the luminance of saturated colours.
                linear_sum_subject += (0.2126 * (f64)lut[r])
                                    + (0.7152 * (f64)lut[g])
                                    + (0.0722 * (f64)lut[b]);

                if (is_high) { clip_high_subject += 1; }
                if (is_low)  { clip_low_subject  += 1; }
            } else if (reg == REGION_BACKGROUND) {
                n_background += 1;
                luma_sum_background += (f64)l;
            }
        }
    }

    // Noise estimate from Immerkaer's kernel
    //
    //     1 -2  1
    //    -2  4 -2
    //     1 -2  1
    //
    // which cancels locally linear content, leaving noise. Its usual form
    // averages the response over the whole frame, and that does not survive
    // contact with real photographs: texture and edges are not linear either,
    // so the mean tracks detail rather than noise. Measured that way against
    // this corpus it correlated r=+0.71 with sharpness and r=+0.01 with ISO,
    // which is precisely backwards.
    //
    // Taking a low percentile instead lets the flat regions of the frame carry
    // the estimate, which is where noise is actually visible.
    //
    // Native resolution throughout: downscaling averages away the very thing
    // being measured.
    u64 noise_hist[NOISE_BINS] = { 0 };
    u64 noise_n = 0;

    for (size_t y = 1; y + 1 < h; y++) {
        const u8 *r0 = luma + ((y - 1) * w);
        const u8 *r1 = luma + (y * w);
        const u8 *r2 = luma + ((y + 1) * w);

        for (size_t x = 1; x + 1 < w; x++) {
            i32 v = (i32)r0[x - 1] - (2 * (i32)r0[x]) + (i32)r0[x + 1]
                  - (2 * (i32)r1[x - 1]) + (4 * (i32)r1[x]) - (2 * (i32)r1[x + 1])
                  + (i32)r2[x - 1] - (2 * (i32)r2[x]) + (i32)r2[x + 1];

            if (v < 0) { v = -v; }
            if (v >= NOISE_BINS) { v = NOISE_BINS - 1; }

            noise_hist[v] += 1;
            noise_n += 1;
        }
    }

    if (noise_n > 0) {
        f32 p = percentile_from_hist(noise_hist, NOISE_BINS, noise_n, NOISE_PERCENTILE);

        out->noise_sigma = (f32)((f64)p / NOISE_PERCENTILE_SCALE);
    }

    // A 3x3 Laplacian measures detail at the scale of one pixel, so the same
    // real-world edge spreads thinner on a larger sensor and reads as less
    // sharp. Measuring every photograph at one reference scale is what makes
    // the numbers comparable between a 6 MP frame and a 24 MP one.
    const u8 *sl    = NULL;
    size_t    sw    = 0;
    size_t    sh    = 0;
    bool      owned = false;

    ps_status_e s = downscale_plane(luma, w, h, PS_SHARP_REFERENCE_EDGE,
                                    &sl, &sw, &sh, &owned);
    if (s != PS_OK) {
        PS_FREE(luma);
        return s;
    }

    u64 lap_subject[LAP_BINS]    = { 0 };
    u64 lap_background[LAP_BINS] = { 0 };

    u64 lap_n_subject    = 0;
    u64 lap_n_background = 0;

    for (size_t y = 1; y + 1 < sh; y++) {
        for (size_t x = 1; x + 1 < sw; x++) {
            region_e reg = region_at(mask, x, y, sw, sh, split, gap);
            if (reg == REGION_BOUNDARY) { continue; }

            size_t i = (y * sw) + x;

            i32 c  = (i32)sl[i];
            i32 up = (i32)sl[i - sw];
            i32 dn = (i32)sl[i + sw];
            i32 lf = (i32)sl[i - 1];
            i32 rt = (i32)sl[i + 1];

            i32 lap = (4 * c) - up - dn - lf - rt;
            if (lap < 0) { lap = -lap; }
            if (lap >= LAP_BINS) { lap = LAP_BINS - 1; }

            if (reg == REGION_SUBJECT) {
                lap_subject[lap] += 1;
                lap_n_subject += 1;
            } else {
                lap_background[lap] += 1;
                lap_n_background += 1;
            }
        }
    }

    out->sharp_scale = (u32)((sw > sh) ? sw : sh);

    out->sharp_subject =
        percentile_from_hist(lap_subject, LAP_BINS, lap_n_subject, SHARP_PERCENTILE);
    out->sharp_background =
        percentile_from_hist(lap_background, LAP_BINS, lap_n_background, SHARP_PERCENTILE);

    out->sharp_ratio = (out->sharp_background > 0.0f)
        ? (out->sharp_subject / out->sharp_background)
        : 1.0f;   // no measurable background: the comparison is neutral

    out->luma_subject = (n_subject > 0)
        ? (f32)(luma_sum_subject / (f64)n_subject / 255.0) : 0.0f;
    out->luma_background = (n_background > 0)
        ? (f32)(luma_sum_background / (f64)n_background / 255.0) : 0.0f;

    // Exposure error is meaningful in linear light, not in gamma-encoded
    // values, so the subject mean is accumulated through the sRGB curve.
    if (n_subject > 0) {
        f64 mean_linear = linear_sum_subject / (f64)n_subject;
        if (mean_linear < 1e-6) { mean_linear = 1e-6; }

        out->ev_offset = (f32)log2(mean_linear / (f64)MID_GREY_LINEAR);
    }

    out->clip_high = (f32)((f64)clip_high / (f64)n);
    out->clip_low  = (f32)((f64)clip_low  / (f64)n);

    out->clip_high_subject = (n_subject > 0)
        ? (f32)((f64)clip_high_subject / (f64)n_subject) : 0.0f;
    out->clip_low_subject = (n_subject > 0)
        ? (f32)((f64)clip_low_subject / (f64)n_subject) : 0.0f;

    out->luma_p1  = percentile_from_hist(luma_hist, LUMA_BINS, n, 0.01) / 255.0f;
    out->luma_p99 = percentile_from_hist(luma_hist, LUMA_BINS, n, 0.99) / 255.0f;

    out->contrast_range = out->luma_p99 - out->luma_p1;

    // Mean and spread come straight out of the histogram, so RMS contrast
    // costs no extra pass over the pixels.
    f64 hist_mean = 0.0;
    for (size_t i = 0; i < LUMA_BINS; i++) {
        hist_mean += (f64)i * (f64)luma_hist[i];
    }
    hist_mean /= (f64)n;

    f64 hist_var = 0.0;
    for (size_t i = 0; i < LUMA_BINS; i++) {
        f64 d = (f64)i - hist_mean;
        hist_var += d * d * (f64)luma_hist[i];
    }
    hist_var /= (f64)n;

    out->contrast_rms = (f32)(sqrt(hist_var) / 255.0);

    f64 rg_mean = rg_sum / (f64)n;
    f64 yb_mean = yb_sum / (f64)n;

    f64 rg_var = (rg_sum2 / (f64)n) - (rg_mean * rg_mean);
    f64 yb_var = (yb_sum2 / (f64)n) - (yb_mean * yb_mean);

    if (rg_var < 0.0) { rg_var = 0.0; }   // rounding can push this just below zero
    if (yb_var < 0.0) { yb_var = 0.0; }

    f64 sigma_rgyb = sqrt(rg_var + yb_var);
    f64 mu_rgyb    = sqrt((rg_mean * rg_mean) + (yb_mean * yb_mean));

    out->colourfulness = (f32)(sigma_rgyb + (0.3 * mu_rgyb));

    // Composition, from where the saliency mass sits. Distances are measured
    // across the frame diagonal so they do not depend on aspect ratio.
    {
        ps_mask_stats st = { .centroid_x = 0.5f, .centroid_y = 0.5f };
        if (mask != NULL && mask->values != NULL) { ps_mask_stats_of(mask, &st); }

        f64 cx = (f64)st.centroid_x;
        f64 cy = (f64)st.centroid_y;

        f64 dx = cx - 0.5;
        f64 dy = cy - 0.5;
        out->centre_distance = (f32)(sqrt((dx * dx) + (dy * dy)) / sqrt(0.5));

        f64 best = 2.0;
        const f64 thirds[2] = { 1.0 / 3.0, 2.0 / 3.0 };

        for (size_t i = 0; i < 2; i++) {
            for (size_t j = 0; j < 2; j++) {
                f64 tx = cx - thirds[i];
                f64 ty = cy - thirds[j];
                f64 d  = sqrt((tx * tx) + (ty * ty));

                if (d < best) { best = d; }
            }
        }

        out->thirds_distance = (f32)(best / sqrt(0.5));
    }

    out->subject_fraction    = (f32)((f64)n_subject / (f64)n);
    out->background_fraction = (f32)((f64)n_background / (f64)n);

    if (owned) { PS_FREE((void *)(uintptr_t)(const void *)sl); }
    PS_FREE(luma);

    return PS_OK;
}

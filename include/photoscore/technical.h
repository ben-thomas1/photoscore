#pragma once

#include "photoscore/image.h"
#include "photoscore/mask.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

// Physical measurements only. Nothing here is normalised to [0,1]: these are
// absolute and stay comparable across runs, unlike the corpus percentiles
// derived from them later.
//
// Every measurement that can differ between subject and background is taken
// twice, because the same reading means opposite things in each. Blur behind
// the subject is bokeh; blur on the subject is a missed shot.
// Long edge, in pixels, that sharpness is measured at. A 3x3 Laplacian reads
// detail at the scale of a single pixel, so the same real-world edge spreads
// thinner on a bigger sensor and scores lower. Normalising to one reference
// scale is what makes a 6 MP frame comparable with a 24 MP one.
#define PS_SHARP_REFERENCE_EDGE 1536

typedef struct {
    // 95th percentile of local Laplacian energy, 0..1020, measured at the
    // reference scale. A percentile rather than a mean: a portrait is mostly
    // smooth skin, so the mean would call a perfectly focused face soft.
    f32 sharp_subject;
    f32 sharp_background;

    // Long edge sharpness was actually measured at. Equal to the reference
    // unless the photograph was already smaller, since upscaling would invent
    // detail rather than normalise it.
    u32 sharp_scale;

    // subject / background. Near 1.0 means the two regions are equally
    // detailed, which is what a frame with no real subject looks like.
    f32 sharp_ratio;

    // Mean gamma-encoded luma, 0..1.
    f32 luma_subject;
    f32 luma_background;

    // Stops from mid-grey, measured on the subject in linear light.
    f32 ev_offset;

    // Fraction of pixels with a saturated or crushed channel.
    f32 clip_high;
    f32 clip_low;
    f32 clip_high_subject;
    f32 clip_low_subject;

    // 1st and 99th percentile luma across the frame, 0..1.
    f32 luma_p1;
    f32 luma_p99;

    // Immerkaer estimate of the sensor noise standard deviation, in 0..255
    // units at native resolution. Not split by region: read noise comes from
    // the sensor at a fixed ISO and is uniform across the frame, so there is
    // nothing for a subject/background split to reveal. Measured natively
    // because any downscaling averages the very thing being measured away.
    f32 noise_sigma;

    // Tonal spread: the p99-p1 range, and the RMS contrast of the frame.
    f32 contrast_range;
    f32 contrast_rms;

    // Hasler and Suesstrunk colourfulness. Roughly 0 for monochrome, past 100
    // for a saturated frame.
    f32 colourfulness;

    // Composition, derived from where the saliency mass sits. Both are
    // normalised distances across the frame diagonal.
    f32 thirds_distance;   // to the nearest rule-of-thirds intersection
    f32 centre_distance;   // to the middle of the frame

    // How the frame was split. These sum to less than 1: pixels straddling
    // the subject boundary are excluded from both, since a subject edge is
    // exactly where Laplacian energy is highest and would otherwise inflate
    // the background reading.
    f32 subject_fraction;
    f32 background_fraction;
} ps_technical;

// Measure img, using mask to separate subject from background. mask may be
// NULL, in which case the whole frame is treated as the subject.
ps_status_e ps_technical_measure(const ps_image *img, const ps_mask *mask,
                                 ps_technical *out);

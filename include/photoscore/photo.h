#pragma once

#include "photoscore/aesthetic.h"
#include "photoscore/embedding.h"
#include "photoscore/exif.h"
#include "photoscore/mask.h"
#include "photoscore/technical.h"
#include "photoscore/types.h"

#include <stdbool.h>
#include <stddef.h>

// Reasons a photograph is a candidate for deletion. These are mechanisms, not
// a low score: a photograph can rank last in a corpus and still be worth
// keeping, while a duplicate or a blown frame is disposable whatever it scores.
typedef enum {
    PS_FLAG_DUPLICATE    = 1u << 0,   // a better copy of this shot exists
    PS_FLAG_UTILITY      = 1u << 1,   // the model judged the content unmemorable
    PS_FLAG_BLURRED      = 1u << 2,   // the subject is not in focus
    PS_FLAG_UNDEREXPOSED = 1u << 3,
    PS_FLAG_OVEREXPOSED  = 1u << 4,
} ps_flag_e;

#define PS_FLAG_COUNT 5

// Everything known about one photograph. Grows as each scoring stage lands;
// one of these per image is held for the whole run so the corpus can be
// ranked and calibrated as a whole.
typedef struct {
    const char *path;     // borrowed from the walk arena

    size_t  width;        // upright, after orientation
    size_t  height;
    ps_exif exif;

    ps_aesthetic aesthetic;

    // The map itself is not retained; only what later stages need from it.
    ps_mask_stats saliency;

    ps_technical technical;

    // Absolute [0,1], comparable between runs: how technically sound the
    // photograph is, blended from the measurements above.
    f32 technical_quality;

    // Corpus percentiles, [0,1]. Meaningful only against the exact set of
    // photographs they were computed over.
    f32 aesthetic_pct;
    f32 technical_pct;

    // One-sided multiplier derived from technical_quality: 1.0 unless the
    // photograph is technically poor enough to be pulled down.
    f32 gate;

    // aesthetic_pct * gate, in [0,1].
    f32 score;

    // Owned. Only needed until clustering has run, then released.
    ps_embedding embedding;

    // Near-duplicate grouping. cluster is an opaque group id shared by every
    // member; exactly one member of each group is the best.
    u32  cluster;
    u32  cluster_size;
    bool cluster_best;

    // Path of this group's keeper, so a duplicate can name what supersedes it.
    // Borrowed, and valid for as long as the other photograph's path is.
    const char *cluster_best_path;

    // Bitmask of ps_flag_e.
    u32 flags;
} ps_photo;

const char *ps_flag_name(ps_flag_e flag);

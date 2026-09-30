#pragma once

#include "photoscore/da.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

// Feature-print distance below which two photographs are the same shot.
//
// Preliminary local measurements over three photographs, each re-encoded, recompressed to quality
// 0.35, cropped 12%, brightened half a stop, downscaled to a third and shifted
// a few pixels:
//
//   same photograph   n= 45   mean 0.166   max 0.318
//   different photos  n=108   mean 0.834   min 0.538
//
// These small samples did not overlap; the original experiment is not included
// and the threshold is not a guarantee. This sits nearer the top of the first
// than the midpoint of the gap on purpose: merging two distinct photographs
// hides one of them from the ranking entirely, while missing a duplicate only
// leaves two similar frames in it. The run reports both extremes so the
// margin can be checked against a real corpus. Vision revision 2 is used.
#define PS_DEDUP_THRESHOLD 0.40f

typedef struct {
    size_t clusters;      // distinct groups, singletons included
    size_t duplicates;    // photographs that are not the best of their group
    size_t largest;       // members in the biggest group

    // Pairwise threshold statistics, not final cluster diameters: single
    // linkage can join endpoints whose mutual distance exceeds the threshold.
    f32 nearest_nonmatch;
    f32 farthest_match;
} ps_dedup_stats;

// Group photographs whose feature prints fall within threshold, by single
// linkage: a chain of near-identical frames from a burst stays one group even
// when its two ends have drifted apart.
//
// Writes cluster, cluster_size and cluster_best into each ps_photo. Must run
// before the corpus is sorted, and needs every photo to carry an embedding.
ps_status_e ps_dedup_cluster(ps_da *photos, f32 threshold, ps_dedup_stats *out_stats);

#pragma once

#include "photoscore/arena.h"
#include "photoscore/da.h"
#include "photoscore/status.h"

#include <stdbool.h>
#include <stddef.h>

// Counts for one traversal. Per-entry errors are reported on stderr as they
// happen and do not stop the walk.
typedef struct {
    size_t dirs;        // directories descended into
    size_t files;       // regular files visited
    size_t candidates;  // files that passed the filter
    size_t skipped;     // non-image extensions and hidden files
    size_t bundles;     // hidden or bundle directories not descended into
    size_t symlinks;    // not followed, never scored
    size_t errors;      // unreadable directories, failed stats
} ps_walk_stats;

// Collect every scoreable image under root, recursively, sorted by path.
// Symlinks are not followed.
//
// out is a ps_da of `char *` borrowed from arena, so the arena must outlive
// it. Fails only if root is unusable or memory runs out; unreadable entries
// below root are counted, not fatal.
ps_status_e ps_walk(const char *root, ps_arena *arena, ps_da *out, ps_walk_stats *out_stats);

// True if name has an image extension we can decode and is not hidden.
bool ps_walk_is_candidate(const char *name);

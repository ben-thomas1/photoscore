#include "photoscore/dedup.h"
#include "photoscore/alloc.h"
#include "photoscore/da.h"
#include "photoscore/embedding.h"
#include "photoscore/photo.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <stdint.h>

// Union-find with path halving. Single-linkage clustering is exactly the
// connected components of the graph of close-enough pairs, so a disjoint set
// is all the machinery needed.
static size_t find(size_t *parent, size_t i) {
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }

    return i;
}

static void join(size_t *parent, size_t *rank, size_t a, size_t b) {
    a = find(parent, a);
    b = find(parent, b);

    if (a == b) { return; }

    if (rank[a] < rank[b]) {
        size_t t = a; a = b; b = t;
    }

    parent[b] = a;
    if (rank[a] == rank[b]) { rank[a] += 1; }
}

ps_status_e ps_dedup_cluster(ps_da *photos, f32 threshold, ps_dedup_stats *out_stats) {
    if (photos == NULL || out_stats == NULL) { return PS_ERR_INVALID; }
    if (photos->elem_size != sizeof(ps_photo)) { return PS_ERR_INVALID; }

    *out_stats = (ps_dedup_stats){
        .nearest_nonmatch = -1.0f,
        .farthest_match = -1.0f,
    };

    size_t n = photos->len;
    if (n == 0) { return PS_OK; }

    if (n > SIZE_MAX / sizeof(size_t)) { return PS_ERR_NOMEM; }

    size_t *parent = PS_MALLOC(n * sizeof(size_t));
    size_t *rank   = PS_MALLOC(n * sizeof(size_t));
    size_t *size   = PS_MALLOC(n * sizeof(size_t));

    if (parent == NULL || rank == NULL || size == NULL) {
        PS_FREE(size);
        PS_FREE(rank);
        PS_FREE(parent);
        return PS_ERR_NOMEM;
    }

    for (size_t i = 0; i < n; i++) {
        parent[i] = i;
        rank[i]   = 0;
        size[i]   = 0;
    }

    // Every pair. At a thousand photographs this is half a million distances
    // over a few thousand dimensions, which costs well under a second.
    for (size_t i = 0; i < n; i++) {
        const ps_photo *a = ps_da_at(photos, i);

        for (size_t j = i + 1; j < n; j++) {
            const ps_photo *b = ps_da_at(photos, j);

            f32 d = ps_embedding_distance(&a->embedding, &b->embedding);
            if (d < 0.0f) { continue; }

            if (d <= threshold) {
                join(parent, rank, i, j);

                if (d > out_stats->farthest_match) { out_stats->farthest_match = d; }
            } else {
                if (out_stats->nearest_nonmatch < 0.0f || d < out_stats->nearest_nonmatch) {
                    out_stats->nearest_nonmatch = d;
                }
            }
        }
    }

    for (size_t i = 0; i < n; i++) { size[find(parent, i)] += 1; }

    // Best of each group by score, so a burst contributes one keeper and the
    // rest become deletion candidates rather than crowding the ranking.
    for (size_t i = 0; i < n; i++) {
        ps_photo *p = ps_da_at(photos, i);
        size_t    r = find(parent, i);

        p->cluster            = (u32)r;
        p->cluster_size       = (u32)size[r];
        p->cluster_best       = false;
        p->cluster_best_path  = NULL;
    }

    for (size_t r = 0; r < n; r++) {
        if (find(parent, r) != r) { continue; }

        size_t best = SIZE_MAX;
        for (size_t i = 0; i < n; i++) {
            const ps_photo *p = ps_da_at(photos, i);
            if (p->cluster != (u32)r) { continue; }

            if (best == SIZE_MAX) {
                best = i;
                continue;
            }

            const ps_photo *b = ps_da_at(photos, best);
            if (p->score > b->score) { best = i; }
        }

        if (best != SIZE_MAX) {
            ps_photo *keeper = ps_da_at(photos, best);
            keeper->cluster_best = true;

            // Point every member at the keeper so a duplicate can name what
            // supersedes it without the report having to search for it.
            for (size_t i = 0; i < n; i++) {
                ps_photo *p = ps_da_at(photos, i);
                if (p->cluster != (u32)r) { continue; }

                p->cluster_best_path = keeper->path;

                if (i != best) { p->flags |= PS_FLAG_DUPLICATE; }
            }
        }

        out_stats->clusters += 1;
        if (size[r] > out_stats->largest) { out_stats->largest = size[r]; }
    }

    for (size_t i = 0; i < n; i++) {
        const ps_photo *p = ps_da_at(photos, i);
        if (!p->cluster_best) { out_stats->duplicates += 1; }
    }

    PS_FREE(size);
    PS_FREE(rank);
    PS_FREE(parent);

    return PS_OK;
}

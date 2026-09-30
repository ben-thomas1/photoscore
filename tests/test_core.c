#include "photoscore/arena.h"
#include "photoscore/cli.h"
#include "photoscore/da.h"
#include "photoscore/dedup.h"
#include "photoscore/image.h"
#include "photoscore/mask.h"
#include "photoscore/overlay.h"
#include "photoscore/photo.h"
#include "photoscore/score.h"
#include "photoscore/technical.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(bool ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); failures++; }
}

static void must(ps_status_e s) {
    if (s != PS_OK) { fprintf(stderr, "setup failed: %d\n", s); exit(1); }
}

static void check_allocators(void) {
    ps_arena a;
    must(ps_arena_init(&a, 128));
    const size_t alignments[] = {1, 8, 16, 32, 64, 4096};
    for (size_t pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < sizeof(alignments) / sizeof(alignments[0]); i++) {
            void *p = ps_arena_alloc(&a, 32, alignments[i]);
            check(p != NULL && (uintptr_t)p % alignments[i] == 0, "arena alignment");
            if (p != NULL) { memset(p, 42, 32); }
        }
        ps_arena_reset(&a);
    }
    check(ps_arena_alloc(&a, 4, 3) == NULL, "arena rejects invalid alignment");
    check(ps_arena_array(&a, SIZE_MAX, 2, 8) == NULL, "arena rejects overflow");
    ps_arena_free(&a);

    ps_da values;
    int value = 42;
    must(ps_da_init(&values, sizeof(value), 1));
    must(ps_da_push(&values, &value));
    must(ps_da_push(&values, ps_da_at(&values, 0)));
    check(values.len == 2 && *(int *)ps_da_at(&values, 1) == 42,
          "self-append survives growth");
    ps_da_free(&values);
}

static void check_cli(void) {
    const char *invalid[] = {"-1", "0", "+2", " 2", "2x", "999999999999999999999999"};
    ps_cli_opts opts;
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char *args[] = {(char *)"photoscore", (char *)"/tmp", (char *)"--top", (char *)invalid[i]};
        check(ps_cli_parse(4, args, &opts) == PS_ERR_INVALID, "reject invalid top count");
    }
    char *args[] = {(char *)"photoscore", (char *)"/tmp", (char *)"--top", (char *)"2", (char *)"--print0"};
    must(ps_cli_parse(5, args, &opts));
    check(opts.top == 2 && opts.print0, "parse top and NUL output");
}

static void check_pixels(void) {
    ps_image img;
    ps_technical t;
    must(ps_image_init(&img, 8, 8));
    memset(img.pixels, 128, img.stride * img.height);
    must(ps_technical_measure(&img, NULL, &t));
    check(t.contrast_range == 0.0f && t.luma_p1 > 0.5f, "uniform small image has zero range");
    ps_image_free(&img);

    must(ps_image_init(&img, 3, 3));
    memset(img.pixels, 0, img.stride * img.height);
    memset(ps_image_at(&img, 1, 1), 255, 3);
    must(ps_technical_measure(&img, NULL, &t));
    check(t.sharp_subject == 1020.0f, "one-sample sharpness percentile");
    check(t.sharp_ratio == 1.0f, "missing background is neutral");
    ps_image_free(&img);

    must(ps_image_init(&img, 64, 64));
    for (size_t y = 0; y < img.height; y++) {
        for (size_t x = 0; x < img.width; x++) {
            u8 *p = ps_image_at(&img, x, y);
            p[0] = 0; p[1] = 0; p[2] = 240;
        }
    }
    must(ps_technical_measure(&img, NULL, &t));
    check(fabsf(t.ev_offset - (-1.517f)) < 0.002f, "linear-light exposure for blue");
    ps_photo photo = {.path="blue.png", .technical=t, .aesthetic={.overall=0.5f}};
    ps_da photos;
    must(ps_da_init(&photos, sizeof(photo), 1));
    must(ps_da_push(&photos, &photo));
    ps_score_params params = ps_score_defaults();
    must(ps_score_corpus(&photos, &params));
    check((((ps_photo *)ps_da_at(&photos, 0))->flags & PS_FLAG_UNDEREXPOSED) == 0,
          "blue is not falsely flagged underexposed");
    ps_da_free(&photos);
    ps_image_free(&img);

    // This footprint exceeds the old 32-bit sum without excessive memory use.
    must(ps_image_init(&img, 5000, 5000));
    memset(img.pixels, 255, img.stride * img.height);
    ps_image small;
    must(ps_image_downscale(&img, 1, &small));
    check(small.width == 1 && small.height == 1 && small.pixels[0] == 255,
          "large downscale preserves white");
    ps_image_free(&small);
    ps_image_free(&img);
}

static void check_mask_and_names(void) {
    f32 values[] = {0, 1, 1, 0};
    ps_mask mask = {.values=values, .width=2, .height=2};
    check(ps_mask_sample(&mask, 1, 1, 3, 3) == 0.5f, "bilinear mask centre");
    check(ps_mask_sample(&mask, 0, 0, 3, 3) == 0.0f, "mask edge clamps");
    ps_mask_stats stats;
    ps_mask_stats_of(&mask, &stats);
    check(stats.mean == 0.5f && stats.centroid_x == 0.5f && stats.centroid_y == 0.5f,
          "mask statistics");
    char a[128], b[128], tiny[4];
    must(ps_overlay_filename("/out", "/photos", "/photos/a/b.jpg", a, sizeof(a)));
    must(ps_overlay_filename("/out", "/photos", "/photos/a_b.jpg", b, sizeof(b)));
    check(strcmp(a, b) != 0, "overlay names distinguish separators and underscores");
    check(ps_overlay_filename("/out", "/photos", "/photos/a.jpg", tiny, sizeof(tiny)) == PS_ERR_RANGE,
          "overlay names never truncate silently");
}

static void check_scores_and_clusters(void) {
    ps_da photos;
    must(ps_da_init(&photos, sizeof(ps_photo), 4));
    const char *paths[] = {"a.jpg", "b.jpg", "c.jpg", "d.jpg"};
    f32 embeddings[] = {0.0f, 0.3f, 0.6f, 2.0f};
    f32 aesthetics[] = {0.1f, 0.1f, 0.9f, 0.5f};
    for (size_t i = 0; i < 4; i++) {
        ps_photo p = {
            .path=paths[i], .aesthetic={.overall=aesthetics[i]},
            .technical={.sharp_subject=55, .sharp_ratio=1, .contrast_rms=0.2f},
            .embedding={.values=&embeddings[i], .len=1},
        };
        must(ps_da_push(&photos, &p));
    }
    ps_score_params params = ps_score_defaults();
    must(ps_score_corpus(&photos, &params));
    ps_photo *p = ps_da_at(&photos, 0);
    check(p[0].aesthetic_pct == 0.25f && p[1].aesthetic_pct == 0.25f,
          "tied scores share their percentile");
    check(p[2].score == 0.875f && p[2].gate == 1.0f, "healthy score is aesthetic rank");
    ps_dedup_stats stats;
    must(ps_dedup_cluster(&photos, 0.4f, &stats));
    check(stats.clusters == 2 && stats.duplicates == 2 && stats.largest == 3,
          "single-linkage clusters a chain");
    check(p[2].cluster_best && p[3].cluster_best && p[0].cluster_best_path == paths[2],
          "highest-score cluster member is the keeper");
    check((p[0].flags & PS_FLAG_DUPLICATE) != 0 && p[2].flags == 0,
          "only non-keepers receive duplicate flags");
    check(fabsf(stats.nearest_nonmatch - 0.6f) < 0.0001f,
          "distance diagnostics describe pairs, not cluster diameters");
    p[3].technical.sharp_subject = 0;
    must(ps_score_corpus(&photos, &params));
    check(p[3].gate == params.gate_floor && (p[3].flags & PS_FLAG_BLURRED) != 0,
          "real focus defect lowers score and sets its flag");
    ps_da_free(&photos);
}

int main(void) {
    check_allocators();
    check_cli();
    check_pixels();
    check_mask_and_names();
    check_scores_and_clusters();
    return failures != 0;
}

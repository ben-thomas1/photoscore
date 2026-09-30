#include "photoscore/image.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <stdio.h>
#include <stdlib.h>

#define SRC_W 3
#define SRC_H 2

// A 3x2 source whose pixels encode their own coordinates:
//
//     (0,0) (1,0) (2,0)
//     (0,1) (1,1) (2,1)
//
// Every expected result below is written out by hand rather than computed, so
// the test cannot agree with the implementation by sharing its arithmetic.

typedef struct {
    u32 orientation;
    size_t width;
    size_t height;
    u8 expect[6][2];   // dst row-major, each entry the source (x, y) it holds
} orient_case;

static const orient_case CASES[] = {
    { 1, 3, 2, { {0,0},{1,0},{2,0},
                 {0,1},{1,1},{2,1} } },   // identity

    { 2, 3, 2, { {2,0},{1,0},{0,0},
                 {2,1},{1,1},{0,1} } },   // mirror horizontal

    { 3, 3, 2, { {2,1},{1,1},{0,1},
                 {2,0},{1,0},{0,0} } },   // rotate 180

    { 4, 3, 2, { {0,1},{1,1},{2,1},
                 {0,0},{1,0},{2,0} } },   // mirror vertical

    { 5, 2, 3, { {0,0},{0,1},
                 {1,0},{1,1},
                 {2,0},{2,1} } },         // transpose

    { 6, 2, 3, { {0,1},{0,0},
                 {1,1},{1,0},
                 {2,1},{2,0} } },         // rotate 90 CW

    { 7, 2, 3, { {2,1},{2,0},
                 {1,1},{1,0},
                 {0,1},{0,0} } },         // transverse

    { 8, 2, 3, { {2,0},{2,1},
                 {1,0},{1,1},
                 {0,0},{0,1} } },         // rotate 90 CCW
};

static int failures = 0;

static void make_src(ps_image *out) {
    if (ps_image_init(out, SRC_W, SRC_H) != PS_OK) {
        fprintf(stderr, "ps_image_init failed\n");
        exit(1);
    }

    for (size_t y = 0; y < SRC_H; y++) {
        for (size_t x = 0; x < SRC_W; x++) {
            u8 *p = ps_image_at(out, x, y);
            p[0] = (u8)x;
            p[1] = (u8)y;
            p[2] = 0x40;
        }
    }
}

static void check_case(const orient_case *c) {
    ps_image img;
    make_src(&img);

    ps_status_e s = ps_image_apply_orientation(&img, c->orientation);
    if (s != PS_OK) {
        printf("  o%u: apply failed (%d)\n", c->orientation, (int)s);
        failures += 1;
        ps_image_free(&img);
        return;
    }

    if (img.width != c->width || img.height != c->height) {
        printf("  o%u: dims %zux%zu, want %zux%zu\n",
               c->orientation, img.width, img.height, c->width, c->height);
        failures += 1;
        ps_image_free(&img);
        return;
    }

    size_t i = 0;
    for (size_t y = 0; y < img.height; y++) {
        for (size_t x = 0; x < img.width; x++, i++) {
            const u8 *p  = ps_image_at(&img, x, y);
            u8 want_x = c->expect[i][0];
            u8 want_y = c->expect[i][1];

            if (p[0] != want_x || p[1] != want_y || p[2] != 0x40) {
                printf("  o%u: dst(%zu,%zu) holds src(%u,%u), want src(%u,%u)\n",
                       c->orientation, x, y, p[0], p[1], want_x, want_y);
                failures += 1;
            }
        }
    }

    ps_image_free(&img);
}

// Orientations pair up as mutual inverses; applying both must restore the
// original bytes, including for the self-inverse ones.
static void check_roundtrip(u32 forward, u32 back) {
    ps_image a;
    ps_image b;

    make_src(&a);
    make_src(&b);

    if (ps_image_apply_orientation(&a, forward) != PS_OK ||
        ps_image_apply_orientation(&a, back) != PS_OK) {
        printf("  o%u then o%u: apply failed\n", forward, back);
        failures += 1;
    } else if (a.width != b.width || a.height != b.height) {
        printf("  o%u then o%u: dims %zux%zu, want %zux%zu\n",
               forward, back, a.width, a.height, b.width, b.height);
        failures += 1;
    } else {
        for (size_t y = 0; y < a.height; y++) {
            for (size_t x = 0; x < a.width; x++) {
                const u8 *pa = ps_image_at(&a, x, y);
                const u8 *pb = ps_image_at(&b, x, y);

                if (pa[0] != pb[0] || pa[1] != pb[1]) {
                    printf("  o%u then o%u: dst(%zu,%zu) not restored\n",
                           forward, back, x, y);
                    failures += 1;
                }
            }
        }
    }

    ps_image_free(&a);
    ps_image_free(&b);
}

static void check_edges(void) {
    ps_image img;
    make_src(&img);

    // Out-of-range tags leave the image untouched rather than corrupting it.
    if (ps_image_apply_orientation(&img, 0) != PS_OK ||
        ps_image_apply_orientation(&img, 9) != PS_OK ||
        ps_image_apply_orientation(&img, 4242) != PS_OK) {
        printf("  out-of-range orientation returned an error\n");
        failures += 1;
    }
    if (img.width != SRC_W || img.height != SRC_H) {
        printf("  out-of-range orientation changed dims\n");
        failures += 1;
    }
    ps_image_free(&img);

    if (ps_image_apply_orientation(NULL, 6) != PS_ERR_INVALID) {
        printf("  NULL image not rejected\n");
        failures += 1;
    }
    if (ps_image_init(&img, 0, 10) != PS_ERR_INVALID) {
        printf("  zero width not rejected\n");
        failures += 1;
    }
    if (ps_image_init(&img, SIZE_MAX, SIZE_MAX) != PS_ERR_NOMEM) {
        printf("  overflowing dimensions not rejected\n");
        failures += 1;
    }
}

int main(void) {
    printf("orientation permutations:\n");
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        check_case(&CASES[i]);
    }

    printf("inverse round-trips:\n");
    check_roundtrip(2, 2);
    check_roundtrip(3, 3);
    check_roundtrip(4, 4);
    check_roundtrip(5, 5);
    check_roundtrip(7, 7);
    check_roundtrip(6, 8);
    check_roundtrip(8, 6);

    printf("edge cases:\n");
    check_edges();

    if (failures == 0) {
        printf("ok: all orientation cases pass\n");
        return 0;
    }

    printf("FAILED: %d check(s)\n", failures);
    return 1;
}

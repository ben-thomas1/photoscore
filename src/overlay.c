#include "photoscore/overlay.h"
#include "photoscore/image.h"
#include "photoscore/mask.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// How dark the background is pushed. Low enough that the subject reads at a
// glance, high enough that background detail stays checkable.
#define BACKGROUND_LEVEL 0.28f

ps_status_e ps_overlay_compose(const ps_image *src, const ps_mask *mask, ps_image *out) {
    if (src == NULL || src->pixels == NULL || mask == NULL || out == NULL) {
        return PS_ERR_INVALID;
    }

    // Shrink first, then weight: the mask is smooth and low-resolution, so
    // sampling it at native resolution would cost far more for no visible gain.
    ps_image small;
    ps_status_e s = ps_image_downscale(src, PS_OVERLAY_MAX_EDGE, &small);
    if (s != PS_OK) { return s; }

    for (size_t y = 0; y < small.height; y++) {
        u8 *row = small.pixels + (y * small.stride);

        for (size_t x = 0; x < small.width; x++) {
            f32 sal = ps_mask_sample(mask, x, y, small.width, small.height);

            if (sal < 0.0f) { sal = 0.0f; }
            if (sal > 1.0f) { sal = 1.0f; }

            u8 *p = row + (x * 3);

            f32 r = (f32)p[0];
            f32 g = (f32)p[1];
            f32 b = (f32)p[2];

            // Rec. 601 luma, matching how the technical metrics will read it.
            f32 luma = (0.299f * r) + (0.587f * g) + (0.114f * b);
            f32 dim  = luma * BACKGROUND_LEVEL;

            p[0] = (u8)(dim + ((r - dim) * sal));
            p[1] = (u8)(dim + ((g - dim) * sal));
            p[2] = (u8)(dim + ((b - dim) * sal));
        }
    }

    *out = small;

    return PS_OK;
}

ps_status_e ps_overlay_prepare_directory(const char *dir, const char *root) {
    if (dir == NULL || root == NULL) { return PS_ERR_INVALID; }

    char input[PATH_MAX];
    char output[PATH_MAX];
    if (realpath(root, input) == NULL) { return PS_ERR_IO; }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) { return PS_ERR_IO; }
    if (realpath(dir, output) == NULL) { return PS_ERR_IO; }

    struct stat st;
    if (stat(output, &st) != 0 || !S_ISDIR(st.st_mode)) { return PS_ERR_INVALID; }
    size_t n = strlen(input);
    if (strncmp(input, output, n) == 0 &&
        (n == 1 || output[n] == '/' || output[n] == '\0')) {
        return PS_ERR_INVALID;
    }
    return PS_OK;
}

ps_status_e ps_overlay_filename(const char *dir, const char *root, const char *path,
                                char *out, size_t cap) {
    if (out == NULL || cap == 0) { return PS_ERR_INVALID; }

    out[0] = '\0';
    if (dir == NULL || path == NULL) { return PS_ERR_INVALID; }

    // Drop the root prefix so names stay short, then flatten what is left.
    const char *rel = path;
    if (root != NULL) {
        size_t rlen = strlen(root);

        if (rlen > 0 && strncmp(path, root, rlen) == 0 &&
            (root[rlen - 1] == '/' || path[rlen] == '/')) {
            rel = path + rlen;
            while (*rel == '/') { rel += 1; }
        }
    }

    size_t n = strlen(dir);
    if (n >= cap - 1) { return PS_ERR_RANGE; }
    memcpy(out, dir, n);
    out[n++] = '/';

    // Escaping underscores as well as slashes keeps this mapping one-to-one.
    for (size_t i = 0; rel[i] != '\0'; i++) {
        char c = rel[i];
        size_t needed = (c == '/' || c == '_') ? 2 : 1;
        if (needed >= cap - n) { out[0] = '\0'; return PS_ERR_RANGE; }
        if (needed == 2) { out[n++] = '_'; }
        out[n++] = (c == '/') ? 's' : c;
    }

    const char suffix[] = ".overlay.jpg";
    if (sizeof(suffix) > cap - n) { out[0] = '\0'; return PS_ERR_RANGE; }
    memcpy(out + n, suffix, sizeof(suffix));
    return PS_OK;
}

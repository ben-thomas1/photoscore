#include "photoscore/backend.h"
#include "photoscore/overlay.h"
#include "photoscore/walk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void check(bool ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); failures++; }
}

static void must(ps_status_e s) {
    if (s != PS_OK) { fprintf(stderr, "setup failed: %d\n", s); exit(1); }
}

static void touch(const char *path) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) { exit(1); }
    fclose(f);
}

static unsigned char *read_bytes(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (f == NULL || fseek(f, 0, SEEK_END) != 0) { exit(1); }
    long length = ftell(f);
    if (length < 0) { exit(1); }
    *size = (size_t)length;
    rewind(f);
    unsigned char *data = malloc(*size);
    if (data == NULL || fread(data, 1, *size, f) != *size) { exit(1); }
    fclose(f);
    return data;
}

int main(void) {
    char base[] = "/tmp/photoscore-io-XXXXXX";
    if (mkdtemp(base) == NULL) { return 1; }
    char root[512], output[512], photo[512], alias[512], bundle[512], child[512];
    char hidden[512], bundled_photo[512], alias_dir[512], dangling[512];
    snprintf(root, sizeof(root), "%s/input", base);
    snprintf(output, sizeof(output), "%s/output", base);
    snprintf(photo, sizeof(photo), "%s/input/scene.jpg", base);
    snprintf(alias, sizeof(alias), "%s/input/link.jpg", base);
    snprintf(dangling, sizeof(dangling), "%s/input/dangling.jpg", base);
    snprintf(bundle, sizeof(bundle), "%s/input/library.photoslibrary", base);
    snprintf(child, sizeof(child), "%s/input/overlays", base);
    snprintf(hidden, sizeof(hidden), "%s/input/.hidden.jpg", base);
    snprintf(bundled_photo, sizeof(bundled_photo), "%s/input/library.photoslibrary/inside.jpg", base);
    snprintf(alias_dir, sizeof(alias_dir), "%s/alias", base);
    if (mkdir(root, 0700) != 0 || mkdir(bundle, 0700) != 0) { return 1; }

    ps_backend backend;
    must(ps_backend_init(&backend));
    ps_image img;
    must(ps_image_init(&img, 256, 256));
    for (size_t y = 0; y < img.height; y++) {
        for (size_t x = 0; x < img.width; x++) {
            u8 *p = ps_image_at(&img, x, y);
            p[0] = (u8)x; p[1] = (u8)y;
            p[2] = (u8)(((x / 8 + y / 8) % 2) * 180 + 32);
        }
    }
    must(ps_backend_write_jpeg(&backend, photo, &img, 0.9f));
    size_t before_len, after_len;
    unsigned char *before = read_bytes(photo, &before_len);
    check(ps_backend_write_jpeg(&backend, photo, &img, 0.1f) == PS_ERR_EXISTS,
          "writer refuses to overwrite an existing image");
    if (symlink(photo, alias) != 0 || symlink("missing-target", dangling) != 0) { return 1; }
    check(ps_backend_write_jpeg(&backend, alias, &img, 0.1f) == PS_ERR_EXISTS,
          "writer refuses a symlink destination");
    check(ps_backend_write_jpeg(&backend, dangling, &img, 0.1f) == PS_ERR_EXISTS,
          "writer refuses a dangling symlink destination");
    unsigned char *after = read_bytes(photo, &after_len);
    check(before_len == after_len && memcmp(before, after, before_len) == 0,
          "existing photo bytes remain unchanged");
    free(before); free(after);

    ps_image decoded;
    must(ps_backend_decode(&backend, photo, &decoded, NULL));
    check(decoded.width == img.width && decoded.height == img.height, "JPEG round trip dimensions");
    ps_image_free(&decoded);

    // Smoke-test the actual platform models on generated pixels, without
    // asserting a particular subjective score or retaining private fixtures.
    ps_aesthetic aesthetic;
    must(ps_backend_aesthetics(&backend, &img, &aesthetic));
    check(aesthetic.overall >= -1 && aesthetic.overall <= 1, "aesthetic model output range");
    ps_mask mask;
    must(ps_backend_saliency(&backend, &img, &mask));
    check(mask.width > 0 && mask.height > 0, "saliency model returns a mask");
    ps_mask_free(&mask);
    ps_embedding embedding;
    must(ps_backend_embed(&backend, &img, &embedding));
    check(embedding.len > 0 && ps_embedding_distance(&embedding, &embedding) == 0,
          "feature print has zero self-distance");
    ps_embedding_free(&embedding);
    ps_image_free(&img);
    ps_backend_free(&backend);

    must(ps_overlay_prepare_directory(output, root));
    check(ps_overlay_prepare_directory(root, root) == PS_ERR_INVALID, "reject overlay at input root");
    check(ps_overlay_prepare_directory(child, root) == PS_ERR_INVALID, "reject nested overlay directory");
    if (symlink(root, alias_dir) != 0) { return 1; }
    check(ps_overlay_prepare_directory(alias_dir, root) == PS_ERR_INVALID,
          "reject symlink alias of input root");

    touch(hidden);
    touch(bundled_photo);
    ps_arena arena;
    ps_da paths;
    ps_walk_stats stats;
    must(ps_arena_init(&arena, 0));
    must(ps_da_init(&paths, sizeof(char *), 0));
    must(ps_walk(root, &arena, &paths, &stats));
    check(paths.len == 1 && strcmp(*(char **)ps_da_at(&paths, 0), photo) == 0,
          "walk skips hidden files, symlinks and bundles");
    ps_da_clear(&paths);
    if (geteuid() != 0) {
        if (chmod(root, 0000) != 0) { return 1; }
        check(ps_walk(root, &arena, &paths, &stats) == PS_ERR_IO, "unreadable root fails");
        if (chmod(root, 0700) != 0) { return 1; }
    }
    ps_da_free(&paths);
    ps_arena_free(&arena);

    unlink(alias_dir); unlink(alias); unlink(dangling); unlink(hidden);
    unlink(bundled_photo); unlink(photo);
    rmdir(bundle); rmdir(child); rmdir(output); rmdir(root); rmdir(base);
    return failures != 0;
}

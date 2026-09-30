#include "photoscore/walk.h"
#include "photoscore/arena.h"
#include "photoscore/da.h"
#include "photoscore/status.h"

#include <errno.h>
#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// Extensions ImageIO decodes that are plausibly photographs. RAW is
// deliberately absent. Compared case-insensitively.
static const char *const EXTENSIONS[] = {
    "jpg", "jpeg", "png", "heic", "heif", "webp",
    "tif", "tiff", "jp2", "jxl", "avif", "gif", "bmp",
};

// Visible directories that are really opaque bundles. Descending into these
// finds thousands of generated thumbnails and derivatives, never photographs
// the user took. Matched case-insensitively on the directory's extension.
static const char *const BUNDLE_EXTENSIONS[] = {
    "photoslibrary", "photolibrary", "aplibrary", "migratedaplibrary",
    "lrdata", "lrcat", "app", "bundle", "framework",
};

static bool is_bundle_dir(const char *name) {
    const char *dot = strrchr(name, '.');
    if (dot == NULL || dot[1] == '\0') { return false; }

    const char *ext = dot + 1;
    for (size_t i = 0; i < sizeof(BUNDLE_EXTENSIONS) / sizeof(BUNDLE_EXTENSIONS[0]); i++) {
        if (strcasecmp(ext, BUNDLE_EXTENSIONS[i]) == 0) { return true; }
    }

    return false;
}

bool ps_walk_is_candidate(const char *name) {
    // A leading dot covers hidden files and macOS `._` resource forks, which
    // are byte-for-byte not images despite carrying the same extension.
    if (name == NULL || name[0] == '.') { return false; }

    const char *dot = strrchr(name, '.');
    if (dot == NULL || dot[1] == '\0') { return false; }

    const char *ext = dot + 1;
    for (size_t i = 0; i < sizeof(EXTENSIONS) / sizeof(EXTENSIONS[0]); i++) {
        if (strcasecmp(ext, EXTENSIONS[i]) == 0) { return true; }
    }

    return false;
}

static int cmp_path(const void *a, const void *b) {
    const char *const *pa = a;
    const char *const *pb = b;

    return strcmp(*pa, *pb);
}

static ps_status_e check_root(const char *root) {
    struct stat st;

    if (stat(root, &st) != 0)  { return PS_ERR_NOTFOUND; }
    if (!S_ISDIR(st.st_mode))  { return PS_ERR_INVALID; }

    return PS_OK;
}

ps_status_e ps_walk(const char *root, ps_arena *arena, ps_da *out, ps_walk_stats *out_stats) {
    if (root == NULL || arena == NULL || out == NULL || out_stats == NULL) {
        return PS_ERR_INVALID;
    }
    if (out->elem_size != sizeof(char *)) { return PS_ERR_INVALID; }

    *out_stats = (ps_walk_stats){ 0 };

    ps_status_e s = check_root(root);
    if (s != PS_OK) { return s; }

    // fts_open does not write through this pointer, but its prototype is not
    // const-qualified, so the cast cannot be avoided.
    char *const argv[] = { (char *)(intptr_t)root, NULL };

    FTS *fts = fts_open(argv, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    if (fts == NULL) { return PS_ERR_IO; }

    FTSENT *ent;
    while (s == PS_OK) {
        errno = 0;
        ent = fts_read(fts);
        if (ent == NULL) {
            if (errno != 0) { s = PS_ERR_IO; }
            break;
        }
        switch (ent->fts_info) {
            case FTS_D:
                // Skip hidden dirs and bundles, but never the root itself:
                // naming it explicitly is a deliberate request to look inside.
                if (ent->fts_level > 0 &&
                    (ent->fts_name[0] == '.' || is_bundle_dir(ent->fts_name))) {
                    fts_set(fts, ent, FTS_SKIP);
                    out_stats->bundles += 1;
                } else {
                    out_stats->dirs += 1;
                }
                break;

            case FTS_F:
                out_stats->files += 1;

                if (!ps_walk_is_candidate(ent->fts_name)) {
                    out_stats->skipped += 1;
                    break;
                }

                // fts reuses fts_path between entries, so the string has to be
                // copied out before the next fts_read.
                char *path = ps_arena_strdup(arena, ent->fts_path);
                if (path == NULL) {
                    s = PS_ERR_NOMEM;
                    break;
                }

                s = ps_da_push(out, &path);
                if (s == PS_OK) { out_stats->candidates += 1; }
                break;

            case FTS_SL:
            case FTS_SLNONE:
                out_stats->symlinks += 1;
                break;

            case FTS_DNR:
            case FTS_ERR:
            case FTS_NS:
                out_stats->errors += 1;
                fprintf(stderr, "photoscore: %s: %s\n",
                        ent->fts_path, strerror(ent->fts_errno));
                if (ent->fts_level == 0) { s = PS_ERR_IO; }
                break;

            default:
                break;
        }
    }

    if (fts_close(fts) != 0 && s == PS_OK) { s = PS_ERR_IO; }

    if (s == PS_OK && out->len > 1) {
        qsort(out->items, out->len, out->elem_size, cmp_path);
    }

    return s;
}

#include "photoscore/cli.h"
#include "photoscore/aesthetic.h"
#include "photoscore/arena.h"
#include "photoscore/backend.h"
#include "photoscore/da.h"
#include "photoscore/dedup.h"
#include "photoscore/embedding.h"
#include "photoscore/exif.h"
#include "photoscore/image.h"
#include "photoscore/mask.h"
#include "photoscore/overlay.h"
#include "photoscore/photo.h"
#include "photoscore/progress.h"
#include "photoscore/score.h"
#include "photoscore/status.h"
#include "photoscore/technical.h"
#include "photoscore/types.h"
#include "photoscore/walk.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define HIST_BUCKETS 20

static f64 now_seconds(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (f64)ts.tv_sec + ((f64)ts.tv_nsec / 1e9);
}

// Shutter reads as a fraction below a second, which is how it is written on a
// camera and how it is recognisable in a report.
static void format_shutter(const ps_exif *e, char *out, size_t cap) {
    if (!ps_exif_has(e, PS_EXIF_EXPOSURE_TIME) || e->exposure_time <= 0.0) {
        snprintf(out, cap, "-");
        return;
    }

    if (e->exposure_time >= 1.0) {
        snprintf(out, cap, "%.1fs", e->exposure_time);
    } else {
        snprintf(out, cap, "1/%.0f", 1.0 / e->exposure_time);
    }
}

// Keeps fmt a literal at the snprintf call site, which -Wformat=2 requires.
#define FORMAT_OR_DASH(buf, present, fmt, val)                          \
    do {                                                                \
        if (present) { snprintf((buf), sizeof(buf), fmt, (val)); }      \
        else         { snprintf((buf), sizeof(buf), "-"); }             \
    } while (0)

// Header and rows share one format string so the columns cannot drift apart.
// Every field is passed as a string for that reason.
#define ROW_FMT     "%-6s  %-5s  %-5s  %-5s  %-4s  %-8s  %-7s  %-6s  %-6s  %-5s  %-5s"
#define ROW_FMT_V   "  %-7s  %-5s  %-6s  %-5s  %-5s  %-6s  %-11s  %-6s  %-7s  %-6s  %-6s  %-22s"
#define ROW_FMT_END "  %s\n"

static void print_header(bool verbose) {
    printf(ROW_FMT, "SCORE", "AES", "TECH", "GATE", "UTIL", "DUP",
           "SHARP-S", "EV", "CLIP-H", "NOISE", "COLOR");
    if (verbose) {
        printf(ROW_FMT_V, "SHARP-B", "RATIO", "CLIP-L", "CONTR", "THIRD", "CENTRE",
               "PIXELS", "ISO", "SHUTTER", "F-STOP", "FOCAL", "CAMERA");
    }
    printf(ROW_FMT_END, "PATH");

    printf(ROW_FMT, "------", "-----", "-----", "-----", "----", "--------",
           "-------", "------", "------", "-----", "-----");
    if (verbose) {
        printf(ROW_FMT_V, "-------", "-----", "------", "-----", "-----", "------",
               "-----------", "------", "-------", "------", "------",
               "----------------------");
    }
    printf(ROW_FMT_END, "----");
}

static void print_row(const ps_photo *p, bool verbose) {
    const ps_exif      *e = &p->exif;
    const ps_technical *t = &p->technical;

    char score[16];
    char aes[16];
    char tech[16];
    char gate[16];
    char sharp_s[16];
    char ev[16];
    char clip_h[16];
    char noise[16];
    char color[16];

    snprintf(score,   sizeof(score),   "%.3f",  (f64)p->score);
    snprintf(aes,     sizeof(aes),     "%.3f",  (f64)p->aesthetic_pct);
    snprintf(tech,    sizeof(tech),    "%.3f",  (f64)p->technical_quality);
    snprintf(gate,    sizeof(gate),    "%.3f",  (f64)p->gate);
    snprintf(sharp_s, sizeof(sharp_s), "%.0f",  (f64)t->sharp_subject);
    snprintf(ev,      sizeof(ev),      "%+.2f", (f64)t->ev_offset);
    snprintf(clip_h,  sizeof(clip_h),  "%.2f%%", (f64)t->clip_high * 100.0);
    snprintf(noise,   sizeof(noise),   "%.2f",  (f64)t->noise_sigma);
    snprintf(color,   sizeof(color),   "%.1f",  (f64)t->colourfulness);

    char dup[16];
    if (p->cluster_size <= 1) {
        snprintf(dup, sizeof(dup), "-");
    } else {
        snprintf(dup, sizeof(dup), "%s%u",
                 p->cluster_best ? "best/" : "dup/", p->cluster_size);
    }

    printf(ROW_FMT, score, aes, tech, gate, p->aesthetic.is_utility ? "YES" : "-",
           dup, sharp_s, ev, clip_h, noise, color);

    if (verbose) {
        char sharp_b[16];
        char ratio[16];
        char clip_l[16];
        char contr[16];
        char third[16];
        char centre[16];
        char dims[16];
        char shutter[16];
        char iso[16];
        char aperture[16];
        char focal[16];

        snprintf(sharp_b, sizeof(sharp_b), "%.0f", (f64)t->sharp_background);
        snprintf(ratio,  sizeof(ratio),  "%.2f",   (f64)t->sharp_ratio);
        snprintf(clip_l, sizeof(clip_l), "%.2f%%", (f64)t->clip_low * 100.0);
        snprintf(contr,  sizeof(contr),  "%.3f",   (f64)t->contrast_rms);
        snprintf(third,  sizeof(third),  "%.3f",   (f64)t->thirds_distance);
        snprintf(centre, sizeof(centre), "%.3f",   (f64)t->centre_distance);
        snprintf(dims,   sizeof(dims),   "%zux%zu", p->width, p->height);

        format_shutter(e, shutter, sizeof(shutter));
        FORMAT_OR_DASH(iso,      ps_exif_has(e, PS_EXIF_ISO),        "%.0f",   e->iso);
        FORMAT_OR_DASH(aperture, ps_exif_has(e, PS_EXIF_F_NUMBER),   "f/%.1f", e->f_number);
        FORMAT_OR_DASH(focal,    ps_exif_has(e, PS_EXIF_FOCAL_35MM), "%.0fmm", e->focal_35mm);

        printf(ROW_FMT_V, sharp_b, ratio, clip_l, contr, third, centre, dims,
               iso, shutter, aperture, focal,
               (e->camera[0] != '\0') ? e->camera : "-");
    }

    printf(ROW_FMT_END, p->path);
}

// Short list of what is wrong, in the units the measurement was taken in.
// "composition 0.42" tells you nothing; "subject soft, 4 against 38 on the
// background" tells you the lens focused past the subject.
static void format_reasons(const ps_photo *p, char *out, size_t cap) {
    const ps_technical *t = &p->technical;

    size_t n = 0;
    out[0] = '\0';

    #define APPEND(...)                                                     \
        do {                                                                \
            if (n < cap) {                                                  \
                if (n > 0) { n += (size_t)snprintf(out + n, cap - n, "; "); } \
                if (n < cap) { n += (size_t)snprintf(out + n, cap - n, __VA_ARGS__); } \
            }                                                               \
        } while (0)

    if ((p->flags & PS_FLAG_DUPLICATE) != 0) {
        const char *keeper = p->cluster_best_path;
        const char *base   = (keeper != NULL) ? strrchr(keeper, '/') : NULL;

        APPEND("one of %u near-identical frames, best is %s",
               p->cluster_size, (base != NULL) ? base + 1 : "another");
    }
    if ((p->flags & PS_FLAG_BLURRED) != 0) {
        APPEND("subject soft, Laplacian %.0f against %.0f on the background",
               (f64)t->sharp_subject, (f64)t->sharp_background);
    }
    if ((p->flags & PS_FLAG_OVEREXPOSED) != 0) {
        APPEND("%.1f%% of the frame blown, %+.1f EV",
               (f64)t->clip_high * 100.0, (f64)t->ev_offset);
    }
    if ((p->flags & PS_FLAG_UNDEREXPOSED) != 0) {
        APPEND("%+.1f EV, %.1f%% crushed", (f64)t->ev_offset, (f64)t->clip_low * 100.0);
    }
    if ((p->flags & PS_FLAG_UTILITY) != 0) {
        APPEND("content judged unmemorable");
    }

    #undef APPEND
}

static void format_flags(const ps_photo *p, char *out, size_t cap) {
    static const ps_flag_e ALL[PS_FLAG_COUNT] = {
        PS_FLAG_DUPLICATE, PS_FLAG_UTILITY, PS_FLAG_BLURRED,
        PS_FLAG_UNDEREXPOSED, PS_FLAG_OVEREXPOSED,
    };

    size_t n = 0;
    out[0] = '\0';

    for (size_t i = 0; i < PS_FLAG_COUNT; i++) {
        if ((p->flags & (u32)ALL[i]) == 0) { continue; }
        if (n >= cap) { break; }

        if (n > 0) { n += (size_t)snprintf(out + n, cap - n, ","); }
        if (n < cap) { n += (size_t)snprintf(out + n, cap - n, "%s", ps_flag_name(ALL[i])); }
    }

    if (n == 0) { snprintf(out, cap, "-"); }
}

// Worst first: the point of the list is to work down it deleting things.
static int cmp_score_asc(const void *a, const void *b) {
    const ps_photo *pa = a;
    const ps_photo *pb = b;

    if (pa->score < pb->score) { return -1; }
    if (pa->score > pb->score) { return  1; }

    return 0;
}

static void print_path(const char *path) {
    fputs(path, stdout);
    fputc('\0', stdout);
}

static void print_flagged(const ps_da *photos, bool print0) {
    size_t shown = 0;

    if (!print0) {
        printf("%-34s  %-6s  %-58s  %s\n", "FLAGS", "SCORE", "REASON", "PATH");
        printf("%-34s  %-6s  %-58s  %s\n",
               "----------------------------------", "------",
               "----------------------------------------------------------", "----");
    }

    for (size_t i = 0; i < photos->len; i++) {
        const ps_photo *p = ps_da_at(photos, i);
        if (p->flags == 0) { continue; }
        if (print0) { print_path(p->path); continue; }

        char flags[64];
        char reason[256];
        char score[16];

        format_flags(p, flags, sizeof(flags));
        format_reasons(p, reason, sizeof(reason));
        snprintf(score, sizeof(score), "%.3f", (f64)p->score);

        printf("%-34s  %-6s  %-58s  %s\n", flags, score, reason, p->path);
        shown += 1;
    }

    if (shown == 0 && !print0) {
        printf("(nothing flagged)\n");
    }
}

static int cmp_score_desc(const void *a, const void *b) {
    const ps_photo *pa = a;
    const ps_photo *pb = b;

    if (pa->score < pb->score) { return  1; }
    if (pa->score > pb->score) { return -1; }

    return 0;
}

// The central open question for this stage: does the model actually spread
// these photographs out, or bunch them into a narrow band? Percentile ranking
// would hide the difference, so the raw values get reported here.
static void print_histogram(const ps_da *photos) {
    size_t counts[HIST_BUCKETS] = { 0 };
    size_t widest = 0;

    f32 lo = 1.0f;
    f32 hi = -1.0f;
    f64 sum = 0.0;

    for (size_t i = 0; i < photos->len; i++) {
        const ps_photo *p = ps_da_at(photos, i);
        f32 v = p->aesthetic.overall;

        if (v < lo) { lo = v; }
        if (v > hi) { hi = v; }
        sum += (f64)v;

        // Map -1..1 onto the buckets, clamping the top edge inclusive.
        f64 t = ((f64)v + 1.0) / 2.0;
        size_t b = (size_t)(t * (f64)HIST_BUCKETS);
        if (b >= HIST_BUCKETS) { b = HIST_BUCKETS - 1; }

        counts[b] += 1;
        if (counts[b] > widest) { widest = counts[b]; }
    }

    if (photos->len == 0 || widest == 0) { return; }

    fprintf(stderr, "\nraw aesthetic score distribution (n=%zu):\n", photos->len);

    for (size_t b = 0; b < HIST_BUCKETS; b++) {
        f64 edge_lo = -1.0 + (2.0 * (f64)b / (f64)HIST_BUCKETS);
        f64 edge_hi = edge_lo + (2.0 / (f64)HIST_BUCKETS);

        fprintf(stderr, "  %+.2f..%+.2f  ", edge_lo, edge_hi);

        size_t bar = (counts[b] * 40) / widest;
        for (size_t i = 0; i < bar; i++) { fputc('#', stderr); }

        if (counts[b] > 0) { fprintf(stderr, " %zu", counts[b]); }
        fputc('\n', stderr);
    }

    fprintf(stderr, "  min %+.3f  max %+.3f  mean %+.3f  span %.3f of 2.0\n",
            (f64)lo, (f64)hi, sum / (f64)photos->len, (f64)(hi - lo));

    size_t utility = 0;
    for (size_t i = 0; i < photos->len; i++) {
        const ps_photo *p = ps_da_at(photos, i);
        if (p->aesthetic.is_utility) { utility += 1; }
    }

    fprintf(stderr, "  utility-flagged %zu/%zu\n", utility, photos->len);

    // The flatness threshold decides whether a photo gets a subject/background
    // split at all, so the spread it is judged against is worth seeing.
    f32 sd_lo = 1.0f;
    f32 sd_hi = 0.0f;
    f32 ar_lo = 1.0f;
    f32 ar_hi = 0.0f;
    f64 sd_sum = 0.0;

    for (size_t i = 0; i < photos->len; i++) {
        const ps_photo *p = ps_da_at(photos, i);

        if (p->saliency.stddev < sd_lo) { sd_lo = p->saliency.stddev; }
        if (p->saliency.stddev > sd_hi) { sd_hi = p->saliency.stddev; }
        if (p->saliency.area   < ar_lo) { ar_lo = p->saliency.area; }
        if (p->saliency.area   > ar_hi) { ar_hi = p->saliency.area; }

        sd_sum += (f64)p->saliency.stddev;
    }

    fprintf(stderr, "  saliency stddev %.3f..%.3f (mean %.3f)  subject area %.2f..%.2f\n",
            (f64)sd_lo, (f64)sd_hi, sd_sum / (f64)photos->len, (f64)ar_lo, (f64)ar_hi);
}

// Keep scoring after an overlay failure, but report the run as incomplete.
static ps_status_e write_overlay(const ps_backend *backend, ps_progress *prog,
                          const char *dir, const char *root, const char *path,
                          const ps_image *img, const ps_mask *mask) {
    ps_image over;

    ps_status_e s = ps_overlay_compose(img, mask, &over);
    if (s != PS_OK) {
        ps_progress_clear(prog);
        fprintf(stderr, "photoscore: %s: overlay: %s\n", path, ps_status_str(s));
        return s;
    }

    char out_path[1024];
    s = ps_overlay_filename(dir, root, path, out_path, sizeof(out_path));

    if (s == PS_OK) {
        s = ps_backend_write_jpeg(backend, out_path, &over, PS_OVERLAY_QUALITY);
    }
    if (s != PS_OK) {
        ps_progress_clear(prog);
        fprintf(stderr, "photoscore: %s: overlay: %s\n", path, ps_status_str(s));
    }

    ps_image_free(&over);
    return s;
}

static int run(const ps_cli_opts *opts) {
    const char *root = opts->root;
    ps_arena      arena;
    ps_backend    backend;
    ps_da         paths;
    ps_da         photos;
    ps_walk_stats stats;

    ps_status_e s = ps_backend_init(&backend);
    if (s != PS_OK) {
        fprintf(stderr, "photoscore: backend: %s\n", ps_status_str(s));
        return PS_EXIT_FAIL;
    }

    if (opts->overlay_dir != NULL) {
        s = ps_overlay_prepare_directory(opts->overlay_dir, root);
        if (s != PS_OK) {
            fprintf(stderr, "photoscore: %s: overlay directory must be writable and "
                            "outside the input tree: %s\n", opts->overlay_dir, ps_status_str(s));
            ps_backend_free(&backend);
            return PS_EXIT_FAIL;
        }
    }

    s = ps_arena_init(&arena, 0);
    if (s != PS_OK) {
        ps_backend_free(&backend);
        return PS_EXIT_FAIL;
    }

    s = ps_da_init(&paths, sizeof(char *), 0);
    if (s != PS_OK) {
        ps_arena_free(&arena);
        ps_backend_free(&backend);
        return PS_EXIT_FAIL;
    }

    s = ps_walk(root, &arena, &paths, &stats);
    if (s != PS_OK) {
        fprintf(stderr, "photoscore: %s: %s\n", root, ps_status_str(s));
        ps_da_free(&paths);
        ps_arena_free(&arena);
        ps_backend_free(&backend);
        return PS_EXIT_FAIL;
    }

    s = ps_da_init(&photos, sizeof(ps_photo), paths.len);
    if (s != PS_OK) {
        ps_da_free(&paths);
        ps_arena_free(&arena);
        ps_backend_free(&backend);
        return PS_EXIT_FAIL;
    }

    size_t failed          = 0;
    size_t saliency_failed = 0;
    size_t overlay_failed  = 0;
    int result = PS_EXIT_FAIL;

    f64 pixels      = 0.0;
    f64 decode_s    = 0.0;
    f64 score_s     = 0.0;
    f64 saliency_s  = 0.0;
    f64 technical_s = 0.0;
    f64 embed_s     = 0.0;
    f64 started     = now_seconds();

    ps_progress prog;
    ps_progress_begin(&prog, "scoring", paths.len);

    for (size_t i = 0; i < paths.len; i++) {
        const char *path = *(char **)ps_da_at(&paths, i);

        ps_image img;
        ps_exif  exif;

        f64 t0 = now_seconds();
        ps_status_e ds = ps_backend_decode(&backend, path, &img, &exif);
        decode_s += now_seconds() - t0;

        if (ds != PS_OK) {
            failed += 1;
            ps_progress_clear(&prog);
            fprintf(stderr, "photoscore: %s: %s\n", path, ps_status_str(ds));
            ps_progress_tick(&prog, path);
            continue;
        }

        ps_aesthetic aesthetic;

        t0 = now_seconds();
        ps_status_e as = ps_backend_aesthetics(&backend, &img, &aesthetic);
        score_s += now_seconds() - t0;

        if (as != PS_OK) {
            failed += 1;
            ps_progress_clear(&prog);
            fprintf(stderr, "photoscore: %s: aesthetics: %s\n", path, ps_status_str(as));
            ps_image_free(&img);
            ps_progress_tick(&prog, path);
            continue;
        }

        // Saliency is not fatal: without it the whole frame is the subject,
        // which is the same fallback a flat map already triggers.
        ps_mask       mask = { 0 };
        ps_mask_stats mstats;

        t0 = now_seconds();
        ps_status_e ss = ps_backend_saliency(&backend, &img, &mask);
        saliency_s += now_seconds() - t0;

        if (ss == PS_OK) {
            ps_mask_stats_of(&mask, &mstats);
        } else {
            mstats = (ps_mask_stats){ .area = 1.0f, .centroid_x = 0.5f, .centroid_y = 0.5f };
            saliency_failed += 1;
            ps_progress_clear(&prog);
            fprintf(stderr, "photoscore: %s: saliency: %s\n", path, ps_status_str(ss));
        }

        if (opts->overlay_dir != NULL) {
            if (ss != PS_OK || write_overlay(&backend, &prog, opts->overlay_dir,
                                            root, path, &img, &mask) != PS_OK) {
                overlay_failed += 1;
            }
        }

        ps_technical tech;

        t0 = now_seconds();
        ps_status_e ts = ps_technical_measure(&img, (ss == PS_OK) ? &mask : NULL, &tech);
        technical_s += now_seconds() - t0;

        if (ts != PS_OK) {
            failed += 1;
            ps_progress_clear(&prog);
            fprintf(stderr, "photoscore: %s: technical: %s\n", path, ps_status_str(ts));
            ps_mask_free(&mask);
            ps_image_free(&img);
            ps_progress_tick(&prog, path);
            continue;
        }

        ps_mask_free(&mask);

        ps_embedding embedding = { 0 };

        t0 = now_seconds();
        ps_status_e es = ps_backend_embed(&backend, &img, &embedding);
        embed_s += now_seconds() - t0;

        if (es != PS_OK) {
            failed += 1;
            ps_progress_clear(&prog);
            fprintf(stderr, "photoscore: %s: feature print: %s\n", path, ps_status_str(es));
            ps_embedding_free(&embedding);
            ps_image_free(&img);
            ps_progress_tick(&prog, path);
            continue;
        }

        pixels += (f64)img.width * (f64)img.height;

        ps_photo photo = {
            .path      = path,
            .width     = img.width,
            .height    = img.height,
            .exif      = exif,
            .aesthetic = aesthetic,
            .saliency  = mstats,
            .technical = tech,
            .embedding = embedding,
        };

        ps_image_free(&img);

        s = ps_da_push(&photos, &photo);
        ps_progress_tick(&prog, path);

        if (s != PS_OK) {
            ps_embedding_free(&embedding);
            break;
        }
    }

    ps_progress_end(&prog);
    if (s != PS_OK) {
        fprintf(stderr, "photoscore: collecting photos: %s\n", ps_status_str(s));
        goto cleanup;
    }

    f64 elapsed = now_seconds() - started;

    ps_score_params params = ps_score_defaults();

    s = ps_score_corpus(&photos, &params);
    if (s != PS_OK) {
        fprintf(stderr, "photoscore: scoring: %s\n", ps_status_str(s));
        goto cleanup;
    }

    ps_dedup_stats dedup = { 0 };

    s = ps_dedup_cluster(&photos, PS_DEDUP_THRESHOLD, &dedup);
    if (s != PS_OK) {
        fprintf(stderr, "photoscore: dedup: %s\n", ps_status_str(s));
        goto cleanup;
    }

    if (photos.len > 1) {
        qsort(photos.items, photos.len, photos.elem_size, cmp_score_desc);
    }

    if (opts->flagged) {
        if (photos.len > 1) {
            qsort(photos.items, photos.len, photos.elem_size, cmp_score_asc);
        }
        print_flagged(&photos, opts->print0);
    } else {
        if (photos.len > 0 && !opts->print0) { print_header(opts->verbose); }

        size_t shown = 0;
        for (size_t i = 0; i < photos.len; i++) {
            const ps_photo *p = ps_da_at(&photos, i);

            // One photograph per group of near-duplicates, so a burst cannot
            // fill a top-ten with ten versions of the same frame.
            if (opts->top > 0) {
                if (!p->cluster_best) { continue; }
                if (shown >= opts->top) { break; }
            }

            if (opts->print0) { print_path(p->path); }
            else { print_row(p, opts->verbose); }
            shown += 1;
        }
    }

    fprintf(stderr,
            "\nphotoscore: backend %s: found %zu, scored %zu, failed %zu "
            "(%.1f MP in %.2fs: %.2fs decode, %.2fs aesthetics, %.2fs saliency, "
            "%.2fs technical, %.2fs embed, %.0f img/s)\n",
            backend.name, paths.len, photos.len, failed, pixels / 1e6,
            elapsed, decode_s, score_s, saliency_s, technical_s, embed_s,
            (elapsed > 0.0) ? (f64)photos.len / elapsed : 0.0);

    if (saliency_failed > 0) {
        fprintf(stderr, "photoscore: saliency: %zu failed "
                        "(whole frame treated as subject)\n", saliency_failed);
    }

    {
        static const ps_flag_e ALL[PS_FLAG_COUNT] = {
            PS_FLAG_DUPLICATE, PS_FLAG_UTILITY, PS_FLAG_BLURRED,
            PS_FLAG_UNDEREXPOSED, PS_FLAG_OVEREXPOSED,
        };

        size_t any = 0;
        fprintf(stderr, "photoscore: flagged ");

        for (size_t f = 0; f < PS_FLAG_COUNT; f++) {
            size_t c = 0;
            for (size_t i = 0; i < photos.len; i++) {
                const ps_photo *p = ps_da_at(&photos, i);
                if ((p->flags & (u32)ALL[f]) != 0) { c += 1; }
            }
            fprintf(stderr, "%s%s %zu", (f > 0) ? ", " : "", ps_flag_name(ALL[f]), c);
        }

        for (size_t i = 0; i < photos.len; i++) {
            const ps_photo *p = ps_da_at(&photos, i);
            if (p->flags != 0) { any += 1; }
        }
        fprintf(stderr, "  (%zu of %zu photographs)\n", any, photos.len);
    }

    if (opts->overlay_dir != NULL) {
        fprintf(stderr, "photoscore: overlays: %s (%zu failed)\n",
                opts->overlay_dir, overlay_failed);
    }

    print_histogram(&photos);
    ps_score_print_params(&params, photos.len);

    fprintf(stderr,
            "  dedup     %zu groups, %zu duplicates, largest %zu"
            "  (farthest matching pair %.3f, nearest nonmatching pair %.3f)\n",
            dedup.clusters, dedup.duplicates, dedup.largest,
            (f64)dedup.farthest_match, (f64)dedup.nearest_nonmatch);

    result = (failed > 0 || overlay_failed > 0 || stats.errors > 0)
        ? PS_EXIT_FAIL : PS_EXIT_OK;
    if (fflush(stdout) != 0) { result = PS_EXIT_FAIL; }

cleanup:
    for (size_t i = 0; i < photos.len; i++) {
        ps_photo *p = ps_da_at(&photos, i);
        ps_embedding_free(&p->embedding);
    }
    ps_da_free(&photos);
    ps_da_free(&paths);
    ps_arena_free(&arena);
    ps_backend_free(&backend);

    return result;
}

int main(int argc, char **argv) {
    ps_cli_opts opts;

    ps_status_e s = ps_cli_parse(argc, argv, &opts);
    if (s != PS_OK) { return PS_EXIT_USAGE; }

    // No default case: -Wswitch flags any action added without handling here.
    switch (opts.action) {
        case PS_ACTION_HELP:
            ps_cli_usage(stdout);
            return PS_EXIT_OK;
        case PS_ACTION_VERSION:
            printf("photoscore %s\n", PS_VERSION);
            return PS_EXIT_OK;
        case PS_ACTION_SCORE:
            break;
    }

    return run(&opts);
}

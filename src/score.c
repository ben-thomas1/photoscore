#include "photoscore/score.h"
#include "photoscore/alloc.h"
#include "photoscore/da.h"
#include "photoscore/photo.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

ps_score_params ps_score_defaults(void) {
    return (ps_score_params){
        // Sharpness dominates because missed focus is the one technical fault
        // that cannot be lived with. Noise is weighted low deliberately: phone
        // pipelines denoise before we ever see the file, so it measured a span
        // of 0.0 to 4.7 across the whole test corpus and carries little signal.
        // Composition is absent on purpose - it is an aesthetic property, and
        // the aesthetic model has already judged it.
        .w_sharpness = 0.35f,
        .w_focus     = 0.10f,
        .w_exposure  = 0.20f,
        .w_highlight = 0.15f,
        .w_shadow    = 0.05f,
        .w_noise     = 0.10f,
        .w_contrast  = 0.05f,

        .sharp_floor         = 8.0f,
        .sharp_good          = 55.0f,
        .focus_floor         = 0.60f,
        .ev_tolerance        = 2.5f,
        .clip_high_tolerance = 0.05f,
        .clip_low_tolerance  = 0.05f,
        .noise_tolerance     = 8.0f,
        .contrast_good       = 0.20f,

        .gate_sharp_ok  = 15.0f,
        .gate_sharp_bad = 4.0f,
        .gate_clip_ok   = 0.20f,
        .gate_clip_bad  = 0.60f,
        .gate_ev_ok     = 2.0f,
        .gate_ev_bad    = 4.0f,
        .gate_floor     = 0.25f,
    };
}

// 1.0 at or above ok, 0 at or below bad.
static f32 require_above(f32 v, f32 ok, f32 bad) {
    if (v >= ok)  { return 1.0f; }
    if (v <= bad) { return 0.0f; }

    return (v - bad) / (ok - bad);
}

// 1.0 at or below ok, 0 at or above bad.
static f32 tolerate_up_to(f32 v, f32 ok, f32 bad) {
    if (v <= ok)  { return 1.0f; }
    if (v >= bad) { return 0.0f; }

    return 1.0f - ((v - ok) / (bad - ok));
}

// A defect term at or below this is bad enough to name the photograph as a
// deletion candidate, rather than merely docking its score.
#define FLAG_LEVEL 0.5f

static f32 defect_gate(const ps_technical *t, const ps_score_params *p, u32 *out_flags) {
    f32 ev = (t->ev_offset < 0.0f) ? -t->ev_offset : t->ev_offset;

    f32 g_focus = require_above(t->sharp_subject, p->gate_sharp_ok, p->gate_sharp_bad);
    f32 g_clip  = tolerate_up_to(t->clip_high, p->gate_clip_ok, p->gate_clip_bad);
    f32 g_ev    = tolerate_up_to(ev, p->gate_ev_ok, p->gate_ev_bad);

    // Flags come off the same terms as the gate, so a photograph can never be
    // called blown while its score goes unpenalised for it.
    if (out_flags != NULL) {
        if (g_focus <= FLAG_LEVEL) { *out_flags |= PS_FLAG_BLURRED; }
        if (g_clip  <= FLAG_LEVEL) { *out_flags |= PS_FLAG_OVEREXPOSED; }

        if (g_ev <= FLAG_LEVEL) {
            *out_flags |= (t->ev_offset < 0.0f) ? PS_FLAG_UNDEREXPOSED
                                                : PS_FLAG_OVEREXPOSED;
        }
    }

    // The worst defect decides. One ruinous fault is not offset by everything
    // else being fine.
    f32 g = g_focus;
    if (g_clip < g) { g = g_clip; }
    if (g_ev   < g) { g = g_ev; }

    if (g < p->gate_floor) { g = p->gate_floor; }
    if (g > 1.0f) { g = 1.0f; }

    return g;
}

// Rises from 0 at lo to 1 at hi.
static f32 ramp(f32 v, f32 lo, f32 hi) {
    if (hi <= lo) { return 1.0f; }

    f32 t = (v - lo) / (hi - lo);

    if (t < 0.0f) { return 0.0f; }
    if (t > 1.0f) { return 1.0f; }

    return t;
}

// Falls from 1 at zero to 0 at tolerance.
static f32 falloff(f32 v, f32 tolerance) {
    if (tolerance <= 0.0f) { return 1.0f; }
    if (v <= 0.0f) { return 1.0f; }

    f32 t = 1.0f - (v / tolerance);

    return (t < 0.0f) ? 0.0f : t;
}

static f32 technical_quality(const ps_technical *t, const ps_score_params *p) {
    f32 q_sharp = ramp(t->sharp_subject, p->sharp_floor, p->sharp_good);

    // Only a subject softer than its surroundings is a fault. A ratio above
    // one means the background is the blurred part, which is bokeh.
    f32 q_focus = (t->sharp_ratio >= 1.0f)
        ? 1.0f
        : ramp(t->sharp_ratio, p->focus_floor, 1.0f);

    f32 ev = (t->ev_offset < 0.0f) ? -t->ev_offset : t->ev_offset;

    f32 q_exposure  = falloff(ev, p->ev_tolerance);
    f32 q_highlight = falloff(t->clip_high, p->clip_high_tolerance);
    f32 q_shadow    = falloff(t->clip_low, p->clip_low_tolerance);
    f32 q_noise     = falloff(t->noise_sigma, p->noise_tolerance);
    f32 q_contrast  = ramp(t->contrast_rms, 0.0f, p->contrast_good);

    f32 sum = (p->w_sharpness * q_sharp)
            + (p->w_focus     * q_focus)
            + (p->w_exposure  * q_exposure)
            + (p->w_highlight * q_highlight)
            + (p->w_shadow    * q_shadow)
            + (p->w_noise     * q_noise)
            + (p->w_contrast  * q_contrast);

    f32 total = p->w_sharpness + p->w_focus + p->w_exposure + p->w_highlight
              + p->w_shadow + p->w_noise + p->w_contrast;

    return (total > 0.0f) ? (sum / total) : 0.0f;
}

typedef struct {
    f32    value;
    size_t index;
} ranked;

static int cmp_ranked(const void *a, const void *b) {
    const ranked *ra = a;
    const ranked *rb = b;

    if (ra->value < rb->value) { return -1; }
    if (ra->value > rb->value) { return  1; }

    return 0;
}

// Percentile rank, with ties sharing the average of the positions they span so
// that identical measurements are not ordered arbitrarily.
//
// Positions map to (r + 0.5) / n rather than r / (n - 1), which would hand the
// best photograph a flat 1.000 and the worst a flat 0.000 in every corpus, no
// matter how good either actually was. The half-step keeps both ends open, so
// the number reads as a rank within this set and not as a verdict.
static void rank_percentiles(const ranked *sorted, size_t n, f32 *out) {
    if (n == 1) {
        out[sorted[0].index] = 0.5f;   // one photograph ranks against nothing
        return;
    }

    size_t i = 0;
    while (i < n) {
        size_t j = i + 1;
        while (j < n && sorted[j].value == sorted[i].value) { j += 1; }

        f64 mean_pos = ((f64)i + (f64)(j - 1)) * 0.5;
        f32 pct = (f32)((mean_pos + 0.5) / (f64)n);

        for (size_t k = i; k < j; k++) { out[sorted[k].index] = pct; }

        i = j;
    }
}

ps_status_e ps_score_corpus(ps_da *photos, const ps_score_params *params) {
    if (photos == NULL || params == NULL) { return PS_ERR_INVALID; }
    if (photos->elem_size != sizeof(ps_photo)) { return PS_ERR_INVALID; }
    if (photos->len == 0) { return PS_OK; }

    size_t n = photos->len;

    if (n > SIZE_MAX / sizeof(ranked)) { return PS_ERR_NOMEM; }

    ranked *order = PS_MALLOC(n * sizeof(ranked));
    if (order == NULL) { return PS_ERR_NOMEM; }

    f32 *pct = PS_MALLOC(n * sizeof(f32));
    if (pct == NULL) {
        PS_FREE(order);
        return PS_ERR_NOMEM;
    }

    // Absolute first: technical quality does not depend on the other photos.
    for (size_t i = 0; i < n; i++) {
        ps_photo *p = ps_da_at(photos, i);

        p->technical_quality = technical_quality(&p->technical, params);
    }

    // Then relative: the aesthetic percentile is defined only against this
    // corpus, which is why the corpus size travels with the report.
    for (size_t i = 0; i < n; i++) {
        const ps_photo *p = ps_da_at(photos, i);

        order[i] = (ranked){ .value = p->aesthetic.overall, .index = i };
    }
    qsort(order, n, sizeof(ranked), cmp_ranked);
    rank_percentiles(order, n, pct);

    for (size_t i = 0; i < n; i++) {
        ps_photo *p = ps_da_at(photos, i);
        p->aesthetic_pct = pct[i];
    }

    for (size_t i = 0; i < n; i++) {
        const ps_photo *p = ps_da_at(photos, i);

        order[i] = (ranked){ .value = p->technical_quality, .index = i };
    }
    qsort(order, n, sizeof(ranked), cmp_ranked);
    rank_percentiles(order, n, pct);

    for (size_t i = 0; i < n; i++) {
        ps_photo *p = ps_da_at(photos, i);

        p->technical_pct = pct[i];

        // Preserve any flag already set, so a second call cannot clear one.
        u32 flags = p->flags;
        if (p->aesthetic.is_utility) { flags |= PS_FLAG_UTILITY; }

        p->gate  = defect_gate(&p->technical, params, &flags);
        p->flags = flags;
        p->score = p->aesthetic_pct * p->gate;
    }

    PS_FREE(pct);
    PS_FREE(order);

    return PS_OK;
}

void ps_score_print_params(const ps_score_params *p, size_t corpus) {
    if (p == NULL) { return; }

    fprintf(stderr,
        "\nscoring: corpus %zu   score = aesthetic percentile x gate\n"
        "  weights   sharp %.2f  focus %.2f  exposure %.2f  highlight %.2f\n"
        "            shadow %.2f  noise %.2f  contrast %.2f\n"
        "  limits    sharp %.0f-%.0f  focus %.2f  ev %.1f  clip %.0f%%/%.0f%%"
        "  noise %.1f  contrast %.2f\n"
        "  gate      worst of: sharp %.0f/%.0f  clip %.0f%%/%.0f%%  ev %.1f/%.1f"
        "  floor %.2f\n",
        corpus,
        (f64)p->w_sharpness, (f64)p->w_focus, (f64)p->w_exposure, (f64)p->w_highlight,
        (f64)p->w_shadow, (f64)p->w_noise, (f64)p->w_contrast,
        (f64)p->sharp_floor, (f64)p->sharp_good, (f64)p->focus_floor,
        (f64)p->ev_tolerance, (f64)p->clip_high_tolerance * 100.0,
        (f64)p->clip_low_tolerance * 100.0, (f64)p->noise_tolerance,
        (f64)p->contrast_good,
        (f64)p->gate_sharp_ok, (f64)p->gate_sharp_bad,
        (f64)p->gate_clip_ok * 100.0, (f64)p->gate_clip_bad * 100.0,
        (f64)p->gate_ev_ok, (f64)p->gate_ev_bad,
        (f64)p->gate_floor);
}

#pragma once

#include "photoscore/da.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

// Tuning for the whole scoring stage. Reported alongside every run, because
// the percentiles below are meaningless without knowing what produced them.
typedef struct {
    // Technical subscore weights. Expected to sum to 1.
    f32 w_sharpness;
    f32 w_focus;
    f32 w_exposure;
    f32 w_highlight;
    f32 w_shadow;
    f32 w_noise;
    f32 w_contrast;

    // Absolute tolerances, in the units the measurement is reported in.
    f32 sharp_floor;          // Laplacian p95 at or below this scores zero
    f32 sharp_good;           // and at or above this scores one
    f32 focus_floor;          // subject/background ratio below this is penalised
    f32 ev_tolerance;         // stops from mid-grey before exposure scores zero
    f32 clip_high_tolerance;  // fraction of blown pixels before scoring zero
    f32 clip_low_tolerance;
    f32 noise_tolerance;      // sigma in 0..255 units before scoring zero
    f32 contrast_good;        // RMS contrast at or above this scores one

    // The gate is deliberately one-sided: exactly 1.0 for a sound photograph,
    // so it is ranked purely on how good a picture it is, and below 1.0 only
    // when something is actually wrong. It can never push a score up - a
    // flawless photograph of nothing should not outrank a slightly flawed
    // photograph of something.
    //
    // It is taken as the worst of the individual defects below rather than
    // from the blended quality, because a blend averages catastrophe away: a
    // completely out-of-focus frame loses only the sharpness weight and still
    // scores 0.58 overall. The thresholds here are far looser than the ones
    // above - these mark "this photograph is broken", not "this could be
    // better".
    f32 gate_sharp_ok;    // subject Laplacian at or above this is in focus
    f32 gate_sharp_bad;   // and at or below this is unusable
    f32 gate_clip_ok;     // blown fraction up to here is acceptable
    f32 gate_clip_bad;
    f32 gate_ev_ok;       // stops of error up to here is recoverable
    f32 gate_ev_bad;

    f32 gate_floor;       // however bad, never multiply below this
} ps_score_params;

ps_score_params ps_score_defaults(void);

// Score a whole corpus at once. Technical quality is absolute and comparable
// between runs; the aesthetic percentile is relative to exactly these photos.
//
// photos is a ps_da of ps_photo, updated in place.
ps_status_e ps_score_corpus(ps_da *photos, const ps_score_params *params);

void ps_score_print_params(const ps_score_params *params, size_t corpus);

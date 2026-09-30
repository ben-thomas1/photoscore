#pragma once

#include "photoscore/types.h"

#include <stdbool.h>

// What the platform's aesthetic model reports for one image.
typedef struct {
    // Raw model output, -1 (least desirable) to +1 (most desirable). Kept
    // unscaled because it is absolute: comparable across runs, unlike the
    // corpus percentiles derived from it later.
    f32 overall;

    // Apple's flag for images that "may not have memorable or exciting
    // content". Not a poor-quality marker and not specifically a screenshot
    // detector, despite reading like one. There is no threshold to tune: the
    // request exposes no options and the observation exposes no score behind
    // this boolean.
    //
    // VNObservation.confidence is deliberately not carried here: it measured
    // exactly 1.000 across every photograph tested, which is the documented
    // behaviour for requests where confidence has no meaning.
    bool is_utility;
} ps_aesthetic;

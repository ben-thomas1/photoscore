#pragma once

#include "photoscore/backend.h"
#include "photoscore/status.h"

// Fill out with the implementation backed by Apple's frameworks: ImageIO for
// decode, and Vision for the model-driven slots as they land.
ps_status_e ps_backend_apple_init(ps_backend *out);

#pragma once

#include "photoscore/image.h"

#include <CoreGraphics/CoreGraphics.h>

// Wrap a decoded buffer in a CGImage without copying it. CGImage, unlike
// CGBitmapContext, accepts 24-bit RGB directly. The provider borrows the
// pixels, so the returned CGImage must not outlive img.
//
// Returns NULL on failure. Caller releases with CGImageRelease.
CGImageRef ps_apple_cg_from_image(const ps_image *img) CF_RETURNS_RETAINED;

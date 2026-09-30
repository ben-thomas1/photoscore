#pragma once

#include "photoscore/aesthetic.h"
#include "photoscore/embedding.h"
#include "photoscore/image.h"
#include "photoscore/mask.h"
#include "photoscore/status.h"

// C-callable wrappers over Vision, which is Objective-C only. Keeping them
// behind this header confines the Objective-C to vision.m.

// Returns PS_ERR_UNSUPPORTED if the running OS predates the aesthetics request.
ps_status_e ps_vision_aesthetics(const ps_image *img, ps_aesthetic *out);

// Attention-based saliency. Caller frees out with ps_mask_free.
ps_status_e ps_vision_saliency(const ps_image *img, ps_mask *out);

// Feature print for near-duplicate matching. Caller frees out with
// ps_embedding_free.
ps_status_e ps_vision_featureprint(const ps_image *img, ps_embedding *out);

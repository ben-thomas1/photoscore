#pragma once

#include "photoscore/image.h"
#include "photoscore/mask.h"
#include "photoscore/status.h"

#include <stddef.h>

// Longest edge of a written overlay. Review artefacts, not measurements, so
// they are kept small enough to flip through a thousand of them.
#define PS_OVERLAY_MAX_EDGE 1024
#define PS_OVERLAY_QUALITY  0.8f

// Render src with its saliency map made visible: the subject keeps its colour,
// everything else drops to dark grey. Reading a mask as numbers is hopeless,
// and every subject-versus-background measurement downstream depends on it
// being right, so it has to be inspectable by eye.
//
// out is freshly allocated and already downscaled; free with ps_image_free.
ps_status_e ps_overlay_compose(const ps_image *src, const ps_mask *mask, ps_image *out);

// Create/check an output directory outside the input tree, resolving symlinks.
ps_status_e ps_overlay_prepare_directory(const char *dir, const char *root);

// Flatten the relative path with escaped separators. Fails instead of
// truncating a name. Existing files must still be protected by the writer.
ps_status_e ps_overlay_filename(const char *dir, const char *root, const char *path,
                                char *out, size_t cap);

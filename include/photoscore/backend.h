#pragma once

#include "photoscore/aesthetic.h"
#include "photoscore/embedding.h"
#include "photoscore/exif.h"
#include "photoscore/image.h"
#include "photoscore/mask.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

// The seam between photoscore and the platform. Anything that needs an OS
// framework or a trained model lives behind this vtable; anything computable
// from pixels alone stays outside it and is shared by every backend.
//
// Slots arrive with the chunk that needs them. A NULL slot means the backend
// does not provide that capability, and the dispatch helper below reports
// PS_ERR_UNSUPPORTED rather than calling through a null pointer.
typedef struct {
    const char *name;
    void       *ctx;    // backend-owned state, opaque to callers

    ps_status_e (*decode)(void *ctx, const char *path, ps_image *out, ps_exif *out_exif);
    ps_status_e (*aesthetics)(void *ctx, const ps_image *img, ps_aesthetic *out);
    ps_status_e (*saliency)(void *ctx, const ps_image *img, ps_mask *out);
    ps_status_e (*embed)(void *ctx, const ps_image *img, ps_embedding *out);
    ps_status_e (*write_jpeg)(void *ctx, const char *path, const ps_image *img, f32 quality);

    void (*free)(void *ctx);
} ps_backend;

// Select and initialise the backend for this platform. Caller owns the struct.
ps_status_e ps_backend_init(ps_backend *out);
void        ps_backend_free(ps_backend *b);

// Decode path into an upright RGB8 image. out_exif may be NULL. On success the
// caller frees out with ps_image_free.
ps_status_e ps_backend_decode(const ps_backend *b, const char *path,
                              ps_image *out, ps_exif *out_exif);

// Score img with the platform's aesthetic model.
ps_status_e ps_backend_aesthetics(const ps_backend *b, const ps_image *img,
                                  ps_aesthetic *out);

// Attention-based saliency for img. Caller frees out with ps_mask_free.
ps_status_e ps_backend_saliency(const ps_backend *b, const ps_image *img, ps_mask *out);

// Feature print for near-duplicate matching. Caller frees with ps_embedding_free.
ps_status_e ps_backend_embed(const ps_backend *b, const ps_image *img, ps_embedding *out);

// Write img to a new path as JPEG. Existing files/symlinks are never replaced.
// quality runs 0..1. Returns PS_ERR_EXISTS if the destination already exists.
ps_status_e ps_backend_write_jpeg(const ps_backend *b, const char *path,
                                  const ps_image *img, f32 quality);

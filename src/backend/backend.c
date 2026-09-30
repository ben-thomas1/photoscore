#include "photoscore/backend.h"
#include "photoscore/status.h"

#include "apple/apple.h"

// One backend today. When a second arrives this becomes a selection rather
// than a straight call, and the choice belongs here rather than in callers.
ps_status_e ps_backend_init(ps_backend *out) {
    if (out == NULL) { return PS_ERR_INVALID; }

    *out = (ps_backend){ 0 };

    return ps_backend_apple_init(out);
}

void ps_backend_free(ps_backend *b) {
    if (b == NULL) { return; }

    if (b->free != NULL) { b->free(b->ctx); }

    *b = (ps_backend){ 0 };
}

ps_status_e ps_backend_decode(const ps_backend *b, const char *path,
                              ps_image *out, ps_exif *out_exif) {
    if (b == NULL || path == NULL || out == NULL) { return PS_ERR_INVALID; }
    if (b->decode == NULL) { return PS_ERR_UNSUPPORTED; }

    return b->decode(b->ctx, path, out, out_exif);
}

ps_status_e ps_backend_aesthetics(const ps_backend *b, const ps_image *img,
                                  ps_aesthetic *out) {
    if (b == NULL || img == NULL || out == NULL) { return PS_ERR_INVALID; }
    if (b->aesthetics == NULL) { return PS_ERR_UNSUPPORTED; }

    return b->aesthetics(b->ctx, img, out);
}

ps_status_e ps_backend_saliency(const ps_backend *b, const ps_image *img, ps_mask *out) {
    if (b == NULL || img == NULL || out == NULL) { return PS_ERR_INVALID; }
    if (b->saliency == NULL) { return PS_ERR_UNSUPPORTED; }

    return b->saliency(b->ctx, img, out);
}

ps_status_e ps_backend_embed(const ps_backend *b, const ps_image *img, ps_embedding *out) {
    if (b == NULL || img == NULL || out == NULL) { return PS_ERR_INVALID; }
    if (b->embed == NULL) { return PS_ERR_UNSUPPORTED; }

    return b->embed(b->ctx, img, out);
}

ps_status_e ps_backend_write_jpeg(const ps_backend *b, const char *path,
                                  const ps_image *img, f32 quality) {
    if (b == NULL || path == NULL || img == NULL) { return PS_ERR_INVALID; }
    if (b->write_jpeg == NULL) { return PS_ERR_UNSUPPORTED; }

    return b->write_jpeg(b->ctx, path, img, quality);
}

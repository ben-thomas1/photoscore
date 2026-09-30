#include "vision.h"
#include "cgimage.h"

#include "photoscore/aesthetic.h"
#include "photoscore/embedding.h"
#include "photoscore/image.h"
#include "photoscore/mask.h"
#include "photoscore/status.h"

#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>

// Run one request over img and hand the first observation to the block.
// Centralises the handler setup, CGImage lifetime and error mapping shared by
// every Vision call.
static ps_status_e perform(const ps_image *img, VNImageBasedRequest *req,
                           ps_status_e (^consume)(VNObservation *obs)) {
    CGImageRef cg = ps_apple_cg_from_image(img);
    if (cg == NULL) { return PS_ERR_BACKEND; }

    ps_status_e s = PS_OK;

    @autoreleasepool {
        // The image is already upright, so no orientation is passed.
        VNImageRequestHandler *handler =
            [[VNImageRequestHandler alloc] initWithCGImage:cg options:@{}];

        NSError *err = nil;
        if (![handler performRequests:@[ req ] error:&err]) {
            s = PS_ERR_BACKEND;
        } else {
            VNObservation *obs = req.results.firstObject;
            s = (obs == nil) ? PS_ERR_BACKEND : consume(obs);
        }
    }

    CGImageRelease(cg);

    return s;
}

ps_status_e ps_vision_aesthetics(const ps_image *img, ps_aesthetic *out) {
    if (img == NULL || img->pixels == NULL || out == NULL) { return PS_ERR_INVALID; }

    *out = (ps_aesthetic){ .overall = 0.0f, .is_utility = false };

    if (@available(macOS 15.0, *)) {
        VNCalculateImageAestheticsScoresRequest *req =
            [[VNCalculateImageAestheticsScoresRequest alloc] init];

        return perform(img, req, ^ps_status_e(VNObservation *obs) {
            VNImageAestheticsScoresObservation *a =
                (VNImageAestheticsScoresObservation *)obs;

            out->overall    = a.overallScore;
            out->is_utility = a.isUtility ? true : false;

            return PS_OK;
        });
    }

    return PS_ERR_UNSUPPORTED;
}

// Copy the observation's single-channel float buffer into a ps_mask, clamping
// into [0,1]. Vision documents the range but the clamp keeps every downstream
// weighting well defined regardless.
static ps_status_e mask_from_pixel_buffer(CVPixelBufferRef pb, ps_mask *out) {
    if (pb == NULL) { return PS_ERR_BACKEND; }

    if (CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        return PS_ERR_BACKEND;
    }

    size_t w   = CVPixelBufferGetWidth(pb);
    size_t h   = CVPixelBufferGetHeight(pb);
    size_t bpr = CVPixelBufferGetBytesPerRow(pb);
    const uint8_t *base = CVPixelBufferGetBaseAddress(pb);

    ps_status_e s = PS_OK;

    if (base == NULL || w == 0 || h == 0) {
        s = PS_ERR_BACKEND;
    } else {
        s = ps_mask_init(out, w, h);

        if (s == PS_OK) {
            for (size_t y = 0; y < h; y++) {
                const float *row = (const float *)(const void *)(base + (y * bpr));

                for (size_t x = 0; x < w; x++) {
                    float v = row[x];

                    if (v < 0.0f) { v = 0.0f; }
                    if (v > 1.0f) { v = 1.0f; }

                    out->values[(y * w) + x] = v;
                }
            }
        }
    }

    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);

    return s;
}

ps_status_e ps_vision_featureprint(const ps_image *img, ps_embedding *out) {
    if (img == NULL || img->pixels == NULL || out == NULL) { return PS_ERR_INVALID; }

    *out = (ps_embedding){ .values = NULL, .len = 0 };

    VNGenerateImageFeaturePrintRequest *req =
        [[VNGenerateImageFeaturePrintRequest alloc] init];
    // Keep the distance scale stable across OS defaults.
    req.revision = VNGenerateImageFeaturePrintRequestRevision2;

    return perform(img, req, ^ps_status_e(VNObservation *obs) {
        VNFeaturePrintObservation *fp = (VNFeaturePrintObservation *)obs;

        NSData     *data  = fp.data;
        NSUInteger  count = fp.elementCount;

        if (data == nil || count == 0) { return PS_ERR_BACKEND; }

        ps_status_e s = ps_embedding_init(out, (size_t)count);
        if (s != PS_OK) { return s; }

        const void *bytes = data.bytes;

        // Pulled out as a plain float vector so distances are computed in C.
        // A thousand photographs is half a million pairs, and going through
        // computeDistance: for each would mean half a million message sends.
        switch (fp.elementType) {
            case VNElementTypeFloat: {
                if (data.length < count * sizeof(float)) {
                    ps_embedding_free(out);
                    return PS_ERR_BACKEND;
                }

                const float *v = bytes;
                for (size_t i = 0; i < (size_t)count; i++) { out->values[i] = v[i]; }
                break;
            }

            case VNElementTypeDouble: {
                if (data.length < count * sizeof(double)) {
                    ps_embedding_free(out);
                    return PS_ERR_BACKEND;
                }

                const double *v = bytes;
                for (size_t i = 0; i < (size_t)count; i++) { out->values[i] = (f32)v[i]; }
                break;
            }

            default:
                ps_embedding_free(out);
                return PS_ERR_UNSUPPORTED;
        }

        return PS_OK;
    });
}

ps_status_e ps_vision_saliency(const ps_image *img, ps_mask *out) {
    if (img == NULL || img->pixels == NULL || out == NULL) { return PS_ERR_INVALID; }

    *out = (ps_mask){ .values = NULL, .width = 0, .height = 0 };

    // Attention-based rather than objectness-based: we want where a viewer
    // looks, which is what composition and subject-focus are about, not where
    // recognisable objects happen to sit.
    VNGenerateAttentionBasedSaliencyImageRequest *req =
        [[VNGenerateAttentionBasedSaliencyImageRequest alloc] init];

    return perform(img, req, ^ps_status_e(VNObservation *obs) {
        VNSaliencyImageObservation *s = (VNSaliencyImageObservation *)obs;

        return mask_from_pixel_buffer(s.pixelBuffer, out);
    });
}

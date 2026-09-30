#include "cgimage.h"

#include "photoscore/image.h"

#include <CoreGraphics/CoreGraphics.h>
#include <stdint.h>

CGImageRef ps_apple_cg_from_image(const ps_image *img) {
    if (img == NULL || img->pixels == NULL) { return NULL; }

    CGDataProviderRef provider =
        CGDataProviderCreateWithData(NULL, img->pixels, img->stride * img->height, NULL);
    if (provider == NULL) { return NULL; }

    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (cs == NULL) {
        CGDataProviderRelease(provider);
        return NULL;
    }

    // The two constants come from different enums, so combine them as bits.
    CGBitmapInfo bitmap_info =
        (CGBitmapInfo)((uint32_t)kCGBitmapByteOrderDefault | (uint32_t)kCGImageAlphaNone);

    CGImageRef cg = CGImageCreate(
        img->width, img->height,
        8,                      // bits per component
        24,                     // bits per pixel
        img->stride,
        cs,
        bitmap_info,
        provider,
        NULL,                   // no decode array
        false,                  // no interpolation needed
        kCGRenderingIntentDefault);

    CGColorSpaceRelease(cs);
    CGDataProviderRelease(provider);

    return cg;
}

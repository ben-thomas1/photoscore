#include "apple.h"
#include "cgimage.h"
#include "vision.h"

#include "photoscore/alloc.h"
#include "photoscore/backend.h"
#include "photoscore/exif.h"
#include "photoscore/image.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void cf_string_copy(CFStringRef s, char *out, size_t cap) {
    out[0] = '\0';
    if (s == NULL || CFGetTypeID(s) != CFStringGetTypeID()) { return; }

    CFStringGetCString(s, out, (CFIndex)cap, kCFStringEncodingUTF8);
}

static bool cf_number_get(CFTypeRef v, f64 *out) {
    if (v == NULL || CFGetTypeID(v) != CFNumberGetTypeID()) { return false; }

    return CFNumberGetValue(v, kCFNumberDoubleType, out);
}

static bool dict_get_f64(CFDictionaryRef d, CFStringRef key, f64 *out) {
    if (d == NULL) { return false; }

    return cf_number_get(CFDictionaryGetValue(d, key), out);
}

static void dict_get_str(CFDictionaryRef d, CFStringRef key, char *out, size_t cap) {
    out[0] = '\0';
    if (d == NULL) { return; }

    cf_string_copy(CFDictionaryGetValue(d, key), out, cap);
}

// ISO is stored as an array of ratings; the first is the one in effect.
static bool dict_get_iso(CFDictionaryRef exif, f64 *out) {
    if (exif == NULL) { return false; }

    CFTypeRef v = CFDictionaryGetValue(exif, kCGImagePropertyExifISOSpeedRatings);
    if (v == NULL || CFGetTypeID(v) != CFArrayGetTypeID()) { return false; }

    CFArrayRef arr = v;
    if (CFArrayGetCount(arr) < 1) { return false; }

    return cf_number_get(CFArrayGetValueAtIndex(arr, 0), out);
}

static void mark(ps_exif *e, ps_exif_field_e field, bool found) {
    if (found) { e->present |= (u32)field; }
}

static void read_properties(CGImageSourceRef src, ps_exif *out) {
    *out = (ps_exif){ 0 };
    out->orientation = 1;

    CFStringRef uti = CGImageSourceGetType(src);
    cf_string_copy(uti, out->format, sizeof(out->format));

    CFDictionaryRef props = CGImageSourceCopyPropertiesAtIndex(src, 0, NULL);
    if (props == NULL) { return; }

    f64 n = 0.0;
    if (dict_get_f64(props, kCGImagePropertyPixelWidth, &n))  { out->width  = (size_t)n; }
    if (dict_get_f64(props, kCGImagePropertyPixelHeight, &n)) { out->height = (size_t)n; }
    if (dict_get_f64(props, kCGImagePropertyOrientation, &n)) {
        u32 o = (u32)n;
        if (o >= 1 && o <= 8) { out->orientation = o; }
    }

    CFDictionaryRef tiff = CFDictionaryGetValue(props, kCGImagePropertyTIFFDictionary);
    if (tiff != NULL) {
        char make[PS_EXIF_STR_MAX];
        char model[PS_EXIF_STR_MAX];

        dict_get_str(tiff, kCGImagePropertyTIFFMake, make, sizeof(make));
        dict_get_str(tiff, kCGImagePropertyTIFFModel, model, sizeof(model));

        if (make[0] != '\0' && model[0] != '\0') {
            snprintf(out->camera, sizeof(out->camera), "%s %s", make, model);
        } else if (model[0] != '\0') {
            snprintf(out->camera, sizeof(out->camera), "%s", model);
        } else {
            snprintf(out->camera, sizeof(out->camera), "%s", make);
        }
    }

    CFDictionaryRef exif = CFDictionaryGetValue(props, kCGImagePropertyExifDictionary);
    if (exif != NULL) {
        mark(out, PS_EXIF_ISO,           dict_get_iso(exif, &out->iso));
        mark(out, PS_EXIF_EXPOSURE_TIME, dict_get_f64(exif, kCGImagePropertyExifExposureTime, &out->exposure_time));
        mark(out, PS_EXIF_F_NUMBER,      dict_get_f64(exif, kCGImagePropertyExifFNumber, &out->f_number));
        mark(out, PS_EXIF_FOCAL_LENGTH,  dict_get_f64(exif, kCGImagePropertyExifFocalLength, &out->focal_length));
        mark(out, PS_EXIF_FOCAL_35MM,    dict_get_f64(exif, kCGImagePropertyExifFocalLenIn35mmFilm, &out->focal_35mm));
        mark(out, PS_EXIF_EXPOSURE_BIAS, dict_get_f64(exif, kCGImagePropertyExifExposureBiasValue, &out->exposure_bias));

        f64 flash = 0.0;
        if (dict_get_f64(exif, kCGImagePropertyExifFlash, &flash)) {
            // Bit 0 of the flash tag records whether it actually fired.
            out->flash = ((u32)flash & 1u) != 0;
            mark(out, PS_EXIF_FLASH, true);
        }

        dict_get_str(exif, kCGImagePropertyExifLensModel, out->lens, sizeof(out->lens));
        dict_get_str(exif, kCGImagePropertyExifDateTimeOriginal, out->datetime, sizeof(out->datetime));
    }

    CFRelease(props);
}

// CGBitmapContext has no 24-bit RGB format, so draw into RGBA and pack down.
static ps_status_e draw_to_rgb8(CGImageRef cg, ps_image *out) {
    size_t w = CGImageGetWidth(cg);
    size_t h = CGImageGetHeight(cg);

    if (w == 0 || h == 0) { return PS_ERR_DECODE; }
    if (w > SIZE_MAX / 4) { return PS_ERR_NOMEM; }

    size_t rgba_stride = w * 4;
    if (h > SIZE_MAX / rgba_stride) { return PS_ERR_NOMEM; }

    u8 *rgba = PS_MALLOC(rgba_stride * h);
    if (rgba == NULL) { return PS_ERR_NOMEM; }

    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (cs == NULL) {
        PS_FREE(rgba);
        return PS_ERR_BACKEND;
    }

    CGContextRef ctx = CGBitmapContextCreate(
        rgba, w, h, 8, rgba_stride, cs,
        kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);

    if (ctx == NULL) {
        PS_FREE(rgba);
        return PS_ERR_BACKEND;
    }

    // Lay down an opaque matte first, then compose over it. The buffer is
    // uninitialised malloc, and images with an alpha channel would otherwise
    // be measured against whatever happened to be in those bytes.
    //
    // Mid-grey rather than white: white reads as saturated, and a transparent
    // PNG was reporting 29% blown highlights that were entirely the matte.
    // A neutral value is clipped at neither end and sits mid-range, so it
    // distorts the tone statistics as little as a synthetic backdrop can.
    CGContextSetRGBFillColor(ctx, 0.5, 0.5, 0.5, 1.0);
    CGContextFillRect(ctx, CGRectMake(0.0, 0.0, (CGFloat)w, (CGFloat)h));

    CGContextDrawImage(ctx, CGRectMake(0.0, 0.0, (CGFloat)w, (CGFloat)h), cg);
    CGContextRelease(ctx);

    ps_status_e s = ps_image_init(out, w, h);
    if (s != PS_OK) {
        PS_FREE(rgba);
        return s;
    }

    for (size_t y = 0; y < h; y++) {
        const u8 *srow = rgba + (y * rgba_stride);
        u8       *drow = out->pixels + (y * out->stride);

        for (size_t x = 0; x < w; x++) {
            drow[(x * 3) + 0] = srow[(x * 4) + 0];
            drow[(x * 3) + 1] = srow[(x * 4) + 1];
            drow[(x * 3) + 2] = srow[(x * 4) + 2];
        }
    }

    PS_FREE(rgba);

    return PS_OK;
}

static ps_status_e apple_decode(void *ctx, const char *path,
                                ps_image *out, ps_exif *out_exif) {
    (void)ctx;   // ImageIO decoding is stateless; Vision will need one

    if (path == NULL || out == NULL) { return PS_ERR_INVALID; }

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
    if (url == NULL) { return PS_ERR_INVALID; }

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, NULL);
    CFRelease(url);
    if (src == NULL) { return PS_ERR_DECODE; }

    ps_exif exif;
    read_properties(src, &exif);

    CGImageRef cg = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    CFRelease(src);
    if (cg == NULL) { return PS_ERR_DECODE; }

    ps_status_e s = draw_to_rgb8(cg, out);
    CGImageRelease(cg);
    if (s != PS_OK) { return s; }

    s = ps_image_apply_orientation(out, exif.orientation);
    if (s != PS_OK) {
        ps_image_free(out);
        return s;
    }

    if (out_exif != NULL) { *out_exif = exif; }

    return PS_OK;
}

static ps_status_e apple_aesthetics(void *ctx, const ps_image *img, ps_aesthetic *out) {
    (void)ctx;

    return ps_vision_aesthetics(img, out);
}

static ps_status_e apple_saliency(void *ctx, const ps_image *img, ps_mask *out) {
    (void)ctx;

    return ps_vision_saliency(img, out);
}

static ps_status_e apple_embed(void *ctx, const ps_image *img, ps_embedding *out) {
    (void)ctx;

    return ps_vision_featureprint(img, out);
}

// Publish a complete file without ever replacing an existing destination.
// A sibling temporary file makes link() atomic and keeps partial writes hidden.
static ps_status_e write_new_file(const char *path, CFDataRef data) {
    size_t len = strlen(path);
    if (len > SIZE_MAX - sizeof(".XXXXXX")) { return PS_ERR_RANGE; }
    char *temporary = PS_MALLOC(len + sizeof(".XXXXXX"));
    if (temporary == NULL) { return PS_ERR_NOMEM; }
    memcpy(temporary, path, len);
    memcpy(temporary + len, ".XXXXXX", sizeof(".XXXXXX"));

    int fd = mkstemp(temporary);
    if (fd < 0) { PS_FREE(temporary); return PS_ERR_IO; }
    ps_status_e s = PS_OK;
    const UInt8 *bytes = CFDataGetBytePtr(data);
    size_t remaining = (size_t)CFDataGetLength(data);
    while (remaining > 0) {
        ssize_t written = write(fd, bytes, remaining);
        if (written < 0 && errno == EINTR) { continue; }
        if (written <= 0) { s = PS_ERR_IO; break; }
        bytes += (size_t)written;
        remaining -= (size_t)written;
    }
    if (close(fd) != 0) { s = PS_ERR_IO; }
    if (s == PS_OK && link(temporary, path) != 0) {
        s = (errno == EEXIST) ? PS_ERR_EXISTS : PS_ERR_IO;
    }
    unlink(temporary);
    PS_FREE(temporary);
    return s;
}

static ps_status_e apple_write_jpeg(void *ctx, const char *path,
                                    const ps_image *img, f32 quality) {
    (void)ctx;

    if (path == NULL || img == NULL || img->pixels == NULL) { return PS_ERR_INVALID; }

    CGImageRef cg = ps_apple_cg_from_image(img);
    if (cg == NULL) { return PS_ERR_BACKEND; }

    CFMutableDataRef data = CFDataCreateMutable(NULL, 0);
    if (data == NULL) {
        CGImageRelease(cg);
        return PS_ERR_NOMEM;
    }

    CGImageDestinationRef dst =
        CGImageDestinationCreateWithData(data, CFSTR("public.jpeg"), 1, NULL);

    if (dst == NULL) {
        CFRelease(data);
        CGImageRelease(cg);
        return PS_ERR_IO;
    }

    CFNumberRef q = CFNumberCreate(NULL, kCFNumberFloatType, &quality);
    if (q == NULL) {
        CFRelease(dst);
        CFRelease(data);
        CGImageRelease(cg);
        return PS_ERR_NOMEM;
    }
    CFTypeRef keys[]   = { kCGImageDestinationLossyCompressionQuality };
    CFTypeRef values[] = { q };

    CFDictionaryRef opts = CFDictionaryCreate(
        NULL, keys, values, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    ps_status_e s = PS_ERR_NOMEM;
    if (opts != NULL) {
        CGImageDestinationAddImage(dst, cg, opts);
        s = CGImageDestinationFinalize(dst) ? write_new_file(path, data) : PS_ERR_IO;
    }

    if (opts != NULL) { CFRelease(opts); }
    if (q != NULL)    { CFRelease(q); }
    CFRelease(dst);
    CFRelease(data);
    CGImageRelease(cg);

    return s;
}

ps_status_e ps_backend_apple_init(ps_backend *out) {
    if (out == NULL) { return PS_ERR_INVALID; }

    *out = (ps_backend){
        .name       = "apple",
        .ctx        = NULL,
        .decode     = apple_decode,
        .aesthetics = apple_aesthetics,
        .saliency   = apple_saliency,
        .embed      = apple_embed,
        .write_jpeg = apple_write_jpeg,
        .free       = NULL,
    };

    return PS_OK;
}

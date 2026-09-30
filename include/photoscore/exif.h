#pragma once

#include "photoscore/types.h"

#include <stdbool.h>
#include <stddef.h>

#define PS_EXIF_STR_MAX 128

// Which numeric fields the file actually carried. Needed because 0 is a
// legitimate value for exposure bias, and NaN sentinels are unusable under the
// release build's -ffast-math.
typedef enum {
    PS_EXIF_ISO           = 1u << 0,
    PS_EXIF_EXPOSURE_TIME = 1u << 1,
    PS_EXIF_F_NUMBER      = 1u << 2,
    PS_EXIF_FOCAL_LENGTH  = 1u << 3,
    PS_EXIF_FOCAL_35MM    = 1u << 4,
    PS_EXIF_EXPOSURE_BIAS = 1u << 5,
    PS_EXIF_FLASH         = 1u << 6,
} ps_exif_field_e;

typedef struct {
    size_t width;        // as stored, before orientation is applied
    size_t height;
    u32    orientation;  // EXIF tag 1-8 as found in the file

    char format[PS_EXIF_STR_MAX];    // UTI, e.g. "public.heic"
    char camera[PS_EXIF_STR_MAX];
    char lens[PS_EXIF_STR_MAX];
    char datetime[PS_EXIF_STR_MAX];

    u32 present;         // bitmask of ps_exif_field_e

    f64  iso;
    f64  exposure_time;  // seconds
    f64  f_number;
    f64  focal_length;   // mm
    f64  focal_35mm;     // mm, 35mm equivalent
    f64  exposure_bias;  // EV
    bool flash;
} ps_exif;

static inline bool ps_exif_has(const ps_exif *e, ps_exif_field_e field) {
    return (e->present & (u32)field) != 0;
}

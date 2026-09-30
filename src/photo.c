#include "photoscore/photo.h"

// No default case: -Wswitch then flags any flag added without a name here.
const char *ps_flag_name(ps_flag_e flag) {
    switch (flag) {
        case PS_FLAG_DUPLICATE:    return "duplicate";
        case PS_FLAG_UTILITY:      return "utility";
        case PS_FLAG_BLURRED:      return "blurred";
        case PS_FLAG_UNDEREXPOSED: return "underexposed";
        case PS_FLAG_OVEREXPOSED:  return "overexposed";
    }

    return "unknown";
}

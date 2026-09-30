#include "photoscore/status.h"

// No default case: -Wswitch then flags any status added without a string here.
const char *ps_status_str(ps_status_e s) {
    switch (s) {
        case PS_OK:              return "ok";
        case PS_ERR_NOMEM:       return "out of memory";
        case PS_ERR_INVALID:     return "invalid argument";
        case PS_ERR_RANGE:       return "out of range";
        case PS_ERR_NOTFOUND:    return "not found";
        case PS_ERR_EXISTS:      return "already exists";
        case PS_ERR_FULL:        return "full";
        case PS_ERR_EMPTY:       return "empty";
        case PS_ERR_IO:          return "i/o error";
        case PS_ERR_DECODE:      return "decode failed";
        case PS_ERR_BACKEND:     return "backend error";
        case PS_ERR_UNSUPPORTED: return "unsupported";
    }

    return "unknown status";
}

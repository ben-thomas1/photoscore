#pragma once

// Library-wide status. Shared failure categories, not per-function codes.
// PS_OK is 0 so callers can write `if (s != PS_OK)`.
typedef enum {
    PS_OK = 0,

    PS_ERR_NOMEM,
    PS_ERR_INVALID,
    PS_ERR_RANGE,
    PS_ERR_NOTFOUND,
    PS_ERR_EXISTS,
    PS_ERR_FULL,
    PS_ERR_EMPTY,
    PS_ERR_IO,
    PS_ERR_DECODE,
    PS_ERR_BACKEND,
    PS_ERR_UNSUPPORTED,
} ps_status_e;

// Short human-readable description. Never returns NULL.
const char *ps_status_str(ps_status_e s);

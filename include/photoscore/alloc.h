#pragma once

#include <stdlib.h>

// All heap allocation routes through these so an embedder can reroute it.
// Override by defining them before including any photoscore header.

#ifndef PS_MALLOC
#define PS_MALLOC(sz)      malloc(sz)
#endif

#ifndef PS_REALLOC
#define PS_REALLOC(p, sz)  realloc(p, sz)
#endif

#ifndef PS_FREE
#define PS_FREE(p)         free(p)
#endif

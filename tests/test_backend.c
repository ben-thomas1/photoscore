#include "photoscore/backend.h"
#include "photoscore/image.h"
#include "photoscore/status.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(bool ok, const char *what) {
    if (!ok) {
        printf("  %s\n", what);
        failures += 1;
    }
}

static void check_init(void) {
    ps_backend b;

    check(ps_backend_init(NULL) == PS_ERR_INVALID, "NULL out not rejected");

    check(ps_backend_init(&b) == PS_OK, "init failed");
    check(b.name != NULL, "backend has no name");
    check(b.decode != NULL, "backend provides no decode");

    ps_backend_free(&b);
    check(b.name == NULL && b.decode == NULL, "free did not zero the struct");

    ps_backend_free(NULL);   // must not crash
}

// A zeroed backend stands in for one that lacks a capability. Dispatch has to
// report that rather than calling through a null pointer.
static void check_unsupported(void) {
    ps_backend empty = { 0 };
    ps_image   img;

    check(ps_backend_decode(&empty, "/tmp/x.jpg", &img, NULL) == PS_ERR_UNSUPPORTED,
          "missing decode slot did not report PS_ERR_UNSUPPORTED");
}

static void check_dispatch_guards(void) {
    ps_backend b;
    ps_image   img;

    if (ps_backend_init(&b) != PS_OK) {
        printf("  init failed, skipping guard checks\n");
        failures += 1;
        return;
    }

    check(ps_backend_decode(NULL, "/tmp/x.jpg", &img, NULL) == PS_ERR_INVALID,
          "NULL backend not rejected");
    check(ps_backend_decode(&b, NULL, &img, NULL) == PS_ERR_INVALID,
          "NULL path not rejected");
    check(ps_backend_decode(&b, "/tmp/x.jpg", NULL, NULL) == PS_ERR_INVALID,
          "NULL out image not rejected");
    check(ps_backend_decode(&b, "/nonexistent/nope.jpg", &img, NULL) == PS_ERR_DECODE,
          "missing file did not report PS_ERR_DECODE");

    ps_backend_free(&b);
}

int main(void) {
    printf("backend lifecycle:\n");
    check_init();

    printf("unsupported capability:\n");
    check_unsupported();

    printf("dispatch guards:\n");
    check_dispatch_guards();

    if (failures == 0) {
        printf("ok: all backend cases pass\n");
        return 0;
    }

    printf("FAILED: %d check(s)\n", failures);
    return 1;
}

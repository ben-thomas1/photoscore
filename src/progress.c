#include "photoscore/progress.h"
#include "photoscore/types.h"

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define REDRAW_INTERVAL 0.05   // seconds; ~20 Hz is smooth without flicker
#define FALLBACK_WIDTH  80
#define MIN_WIDTH       24
#define BAR_WIDTH       24

static f64 now_seconds(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (f64)ts.tv_sec + ((f64)ts.tv_nsec / 1e9);
}

static int term_width(void) {
    struct winsize ws;

    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return (int)ws.ws_col;
    }

    return FALLBACK_WIDTH;
}

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');

    return (slash != NULL) ? slash + 1 : path;
}

static void format_duration(f64 seconds, char *out, size_t cap) {
    if (seconds < 0.0)   { seconds = 0.0; }
    if (seconds > 359999.0) { seconds = 359999.0; }

    if (seconds < 60.0) {
        snprintf(out, cap, "%.0fs", seconds);
    } else {
        int total = (int)seconds;
        snprintf(out, cap, "%dm%02ds", total / 60, total % 60);
    }
}

void ps_progress_begin(ps_progress *out, const char *label, size_t total) {
    if (out == NULL) { return; }

    *out = (ps_progress){
        .label     = (label != NULL) ? label : "working",
        .total     = total,
        .done      = 0,
        .started   = now_seconds(),
        .last_draw = 0.0,
        .enabled   = (total > 0) && (isatty(STDERR_FILENO) == 1),
    };
}

void ps_progress_clear(const ps_progress *p) {
    if (p == NULL || !p->enabled) { return; }

    // \r returns to column 0, \033[K erases to end of line.
    fputs("\r\033[K", stderr);
    fflush(stderr);
}

void ps_progress_tick(ps_progress *p, const char *item) {
    if (p == NULL) { return; }

    p->done += 1;

    if (!p->enabled) { return; }

    f64 now  = now_seconds();
    bool last = (p->done >= p->total);

    if (!last && (now - p->last_draw) < REDRAW_INTERVAL) { return; }
    p->last_draw = now;

    f64 elapsed = now - p->started;
    f64 rate    = (elapsed > 0.0) ? (f64)p->done / elapsed : 0.0;
    f64 frac    = (f64)p->done / (f64)p->total;

    char eta[16];
    format_duration((rate > 0.0) ? ((f64)(p->total - p->done) / rate) : 0.0, eta, sizeof(eta));

    char bar[BAR_WIDTH + 1];
    size_t filled = (size_t)(frac * (f64)BAR_WIDTH);
    for (size_t i = 0; i < BAR_WIDTH; i++) {
        bar[i] = (i < filled) ? '=' : ' ';
    }
    bar[BAR_WIDTH] = '\0';

    char line[512];
    int n = snprintf(line, sizeof(line), "%s [%s] %zu/%zu %3.0f%% %.1f/s eta %s",
                     p->label, bar, p->done, p->total, frac * 100.0, rate, eta);

    // Append the current file only if it fits, so the line never wraps and
    // breaks the in-place redraw.
    int width = term_width();
    if (item != NULL && n > 0 && n < (int)sizeof(line)) {
        const char *name = basename_of(item);
        int room = width - n - 2;

        if (room > 8) {
            snprintf(line + n, sizeof(line) - (size_t)n, "  %.*s", room, name);
        }
    }

    if (width > MIN_WIDTH && (int)strlen(line) > width - 1) {
        line[width - 1] = '\0';
    }

    fprintf(stderr, "\r\033[K%s", line);
    fflush(stderr);
}

void ps_progress_end(ps_progress *p) {
    if (p == NULL) { return; }

    ps_progress_clear(p);
    p->enabled = false;
}

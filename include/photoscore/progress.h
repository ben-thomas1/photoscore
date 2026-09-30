#pragma once

#include "photoscore/types.h"

#include <stdbool.h>
#include <stddef.h>

// In-place progress meter on stderr, so stdout stays clean for redirection.
// Every call is a no-op unless stderr is a terminal, which keeps redirected
// logs free of carriage returns and escape sequences.
typedef struct {
    const char *label;
    size_t      total;
    size_t      done;

    f64 started;
    f64 last_draw;

    bool enabled;
} ps_progress;

void ps_progress_begin(ps_progress *out, const char *label, size_t total);

// Advance by one. item is an optional path shown alongside the bar.
void ps_progress_tick(ps_progress *p, const char *item);

// Erase the meter so other output can be written without colliding with it.
// The next tick redraws.
void ps_progress_clear(const ps_progress *p);

void ps_progress_end(ps_progress *p);

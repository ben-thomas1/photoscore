#pragma once

#include "photoscore/status.h"

#include <stdbool.h>
#include <stdio.h>

#define PS_EXIT_OK    0
#define PS_EXIT_FAIL  1
#define PS_EXIT_USAGE 2

// What the parsed command line asks the program to do.
typedef enum {
    PS_ACTION_SCORE = 0,
    PS_ACTION_HELP,
    PS_ACTION_VERSION,
} ps_cli_action_e;

typedef struct {
    ps_cli_action_e  action;
    const char      *root;         // borrowed from argv; set only when action is PS_ACTION_SCORE
    const char      *overlay_dir;  // borrowed from argv; NULL unless --overlay was given
    bool             verbose;      // add camera and exposure columns
    size_t           top;          // keep only the best N, 0 for all
    bool             flagged;      // list deletion candidates instead of a ranking
    bool             print0;       // selected paths only, separated by NUL bytes
} ps_cli_opts;

// Parse argv into out. Diagnostics for malformed input are written to stderr
// here, so the caller only has to map the status onto an exit code.
ps_status_e ps_cli_parse(int argc, char **argv, ps_cli_opts *out);

void ps_cli_usage(FILE *out);

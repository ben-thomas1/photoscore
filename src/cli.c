#include "photoscore/cli.h"
#include "photoscore/status.h"
#include "photoscore/types.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ps_cli_usage(FILE *out) {
    fprintf(out,
        "usage: photoscore <dir>\n"
        "\n"
        "Score every image under <dir> recursively and write a ranking to\n"
        "stdout, best first. Progress and diagnostics go to stderr, so stdout\n"
        "stays clean for redirection.\n"
        "\n"
        "  -h, --help          show this message\n"
        "  -v, --verbose       add camera and exposure columns\n"
        "      --top N         keep only the best N, one per group of near-duplicates\n"
        "      --flagged       list deletion candidates instead of a ranking\n"
        "  -0, --print0        print selected paths separated by NUL bytes\n"
        "      --version       print version and exit\n"
        "      --overlay DIR   write saliency overlay images to DIR for review\n"
        "\n"
        "Nothing is ever moved or deleted; --flagged only prints a list.\n");
}

ps_status_e ps_cli_parse(int argc, char **argv, ps_cli_opts *out) {
    if (out == NULL || argv == NULL) { return PS_ERR_INVALID; }

    *out = (ps_cli_opts){
        .action      = PS_ACTION_SCORE,
        .root        = NULL,
        .overlay_dir = NULL,
        .verbose     = false,
        .top         = 0,
        .flagged     = false,
    };

    for (i32 i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            out->action = PS_ACTION_HELP;
            return PS_OK;
        }
        if (strcmp(arg, "--version") == 0) {
            out->action = PS_ACTION_VERSION;
            return PS_OK;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            out->verbose = true;
            continue;
        }
        if (strcmp(arg, "--flagged") == 0) {
            out->flagged = true;
            continue;
        }
        if (strcmp(arg, "-0") == 0 || strcmp(arg, "--print0") == 0) {
            out->print0 = true;
            continue;
        }
        if (strcmp(arg, "--top") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "photoscore: --top needs a count\n");
                ps_cli_usage(stderr);
                return PS_ERR_INVALID;
            }

            i += 1;

            char          *end = NULL;
            errno = 0;
            unsigned long  v   = strtoul(argv[i], &end, 10);

            if (argv[i][0] < '0' || argv[i][0] > '9' ||
                end == argv[i] || *end != '\0' || errno == ERANGE ||
                v == 0 || v > SIZE_MAX) {
                fprintf(stderr, "photoscore: --top needs a positive count, got `%s`\n",
                        argv[i]);
                return PS_ERR_INVALID;
            }

            out->top = (size_t)v;
            continue;
        }
        if (strcmp(arg, "--overlay") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "photoscore: --overlay needs a directory\n");
                ps_cli_usage(stderr);
                return PS_ERR_INVALID;
            }

            i += 1;
            out->overlay_dir = argv[i];
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "photoscore: unknown option `%s`\n", arg);
            ps_cli_usage(stderr);
            return PS_ERR_INVALID;
        }
        if (out->root != NULL) {
            fprintf(stderr, "photoscore: unexpected argument `%s`\n", arg);
            ps_cli_usage(stderr);
            return PS_ERR_INVALID;
        }
        out->root = arg;
    }

    if (out->root == NULL) {
        fprintf(stderr, "photoscore: missing <dir>\n");
        ps_cli_usage(stderr);
        return PS_ERR_INVALID;
    }

    return PS_OK;
}

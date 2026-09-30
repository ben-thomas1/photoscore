// Exercise the actual CLI control flow with deterministic backend/stage failures.
// No model failure or exhausted machine memory is required to test these paths.
#define ps_backend_init test_backend_init
#define ps_technical_measure test_technical_measure
#define ps_score_corpus test_score_corpus
#define ps_dedup_cluster test_dedup_cluster
#define main photoscore_entry
int photoscore_entry(int argc, char **argv);
#include "../src/main.c"
#undef ps_backend_init
#undef ps_technical_measure
#undef ps_score_corpus
#undef ps_dedup_cluster
#undef main

#include <unistd.h>

ps_status_e ps_score_corpus(ps_da *, const ps_score_params *);
ps_status_e ps_dedup_cluster(ps_da *, f32, ps_dedup_stats *);

typedef enum { HEALTHY, DECODE_FAIL, AESTHETICS_FAIL, SALIENCY_FAIL,
               TECHNICAL_FAIL, EMBED_FAIL, SCORE_FAIL, DEDUP_FAIL, PARTIAL } fault_e;
static fault_e fault;
static int failures;

static void check(bool ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); failures++; }
}

static ps_status_e fake_decode(void *ctx, const char *path, ps_image *img, ps_exif *exif) {
    (void)ctx;
    if (fault == DECODE_FAIL || (fault == PARTIAL && strstr(path, "bad.jpg") != NULL)) {
        return PS_ERR_DECODE;
    }
    ps_status_e s = ps_image_init(img, 8, 8);
    if (s == PS_OK) { memset(img->pixels, 128, img->height * img->stride); }
    if (exif != NULL) { *exif = (ps_exif){0}; }
    return s;
}

static ps_status_e fake_aesthetics(void *ctx, const ps_image *img, ps_aesthetic *out) {
    (void)ctx; (void)img;
    *out = (ps_aesthetic){.overall=0.8f};
    return fault == AESTHETICS_FAIL ? PS_ERR_BACKEND : PS_OK;
}

static ps_status_e fake_saliency(void *ctx, const ps_image *img, ps_mask *out) {
    (void)ctx; (void)img;
    if (fault == SALIENCY_FAIL) { return PS_ERR_BACKEND; }
    ps_status_e s = ps_mask_init(out, 1, 1);
    if (s == PS_OK) { out->values[0] = 1; }
    return s;
}

static ps_status_e fake_embed(void *ctx, const ps_image *img, ps_embedding *out) {
    (void)ctx; (void)img;
    if (fault == EMBED_FAIL) { return PS_ERR_NOMEM; }
    ps_status_e s = ps_embedding_init(out, 1);
    if (s == PS_OK) { out->values[0] = 0.5f; }
    return s;
}

ps_status_e test_backend_init(ps_backend *out) {
    *out = (ps_backend){.name="test", .decode=fake_decode,
                       .aesthetics=fake_aesthetics, .saliency=fake_saliency, .embed=fake_embed};
    return PS_OK;
}

ps_status_e test_technical_measure(const ps_image *img, const ps_mask *mask, ps_technical *out) {
    (void)img; (void)mask;
    if (fault == TECHNICAL_FAIL) { return PS_ERR_NOMEM; }
    *out = (ps_technical){.sharp_subject=55, .sharp_background=20,
                          .sharp_ratio=2.75f, .contrast_rms=0.2f};
    return PS_OK;
}

ps_status_e test_score_corpus(ps_da *photos, const ps_score_params *params) {
    return fault == SCORE_FAIL ? PS_ERR_NOMEM : ps_score_corpus(photos, params);
}

ps_status_e test_dedup_cluster(ps_da *photos, f32 threshold, ps_dedup_stats *stats) {
    return fault == DEDUP_FAIL ? PS_ERR_NOMEM : ps_dedup_cluster(photos, threshold, stats);
}

static size_t capture(char *dir, bool flagged, int *status, char *out, size_t cap) {
    FILE *file = tmpfile();
    if (file == NULL) { exit(1); }
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    if (saved < 0 || dup2(fileno(file), STDOUT_FILENO) < 0) { exit(1); }
    char *args[] = {(char *)"photoscore", dir, (char *)"--print0",
                   (char *)(flagged ? "--flagged" : "--top"), (char *)"1"};
    *status = photoscore_entry(flagged ? 4 : 5, args);
    fflush(stdout);
    if (dup2(saved, STDOUT_FILENO) < 0) { exit(1); }
    close(saved);
    rewind(file);
    size_t n = fread(out, 1, cap - 1, file);
    out[n] = '\0';
    fclose(file);
    return n;
}

int main(void) {
    char dir[] = "/tmp/photoscore-main-XXXXXX";
    if (mkdtemp(dir) == NULL) { return 1; }
    char path[512];
    snprintf(path, sizeof(path), "%s/a photo's\nname.jpg", dir);
    FILE *file = fopen(path, "wb");
    if (file == NULL) { return 1; }
    fclose(file);

    char output[2048];
    int status;
    fault = HEALTHY;
    size_t n = capture(dir, false, &status, output, sizeof(output));
    check(status == 0 && n == strlen(path) + 1 && strcmp(output, path) == 0,
          "NUL output preserves complete filenames");
    n = capture(dir, true, &status, output, sizeof(output));
    check(status == 0 && n == 0, "no flagged paths means empty machine output");

    const fault_e errors[] = {DECODE_FAIL, AESTHETICS_FAIL, TECHNICAL_FAIL,
                             EMBED_FAIL, SCORE_FAIL, DEDUP_FAIL};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        fault = errors[i];
        n = capture(dir, false, &status, output, sizeof(output));
        check(status == 1 && n == 0, "required stage failure cannot publish a ranking");
        n = capture(dir, true, &status, output, sizeof(output));
        check(status == 1 && n == 0, "required stage failure cannot recommend deletion");
    }
    fault = SALIENCY_FAIL;
    n = capture(dir, false, &status, output, sizeof(output));
    check(status == 0 && n == strlen(path) + 1, "documented saliency fallback still ranks");

    char bad[512];
    snprintf(bad, sizeof(bad), "%s/bad.jpg", dir);
    file = fopen(bad, "wb");
    if (file == NULL) { return 1; }
    fclose(file);
    fault = PARTIAL;
    n = capture(dir, false, &status, output, sizeof(output));
    check(status == 1 && n == strlen(path) + 1 && strcmp(output, path) == 0,
          "partial results remain available with failure status");

    unlink(bad);
    unlink(path);
    rmdir(dir);
    return failures != 0;
}

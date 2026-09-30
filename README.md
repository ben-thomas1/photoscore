# photoscore

Scores photographs so you can find the good ones and delete the junk.

Point it at a directory. It walks the tree, measures every image, and prints a
ranking. It never moves, renames or deletes anything.

macOS only. Everything comes from Apple's frameworks — ImageIO for decoding and
Vision for the models — so there are no third-party dependencies.

---

## Requirements

- macOS 15 or later (the aesthetics model arrived in 15)
- Xcode command line tools with C23 support (tested with Apple Clang 21)
- meson and ninja

## Build

```sh
make            # debug: -O0, ASan + UBSan, -Werror
make release    # what you want for real runs
make test       # regression tests and Apple framework smoke tests
make test-release
make install    # optional: install the release binary
make clean
```

Both configurations build with `-Werror` and a broad warning set. The debug
build is the correctness reference; the release build validates the shipped
`-ffast-math` behaviour, so it is worth running both after any change.

Without installing, use `./build/release/photoscore` in place of `photoscore`
in the examples below. Tests generate their own images and need no photo library.

---

## The two things it is for

**Finding your keepers.** One photograph per group of near-duplicates, so a
burst cannot fill the list with ten versions of one frame:

```sh
photoscore ~/Photos --top 10
```

**Finding what to delete:**

```sh
photoscore ~/Photos --flagged
```

Both print to stdout. Progress and statistics go to stderr, so redirection
gives you clean data:

```sh
photoscore ~/Photos > ranking.txt
```

### Opening the results

```sh
# look at the top ten
photoscore ~/Photos --top 10 --print0 | xargs -0 open

# review the deletion candidates before touching anything
photoscore ~/Photos --flagged --print0 | xargs -0 open
```

`--print0` emits only selected paths, each followed by a NUL byte. This preserves
spaces, quotes and newlines in filenames; human-readable tables are for display.

---

## Options

```
-h, --help          show usage
-v, --verbose       add every measured column
    --top N         keep only the best N, one per group of near-duplicates
    --flagged       list deletion candidates instead of a ranking
 -0, --print0       selected paths only, separated by NUL bytes
    --overlay DIR   write saliency overlays to DIR for review
    --version
```

Accepted formats: `jpg jpeg png heic heif webp tif tiff jp2 jxl avif gif bmp`.
RAW is deliberately not included.

Exit status is 0 for a complete run, 1 for failed inputs or processing/output
errors, and 2 for invalid arguments. Successfully processed images may still be
printed after individual failures. A failed measurement is never used as evidence
that a photograph should be deleted. If saliency alone fails, measurements use
the whole frame and a diagnostic explains the fallback.

Hidden files, symlinks and package directories (`.photoslibrary`, `.lrdata`,
`.app` and friends) are skipped. Naming a package directly as the root still
descends into it, on the grounds that you asked for it.

---

## Reading the output

```
SCORE   AES    TECH   GATE   UTIL  DUP       SHARP-S  EV      CLIP-H  NOISE  COLOR  PATH
0.957   0.957  0.912  1.000  -     -         81       +0.46   1.43%   0.52   34.1   IMG_1716.HEIC
0.175   0.700  0.584  0.250  -     -         4        +0.80   0.06%   0.00   blurred.jpg
```

| column | meaning |
| --- | --- |
| `SCORE` | the answer: `AES x GATE`, in `(0,1)` |
| `AES` | aesthetic rank **within this corpus** |
| `TECH` | technical quality, absolute and comparable between runs |
| `GATE` | penalty for defects. `1.000` unless something is wrong |
| `UTIL` | the model thinks the content is unmemorable |
| `DUP` | `best/N` or `dup/N` for a group of near-duplicates |
| `SHARP-S` | subject sharpness, Laplacian p95 at a 1536px reference scale |
| `EV` | stops from mid-grey, measured on the subject in linear light |
| `CLIP-H` | fraction of the frame with a blown channel |
| `NOISE` | sensor noise deviation, 0-255 units |
| `COLOR` | colourfulness, roughly 0 for monochrome and past 100 for vivid |

`-v` adds background sharpness, the subject/background ratio, shadow clipping,
RMS contrast, rule-of-thirds and centre distances, dimensions, and the camera
and exposure settings.

### The second row above is the point

That photograph is the **second most aesthetically pleasing** of its set at
`AES 0.700`. It is also completely out of focus, so the gate takes it to
`0.175` and it lands fourth. Technical quality can only ever pull a score
down — a flawless photograph of nothing should not outrank a slightly flawed
photograph of something.

---

## How the score is built

```
SCORE = aesthetic percentile  x  gate
```

**The aesthetic half is relative.** There is no absolute scale for "how good is
this photograph", so the model's raw output is ranked against the rest of the
corpus. A score of 0.9 means better than ninety percent of *these* photographs.
Run it over a different directory and the same photograph gets a different
number. The corpus size and every parameter are printed with each run for
exactly this reason.

**The technical half is absolute.** Measured against fixed heuristic tolerances,
so it means the same thing in every run. If it were a percentile the bottom ten
percent of any corpus would be penalised, however good they were.

**The gate is one-sided and takes the worst defect, not the average.** An
earlier version averaged the technical subscores; a totally out-of-focus frame
lost only the sharpness weight and still passed. The gate now takes the minimum
of the focus, highlight and exposure terms, so one ruinous fault is not offset
by everything else being fine.

Defaults, as printed on every run:

```
weights   sharp 0.35  focus 0.10  exposure 0.20  highlight 0.15
          shadow 0.05  noise 0.10  contrast 0.05
limits    sharp 8-55  focus 0.60  ev 2.5  clip 5%/5%  noise 8.0  contrast 0.20
gate      worst of: sharp 15/4  clip 20%/60%  ev 2.0/4.0  floor 0.25
```

Noise is weighted low on purpose: phone pipelines denoise before the file is
written, and it measured a span of only 0.0 to 4.7 across the test corpus.
Composition is absent from the technical blend because it is an aesthetic
property and the aesthetic model has already judged it.

---

## Deletion candidates

`--flagged` lists by mechanism, not by low score:

These are review suggestions. Check the photograph and the named keeper before
deleting anything; neither an aesthetic model nor a similarity threshold can
decide what matters to you.

```
FLAGS              SCORE   REASON                                               PATH
overexposed        0.060   80.8% of the frame blown, +2.1 EV                    blown.jpg
blurred            0.082   subject soft, Laplacian 6 against 7 on the background  IMG_0042.jpeg
duplicate          0.948   one of 7 near-identical frames, best is IMG_1716.HEIC  export.jpg
```

Note the last line. Those duplicates score **0.83 to 0.95** — they are good
photographs that happen to be redundant, and no score threshold would ever have
found them. Equally, the lowest-ranked photograph in a corpus is often worth
keeping. That is why deletion is driven by five mechanisms instead:

| flag | meaning |
| --- | --- |
| `duplicate` | a better copy of this shot exists, named in the reason |
| `utility` | Apple's own judgement that the content is unmemorable |
| `blurred` | the subject is not in focus |
| `underexposed` | unrecoverably dark |
| `overexposed` | blown highlights, or unrecoverably bright |

The technical flags come off the same terms as the gate, so a photograph can
never be called blown while going unpenalised for it in the score. Reasons are
given in the units the measurement was taken in, because "composition 0.42"
tells you nothing and "Laplacian 6 against 7 on the background" tells you the
lens focused past the subject.

---

## Checking that saliency landed correctly

Most measurements are taken twice, once over the subject and once over the
background, because the same reading means opposite things in each. Blur behind
the subject is bokeh; blur on the subject is a ruined photograph. A single
whole-image number cannot tell them apart.

That split depends entirely on the saliency mask being right, and a mask is 4624
floats — unreadable as numbers. So look at it:

```sh
photoscore ~/Photos --overlay /tmp/ov
open /tmp/ov
```

The overlay directory must be outside the input tree, including through symlinks.
Existing files and symlinks are never replaced. Choose a new directory for a fresh
set of overlays; a collision is reported as an error. Nested input paths are
flattened with escaped separators so names remain distinct.

Subject in colour, everything else dimmed to grey. If the mask is wrong, every
technical number for that photograph is wrong too, and the overlay is the only
cheap way to notice.

One thing to know: Vision never returns an empty mask. Given a frame with
nothing to attend to it falls back to a smooth centre blob, and no statistic
distinguishes that from a real subject. On a landscape the subject/background
split quietly becomes centre-versus-edges.

---

## Tuning

Everything adjustable is a named constant, not a magic number:

| constant | where | default |
| --- | --- | --- |
| weights, tolerances, gate | `ps_score_defaults()` in `src/score.c` | printed each run |
| `PS_DEDUP_THRESHOLD` | `include/photoscore/dedup.h` | `0.40` |
| `PS_SHARP_REFERENCE_EDGE` | `include/photoscore/technical.h` | `1536` |
| `PS_OVERLAY_MAX_EDGE` | `include/photoscore/overlay.h` | `1024` |

The dedup threshold is a starting point from a small local experiment: three
photographs each re-encoded, recompressed, cropped, brightened, downscaled and
shifted. The original experiment fixtures and scripts are not included, so these
historical measurements are preliminary rather than a reproducible benchmark:

```
same photograph   n= 45   mean 0.166   max 0.318
different photos  n=108   mean 0.834   min 0.538
```

The populations did not overlap in that sample; this is not a guarantee for your
library. Vision feature-print revision 2 is selected explicitly to keep the
distance scale stable. Each run reports the farthest matching pair and nearest
nonmatching pair. These are pairwise threshold statistics, not cluster diameters:
single linkage can connect a chain whose endpoints differ by more than the
threshold. Review the resulting groups before discarding a photograph.

---

## Known limits

**The aesthetic model is Apple's taste, not yours.** It has been checked for
discrimination — it spreads a real corpus over 0.19 to 0.82 rather than bunching
— but never for agreement with a human. If its ranking disagrees with yours
consistently, that is the finding that matters most, and pairwise A/B labelling
is what would settle it.

**Sharpness is only comparable above the reference scale.** Photographs are
normalised to a 1536px long edge before the Laplacian, which removes the
resolution dependence for anything larger (correlation with megapixels falls
from −0.49 to +0.12). Smaller files are measured natively, because upscaling
invents no detail. Mixing web-sized images with camera files will skew this.

**Noise does not reliably track ISO on phone photographs.** Computational
photography denoises before the file is written. The estimator measures residual
pixel variation, not the noise that was present at the sensor.

**Percentiles need a corpus.** With 35 photographs the ranks are coarse. The
design assumes hundreds to thousands.

**Transparency is composited over mid-grey.** There is no correct answer for
what a transparent region looks like; mid-grey is clipped at neither end, so it
at least avoids inventing a defect.

---

## Layout

```
include/photoscore/     public headers
src/
  walk.c                directory traversal and filtering
  image.c               RGB8 buffer, orientation, downscaling
  technical.c           sharpness, exposure, noise, contrast, colour
  mask.c                saliency map and sampling
  score.c               calibration, blending, the gate, flags
  dedup.c               near-duplicate clustering
  overlay.c             review artefacts
  backend/
    backend.c           the platform seam
    apple/              ImageIO and Vision behind it
tests/
```

Everything that needs an OS framework or a trained model sits behind the
`ps_backend` vtable. Everything computable from pixels alone sits outside it and
would be shared by any other backend.

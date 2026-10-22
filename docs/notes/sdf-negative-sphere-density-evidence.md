# Negative sphere: more cuts

Date: 2026-10-21

[Count comparison and 256-cut carving video](http://starship:8787/reports/f23de4a4ab24f55ab4403a145e1b4fe87a0548c6abf279fbe51561eda92cc096/).

Following the [72-cut shape comparison](sdf-negative-sphere-evidence.md), the
user requested more edits: a subtractive counterpart to the additive ball of
sticks. This study increases count while preserving seed 42, the exact cutter
sequence, camera, material, hard shadows and atmosphere sky. Every smaller
explicit recipe was checked against the corresponding prefix of the next.

`projects/sdf/presets/negative-sphere-dense.json` differs from the original
sphere preset only by count: 256 rather than 72. The generator, renderer and
original presets are unchanged. This is a candidate for visual review, not a
new default or a claim of visual acceptance by the user.

## Visual result

Native stills cover 0, 4, 16, 72, 128, 256, 512, 1,024 and 2,048 cuts. To the
agent's eye, 128 retains broad recognizable surfaces; 256 opens much more of
the interior while retaining the spherical outline. At 512, scattered walls
and fragments dominate; at 1,024–2,048, little remains. More broad cuts do not
produce a dense negative copy of the original slender additive sticks.

The 256-cut video retains the solid hold, four slow guided cuts, accelerated
carving and final half-orbit. FFprobe confirms 720 H.264 frames, 1280x720,
30 FPS and 24 seconds. Native normal/opposite captures and decoded video
frames at 0, 2.3, 8, 12, 16 and 23.9 seconds were inspected. Geometry updates
remain synchronous; this offline video does not qualify smooth live carving.
Detached remnants stay in place.

## Native evidence

`outputs/sdf/sphere-density-qualified/manifest.json` records source `5dd04623`,
clean at capture start, native binary/shader hashes, applied configurations,
explicit recipes, commands, logs and capture hashes. Device: RTX 5070 Ti,
driver 610.57.04. Vulkan validation was required for every run.

| Cutters | Active bricks | Oracle rays | Sampling ambiguities |
| --- | ---: | ---: | ---: |
| 0 | 185,480 | Capture only in this study | — |
| 4 | 272,117 | Capture only in this study | — |
| 16 | 400,538 | Capture only in this study | — |
| 72 | 532,144 | Capture only in this study | — |
| 128 | 504,638 | 771 | 1 |
| 256 | 365,733 | 1,283 | 7 |
| 512 | 144,034 | 258 | 1 |
| 1,024 | 14,922 | 258 | 1 |
| 2,048 | 313 | 258 | 0 |

All five ray checks report zero mismatches and zero exhausted rays under the
existing finite-sampling oracle tolerance. The 0/4/16/72-cut geometry was
validated in the previous study. Above 257 total edits the oracle uses a
smaller fixed ray set, rather than per-cutter probes. At 2,048 cuts none of
those 258 rays hits the tiny surviving fragments; the image and brick count
show that the field is not entirely empty.

The dense recipe hash is `7d1446bfddcd9864`, with 257 total edits across three
batches. It passes again after loading the explicit recipe and at the final
frame of incremental carving: 1,283 rays, 556 hits, zero mismatches/exhaustion,
seven ambiguities, maximum surface residual 0.000120939. Maximum first-hit
distance difference is 0.17755: a grazing ray can meet a different wall when
a thin first remnant is sensitive to grid sampling. The oracle's existing
inner/outer sampling bracket accepts these as ambiguities; no tolerance was
changed. These results do not establish exact analytic first hits for thin
remnants.

The axis-92 profile is fixed at 778,688 bricks and 1,974,880,928 allocated field
bytes. Active-brick counts are not frame timings. No new rendering-performance
matrix or desktop-interaction qualification was run for this preset.

## Slimmer-cutter audition and capacity limit

A temporary generator kept the same base, cutter centers, rotations and long
half extent, while multiplying both transverse half extents and rounding by
0.25. It consumed the same random draws. At 128 cuts it produces fine bores
and needs 649,919 active bricks. At 256 it fails explicitly:

```text
SDF field capacity exceeded: frontier=971422, references=538462,
capacity=778688, atlas_axis=92, flags=1
```

The temporary generator changes were removed; the final tracked change is
the broad 256-cut preset and documentation. Its audition patch and binary
provenance remain in `outputs/sdf/sphere-density/`, and both explicit fine
recipes are included in the report. Both were replayed with the unchanged
native renderer using `--recipe`; the 256-cut trial was required to exit
nonzero and leave no success image. No cuts were dropped or atlas enlarged.
The report includes the expected-failure log and no fabricated 256-cut image.

This trial shows why finer negative sticks need a separate field-capacity or
resolution decision: they add internal surface while leaving most of the
solid intact. That renderer work is outside this count study.

That conclusion applies to the long, quarter-width bores tried here. The
subsequent [exact additive-size counterpart](sdf-thin-negative-sticks-evidence.md)
uses much shorter, narrower sticks on the original placement shell; both
2,048 and 4,096 cutters fit the unchanged atlas profile. The capacity failure
above is not a general limit on slender subtraction.

## Report delivery

The report preserves the full-fetch Blob setup for seekable video on the
static server without HTTP Range support. Isolated headless Chrome checks
pass over both a local no-Range HTTP server and the published URL: all six
decoded chapter seeks, playback, all nine count selectors and active states,
image decoding, local links, no JavaScript errors and 390px layout. Desktop
and mobile report screenshots were inspected. Receipts:
`outputs/sdf/density-browser-review.json` and `outputs/sdf/density-delivery.json`.
The publisher separately verifies HTTP 200 and entrypoint bytes; the snapshot
contains 70 files / 11,216,081 bytes.

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/negative-sphere-dense.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/negative-sphere-dense.json --count 512
```

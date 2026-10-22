# Dense negative sticks with a through-hole introduction

Date: 2026-10-21

[4k carving video and 2k/4k cutter-mode comparisons](http://starship:8787/reports/3a21ce65daedf473f4596ca205421d3d2119e20f73ecb8d79388442b9baae090/).

The user preferred width 0.024 and 2k/4k counts over the earlier 0.015 / 512
candidate, and proposed longer cuts so the introduction produces through-holes.
Both requested counts fit the existing axis-92 field profile.

## Geometry and presentation

Length alone did not make the first cut readable. Original stick centers sit
at radii 0.29–0.30 on a radius-0.30 sphere, so their axes can graze the rim.
The first extended seed-42 cutter remained hidden from the previous camera;
looking along that original line showed a notch at the silhouette.

The new `negative-stick-ball-dense.json` preset uses 4,096 cutters at width
0.024, with `--count 2048` selecting the smaller comparison. Its
`--cutter-mode intro-bores` treatment changes only the first four cuts:

- Multiply their original centers by 0.25, putting them at radii 0.0725–0.075.
- Extend their long half extents to 0.65; retain rotations, width and rounding.
- Leave every cutter after the fourth exactly equal to the short-stick recipe.

The long endpoints lie beyond the sphere in both directions. CPU samples
check a continuous removed path across the entire sphere chord. The preset
camera is calculated from the first cutter's center and local X axis: eye =
center + axis × 1.1, looking at the sphere center. This makes sky visible
through the first bore. It is seed-42 presentation framing, not automatic
camera alignment after changing seed.

The other `--cutter-mode` values are `original` and `through`. Original retains
all short-stick edits. Through extends every original line to half length
0.65 while retaining the original surface-shell centers. At 4k that variant
leaves sparse remnants; the four-bore introduction retains the dense final
surface. All six mode/count comparisons use the same width, camera, light and
material. The report exposes all variants for user review.

## Persistence and compatibility

New variants use `negative-stick-ball-intro-bores-pcg32-v1` or
`negative-stick-ball-through-pcg32-v1`, with optional `generator.cutter_mode`
metadata. The explicit edits and geometry hash remain authoritative. Original
mode preserves the previous generator identifiers and serialized recipe bytes;
both 2k and 4k originals were compared against their pre-change exports.

The existing recipe version, GPU edit layout, field builder, tracer and
lighting shaders are unchanged. The nine compiled shader hashes match the
previous width study. The desktop Cutter mode combo stages the treatment;
Generate scene applies it. Existing presets default to Original mode.

## Native evidence

`outputs/sdf/dense-bore-qualification/manifest.json` pins clean source
`ce6aefc9`, binary/shader hashes, applied configurations, explicit recipes,
commands, captures and logs. Device: RTX 5070 Ti / driver 610.57.04. Every
native run requires Vulkan validation. CPU recipe/timeline tests pass,
including through-chord removal, endpoints, unchanged tail edits, count
prefixes, small recipes, metadata roundtrip and invalid-mode rejection.

| Treatment | Cutters | Active bricks | Oracle hits / 514 rays | Ambiguities |
| --- | ---: | ---: | ---: | ---: |
| Original | 2,048 | 533,690 | 207 | 10 |
| Original | 4,096 | 426,004 | 169 | 7 |
| Intro bores | 2,048 | 546,118 | 210 | 10 |
| Intro bores | 4,096 | 436,391 | 172 | 7 |
| All original lines extended | 2,048 | 352,674 | 155 | 9 |
| All original lines extended | 4,096 | 84,420 | 46 | 4 |

All six checks report zero mismatches and exhausted rays under the unchanged
finite-grid tolerance. Large recipes probe 64 removals across the full list,
including first and last, rather than every cut. Thin remnants can alter
which surface a grazing ray meets first: maximum first-hit differences are
0.102109 for 4k intro bores and 0.302992 for 2k all-through. These are reported
as sampling ambiguities within the existing inner/outer sampling bracket;
they do not establish exact analytic first hits. Maximum surface residuals
across these six cases range from 0.000115956 to 0.000140294.

The intact, one-bore and four-bore fixtures pass 259/263/275 rays with no
mismatches, exhaustion or sampling ambiguities. Native image comparison
finds 237 sky-colored pixels at x=586–605 / y=313–332 where the intact ball
was gold. A decoded video frame at 2.7 seconds retains 216 such pixels.
The cyan guide appears before the cut; the opening remains after it disappears.

The 4k intro recipe hash is `db4eede129e38d27`; 2k is `43a938c767e6a3d3`.
The reloaded explicit recipe is byte-identical. Incremental video construction
finishes at 436,391 bricks and passes the same 514-ray check / 172 hits / seven
ambiguities with zero mismatches or exhausted rays. FFprobe confirms 720
H.264 frames, 1280x720, 30 FPS and 24 seconds, with 2x internal sampling.
Native scene/normal/opposite and decoded video captures were inspected.

Allocation stays fixed at 1,974,880,928 bytes with capacity 778,688 bricks.
No atlas growth or edit dropping was needed. Reference validation is CPU
work and is excluded from render-time claims; no new frame-time benchmark or
smooth-live-carving qualification is claimed. Detached remnants remain static.

A private Xvfb check passes resize, orbit, zoom, pause/reset and mode staging.
Selecting Original causes no build until Generate scene; the resulting hash
matches the original 4k recipe `ae1b36c249c6c5e9`, with exactly two field builds
including startup. Receipt: `outputs/sdf/dense-mode-controls/windowed-interaction.json`.

The static report passes isolated Chrome checks over local no-Range HTTP and
the published URL: seven decoded chapter seeks, playback, three intro stages,
all six mode/count combinations, image decoding, links, 390px layout and no
JavaScript errors. Receipts are `outputs/sdf/dense-bore-browser-review.json`
and `outputs/sdf/dense-bore-delivery.json`. Publisher delivery separately
verifies HTTP 200 and entrypoint bytes; the snapshot contains 76 files /
32,862,609 bytes.

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/negative-stick-ball-dense.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/negative-stick-ball-dense.json --count 2048
build/dev/projects/sdf/sdf --config projects/sdf/presets/negative-stick-ball-dense.json --cutter-mode through
```

# Subtracting the original slender sticks

Date: 2026-10-21

[Matched comparison, count gallery and carving video](http://starship:8787/reports/0254a0153dd18172f3e8ff11cb667a5b6219c4cf11a41f11c48121c817866851/).

The user found this original-width result too subtle. The subsequent
[intermediate-width audition](sdf-intermediate-width-evidence.md) adds an
adjustable width while preserving this default recipe and hash.

The user pointed out that the earlier negative tests used much larger boxes
than the additive ball of sticks. That comparison was misleading: even the
quarter-width trial retained much wider, longer cutters. This experiment
reuses the additive stick geometry exactly, changing Fill to Remove after an
initial spherical Fill.

## Geometry

| Parameter | Original additive / new negative | Earlier broad negative |
| --- | --- | --- |
| Transverse half extents | 0.001, 0.001 | 0.02–0.03 after cutter 16; wider earlier |
| Rounding | 0.0005 | 0.004 |
| Full transverse width, including rounding | 0.003 | 0.048–0.068 after cutter 16 |
| Long half extent | 0.05–0.20 | 0.65 |
| Full length, including rounding | 0.101–0.401 | 1.308 |
| Placement | Centers at radii 0.29–0.30, original random orientations | Through-bores offset in a radius-0.20 disk |

The previous quarter-width trial was still 4–6 times wider than the additive
sticks after cutter 16 and had full length 1.302. Its capacity failure was
specific to that geometry, not a general limit on slender subtraction.

The new `negative-stick-ball` scene calls the original `sticks` generator,
changes every returned operation to Remove and prepends a radius-0.30 sphere.
This starting radius matches the original placement shell; it differs from
the earlier radius-0.36 negative sphere. Parts of a stick outside the sphere
remove nothing. This is a literal counterpart to the additive recipe, rather
than a controlled change of width alone.

The generator identifier is `negative-stick-ball-pcg32-v1`. Count excludes
the base and is limited to 65,535. Zero cutters retains the solid; larger
counts preserve the exact earlier recipe as a prefix. Existing generators,
presets, field/tracing shaders and oracle tolerances are unchanged.

`projects/sdf/presets/negative-stick-ball.json` selects 2,048 cutters, seed 42,
axis 92, closer camera distance 1.1, hard shadows, atmosphere sky and the
existing 24-second carving timeline. The ground and AO remain off. The paired
additive/negative stills use distance 1.35 for both, to fit the protruding
additive sticks; all other camera and lighting settings match.

## Native evidence

`outputs/sdf/thin-sticks-qualified/manifest.json` records source `e553d3e1`,
clean at capture start, binary/shader hashes, commands, explicit recipes,
applied configurations and capture hashes. Device: RTX 5070 Ti, driver
610.57.04. Vulkan validation is required for each native run.

CPU tests pass for exact additive geometry reuse at 2,048 sticks, preserved
count prefixes, seed changes, explicit serialization, the spherical base,
zero cuts and the recipe size limit. Comparing the saved native recipes
independently also confirms exact geometry equality after changing operation.

| Cutters | Active bricks | Oracle rays | Hits | Sampling ambiguities |
| --- | ---: | ---: | ---: | ---: |
| 0 | 127,784 | 259 | 102 | 0 |
| 256 | 169,457 | 1,283 | 957 | 4 |
| 512 | 207,581 | Capture only | — | — |
| 1,024 | 281,053 | Capture only | — | — |
| 2,048 | 416,032 | 514 | 311 | 3 |
| 4,096 | 629,835 | 514 | 314 | 7 |

Checked scenes report zero mismatches and exhausted rays. At 2,048 cuts,
maximum surface residual is 0.000187113 and maximum first-hit distance
difference is 0.00589252; ambiguities remain separately reported under the
existing finite-grid tolerance. The recipe hash is `ddb62b0334b76e95`.
At 4,096 cuts the hash is `5e37bbff157ce6dd`.

The saved 2,048-cut recipe passes the same 514-ray check after loading. The
24-second carving capture also finishes at 416,032 bricks and passes the same
514 rays / 311 hits / three ambiguities with no mismatches or exhaustion.
FFprobe confirms 720 H.264 frames at 1280x720 / 30 FPS; internal sampling is
2x. Decoded frames were inspected. This remains offline logical playback,
not a qualification of smooth live carving.

The ray validator now also probes large subtractive recipes: 64 removals
sampled evenly across their full list, including the first and last. Each
contributes an exterior ray and three rays starting inside its cavity along
its axes. Small recipes through 257 total edits still probe every edit; large
additive recipes retain their previous ray set. This avoids relying on a
coarse exterior grid to hit tiny slots by chance. It does not test every
cutter in a large recipe or establish exact analytic CSG rendering.

The axis-92 field allocation stays at 1,974,880,928 bytes, with capacity
778,688 bricks. An 8,192-cut construction fails explicitly at frontier 784,556
/ references 32,353. It leaves no success image; no atlas was enlarged and no
cuts were dropped. These brick counts do not measure steady rendering time.

A private Xvfb check of the new preset passes resize, orbit, zoom, Space, R
and Escape with one field build; camera/input changes reuse the field. This
does not repeat the earlier carving-control interaction matrix for the new
scene. See `outputs/sdf/thin-sticks-qualified/windowed-interaction.json`.

## Visual result

Native count, matched additive/negative, normal and opposite captures were
inspected. The ball remains mostly solid, with fine slots and punctures. More
cuts increase the surface pattern without creating the earlier broad wall
fragments. Much of the new internal surface is hidden by the remaining solid,
so this literal negative does not have the additive ball's exposed silhouette.
That is a visual limitation for review, not a renderer failure or a claim
that the result is a successful presentation demo.

The report reuses seekable Blob video delivery on the static server. Isolated
headless Chrome checks pass both local no-Range HTTP and the published URL:
all six chapter seeks, playback, all six count selectors and active states,
image decoding, links, 390px layout and no JavaScript errors. Desktop/mobile
report screenshots were inspected. Receipts are
`outputs/sdf/thin-browser-review.json` and `outputs/sdf/thin-delivery.json`.
The publisher independently verifies HTTP 200 and entrypoint bytes; the
snapshot contains 66 files / 21,374,190 bytes.

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/negative-stick-ball.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/negative-stick-ball.json --count 4096
```

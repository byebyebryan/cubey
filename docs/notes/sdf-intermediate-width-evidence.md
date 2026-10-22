# Negative sticks: intermediate width

Date: 2026-10-21

[Width/count comparison and carving video](http://starship:8787/reports/32adae0557e41e2a1b5d3d014b07ea67c4225c71dd33c9862dc740d9cbfcbcca/).

The user subsequently preferred width 0.024 with 2k/4k cuts and a readable
through-hole introduction. The [dense-bore follow-up](sdf-dense-bore-evidence.md)
implements that direction; this record retains the earlier width audition.

The user found the exact additive-width subtraction too subtle and requested
a balance with the earlier broad cuts. This trial holds the radius-0.30 base,
seed 42, stick centers, rotations, long box cores, camera and lighting fixed.
Only transverse width/rounding and requested cutter count vary.

## Audition and candidate

Sixteen native captures compare full widths 0.003, 0.009, 0.015 and 0.024
against counts 128, 256, 512 and 1,024. All fit the explicit axis-92 atlas.
Saved edit lists were checked for exact count prefixes within each width,
identical transforms and long cores across widths, and requested full
transverse dimensions including rounding. The geometry audit is in
`outputs/sdf/medium-width-audition/geometry-audit.json`.

To the agent's eye, **0.015 / 512** gives readable grooves while retaining
broad spherical surfaces. **0.009 / 1,024** is a subtler alternative. Wider
0.024 cuts and higher counts take the surface toward a broken shell. These
remain candidates for the user's visual review. The report preserves the
entire matrix with independent width/count controls, rather than only the
agent's selection.

`projects/sdf/presets/negative-stick-ball-balanced.json` selects width 0.015
and 512 cutters. It retains the existing carving timeline, hard self-shadows,
shared atmosphere sky, material and distance-1.1 camera. The ground and AO
remain off. Native shaded/normal/opposite images, decoded video frames and
desktop/mobile report screenshots were inspected.

## Adjustable width and persistence

`--cutter-width` applies to `negative-stick-ball`, with full width 0.003–0.06.
It scales both transverse half extents and rounding proportionally; long box
cores remain 0.05–0.20, while rounded end caps grow slightly with width.
The desktop width control stages a generator parameter; Generate scene
applies it. Loaded explicit edits remain authoritative until regeneration.

The default 0.003 geometry, generator identifier, serialized metadata and
2,048-cut hash `ddb62b0334b76e95` remain unchanged. Nondefault widths use
`negative-stick-ball-width-pcg32-v1` and persist optional
`generator.cutter_width` metadata alongside every explicit edit. The recipe
format/version and geometry hash algorithm remain unchanged. Older readers
that already support spheres can replay these explicit edits, but drop the
new descriptive metadata if they resave. No GPU edit layout or shader changes
were made.

## Native evidence

The matrix manifest pins clean source `c15d47da`; selected captures pin clean
source `0214aadc`, whose only intervening change adds the selected preset.
Both record native binary/shader hashes, configurations, recipes, commands,
capture hashes and logs. Artifacts are in `outputs/sdf/medium-width-audition/`
and `outputs/sdf/medium-selected/`. Device: RTX 5070 Ti, driver 610.57.04.
Every native capture requires Vulkan validation.

- CPU tests pass for requested full widths, preserved transforms/long cores,
  count prefixes, metadata roundtrip, invalid width rejection and the original
  thin recipe hash.
- The selected 512-cut recipe has hash `50099b4951da321a` and 419,880 active
  bricks. Its oracle passes 514 rays / 281 hits with zero mismatches or
  exhausted rays and four sampling ambiguities. Maximum surface residual is
  0.000117175; maximum first-hit difference is 0.0147393.
- The 0.009 / 1,024 alternative has 489,523 bricks and passes 514 rays / 299
  hits, zero mismatches/exhaustion and nine ambiguities.
- Loading the selected explicit recipe and completing incremental carving
  both pass the same selected ray check. The video is 720 H.264 frames,
  1280x720, 30 FPS, 24 seconds and 2x internal sampling. Offline capture does
  not qualify smooth live geometry updates.
- A private Xvfb check stages width 0.009 without a rebuild, then explicitly
  generates it. The new hash matches the width-0.009 / 512-cut matrix recipe.
  Resize, orbit, zoom, pause and reset also pass. The control receipt is
  `outputs/sdf/medium-width-controls/windowed-interaction.json`.

Ray checks use the unchanged finite-grid tolerance and 64 sampled removals
across larger recipes, including first and last. Sampling ambiguities remain
separate from mismatches; first surfaces at grazing angles can be sensitive
to grid sampling. Other matrix cells are visual captures, not separately
ray-qualified scenes. Allocation stays at 1,974,880,928 bytes with capacity
778,688 bricks. No new frame-time benchmark is claimed.

The report passes isolated Chrome checks over both local no-Range HTTP and
the published URL: all six decoded chapter seeks, playback, all 16 width/count
combinations and active states, native image decoding, links, 390px layout
and no JavaScript errors. Delivery receipts are
`outputs/sdf/medium-browser-review.json` and `outputs/sdf/medium-delivery.json`.
The publisher independently verifies HTTP 200 and entrypoint bytes; the
snapshot contains 115 files / 17,935,231 bytes.

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/negative-stick-ball-balanced.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/negative-stick-ball-balanced.json \
  --cutter-width 0.009 --count 1024
```

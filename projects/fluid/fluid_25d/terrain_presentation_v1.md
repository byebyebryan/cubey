# Terrain presentation V1

Scope: existing unmodified Mountain, Upland basins, Joining valleys and Steep
ridges at 1024² / 16 m. Macro presentation, not ground-level realism. No assets,
clutter, vegetation, solver port, elevation edits, erosion or finer simulation.

## Recommendation

Retain the 2× B-spline display mesh. Use the shared full-source correlated
terrain masks and a camera-independent whole-terrain 2048² shadow box fit.
The prepared terrain launcher selects this combination; old direct-app studies
retain legacy projection, and all four material organizations remain available.
The review is at
[the report](../../../outputs/fluid/terrain-presentation-v1-20261011-HPKiTL/index.html)
or `http://starship:8801/fluid/terrain-presentation-v1-20261011-HPKiTL/`.

The repeated close-up striping was mostly false self-shadowing, not missing
simulation cells: no-shadow, albedo and analytic-base-normal controls separate
it from material and mesh effects. Tightening projection alone made it worse.
The old receiver-plane depth gradient was clipped to ±1. Valid steep receivers
can exceed that, so the new fitted path uses the actual inverse screen-to-shadow
gradient. GPU affine-plane controls cover gradients (3, -5), scaled pixel
derivatives, ordinary slopes, degenerate projections and tent normalization.
Legacy explicitly retains the old clipped-gradient behavior for pixel A/B.

Residual bias is now measured in shadow texels/world metres, then normalized by
the actual light-depth span: half a texel of constant guard plus a two-texel
maximum curvature allowance. It does not displace terrain or water. A box fit
projects every terrain-bound corner, including offscreen hills, with guard
bands; it does not vary with the observer camera or lose out-of-frustum casters.
The reusable bounds helper and low/vertical-light tests live in Cubey Render
and Terrain. No cascade, cache lifecycle or new material classifier was added.

Source-aligned organization reuses `cubey::terrain::TerrainSurfaceField` and
the existing procedural substrate palette. Climate is long-term bare-ground
potential, not current wetness or an ecology/biome simulation. Live depth still
drives wet-ground shading. Existing overlap/derived-spacing tests compare
Fluid samples directly with the Terrain foundation. Site differences are not
hard-coded palette selections by site name.

## Evidence boundaries

Captures use sealed native states with explicit saved per-site camera recipes.
Mountain's developed state is about 10.6 physical hours; the other three are
about six hours. Those are matched before/after within each site, not identical
hydrologic ages across sites. Dry captures use the original zero-water field.
Short camera sweeps hold native fields and do not show new solver evolution.

The harness removes DISPLAY, WAYLAND_DISPLAY and private-Xvfb variables and
checks the actual NVIDIA Vulkan device and bit-exact upload receipts. Renderer
groups are bounded and reaped on failures/timeouts. PNG/video capture costs
are not FPS evidence. Continuous offscreen profiles use no readback or encoder;
live trials run the actual external solver, while held trials isolate renderer
cost. Native GUI layout/scanout and owner visual acceptance remain separate.

Final metrics, parity, rejected alternatives and known remaining limitations
are in the report's validation receipt. Do not infer performance for 2k grids,
every camera, every light direction, or late-time solver states from the 1k
bounded live trials. The original 30 m source information still limits detail.

## Measured performance

All 16 sequential paired trials completed on the frozen Release binary and
shaders. Legacy/candidate order reverses on repeat two. Each trial uses
1920×1080 and a 60 Hz render cap; live requests 60× native pace. Native prewarm
to 1,800 physical seconds occurs before renderer startup. Five wall seconds of
warmup precede each 45-second live or 15-second held measurement window.
The held states are the developed recordings described above, not equal-age
controls for the live trial. Neither timing mode reads back images or encodes video.

The ranges below are the two **individual trial p95 values**, not a pooled
percentile. Every candidate trial passes GPU p95 ≤8 ms, live pace ≥54× and
advancing-field age p95 ≤0.5 s.

| Candidate site/mode | Scene GPU p95 (ms) | Actual native pace | Field age p95 (s) |
| --- | --- | --- | --- |
| Mountain, held | 4.20–4.21 | no solver | held |
| Mountain, live | 5.24–5.30 | 59.92–59.97× | 0.143–0.146 |
| Steep ridges, held | 3.97–4.49 | no solver | held |
| Steep ridges, live | 4.07–5.22 | 60.00–60.03× | 0.144–0.145 |

These are scene GPU timestamps while CUDA shares the device, not pure native
kernel timings. CPU frame service excludes intentional pacing. A capped loop
near 16.74 ms is not evidence of uncapped FPS. Existing desktop GPU background
work was left running. Full per-trial legacy/candidate results are in
`paired-performance/results.json` under the report leaf.

The 4× geometry control gave little visible benefit but reached 8.69 ms GPU
p95 in the short Mountain held probe; retain 2×. A 4096² shadow control remains
available for study but is not promoted. Its short probes are not balanced
evidence that it is faster than 2048². Tightening shadow bounds without fixing
the clipped steep-plane gradient was explicitly rejected.

## Final validation

The selected 55 CTest checks passed without failures or skips; the final
headless-policy/review-helper checks also passed. The three Python contract
modules passed 34 checks, including incomplete evidence, per-trial gate
failures, immutable inputs and failed-process cleanup.

Legacy projection/material images remain byte-identical in all 12 views.
All eight raw depth/speed images match across legacy/candidate appearance.
42 of 46 compiled shaders are unchanged; only the four shared lighting
consumers differ. Every final capture has a bit-exact native terrain/depth/
velocity GPU-upload receipt, without hydraulic dispatches.

The managed 160-second headless lifecycle run acknowledged all 14 actions:
start, pause, rain adjustment, reset, site change and 512²/32 m, 1024²/16 m,
2048²/8 m grid changes. It returned cleanly, had no Vulkan validation errors
and reaped all owned processes. Its mixed-switch timings are not used as
steady-state performance evidence.

The four camera-sweep videos contain 120 frames at 24 Hz each. Their receipts
prove a constant native playhead/state and zero visual time advance, while
decoded frames change with the camera. The recording API requires a positive
interval; starting at the final recording time clamps the playhead there.
Dry sweeps are deliberately rejected by this harness rather than claiming a
hold the API does not supply. Three setup-only failed phases remain archived
and excluded from acceptance; no failed capture or timing trial was promoted.

The report's 33 local HTTP targets return 200 and its page appears in the
existing report index. Desktop and narrow HTML layouts were inspected with
headless Chromium. Cubey was never opened as a native GUI. Owner visual
acceptance and native scanout remain deferred; finer shadow patterning and
source-limited surface detail remain explicit limitations.

## Reference and reproduction

The sequence follows established shadow-map practice: diagnose first, tighten
light-space coverage/depth range and correct bias before adding cascades.
[Microsoft shadow-map guidance](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps)
describes these tradeoffs. The existing four-wide comparison tent follows
[GPU Gems shadow filtering](https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing);
filtering raw depth alone would not solve this problem.

```sh
rtk proxy python3 projects/fluid/fluid_25d/review_terrain_presentation_v1.py \
  --out outputs/fluid/terrain-presentation-v1-FRESH --phase candidate \
  --surface correlated --shadow scene
```

Create a fresh owned output leaf first. The harness refuses to overwrite a
phase; its frozen executable and shader/input hashes accompany every capture.
`--subdivision 1|2|4`, `--shadow-extent 2048|4096`, `--dry`, component `--views`,
`--video --sweep .2`, and `--seconds 15` are independent diagnostic controls.
Profiling and video are deliberately separate modes.

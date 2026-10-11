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

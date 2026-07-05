# Terrain Mountain Visual Source Pivot

Date: 2026-07-05

The current mountain stress recipe has useful field plumbing, but the visible
terrain still reads too synthetic in surface and perspective review. The main
failure is not a missing renderer feature. The source height is still dominated
by graph-derived ridge masks, broad smoothing, and then diagnostic processes
layered on top. That produces rounded bulges, thin fin-like ridges, stepped
shoulders, and process streaks that look authored instead of emergent.

## Direction

Treat `temperate-mountain-range-stress` as a clean-room visual mountain source
experiment:

- keep the broad range support from coherent source fields;
- add a ridged, domain-warped source that can form mountain-chain structure
  without drawing individual ridge lines;
- gate local detail by mountain support, ridged-chain strength, and slope so it
  belongs to the terrain instead of reading as texture;
- add slope-aware morphology as bounded source shaping for this stress recipe,
  while keeping the existing gully/talus fields as review diagnostics.

This intentionally moves the stress recipe away from using
`mountain_ridge_skeleton` as the visible landform driver. The older graph fields
remain useful diagnostics, but they should not be the main shape the camera sees.

## Reference Lessons

ShaderToy mountain examples are useful because they often produce convincing
terrain from compact sampled functions: low-frequency mass, ridged multifractal
structure, domain warping, derivative or slope feedback, and a separate
high-detail normal/material path. Cubey should borrow those concepts, not code.

The erosion ShaderToy examples are useful as morphology references: gullies and
creases are slope/local-relief processes over height and derivatives. They are
not a reason to call the current pass hydraulic erosion, and they do not replace
the longer river/hydrology path.

TerrainEngine is useful on a different axis. Its tiled grid, shader-side height
sampling, material normals, and distance-driven tessellation inform future
runtime/LOD work. It does not provide a richer CPU terrain source model to port
into this batch.

## Batch Target

Revision 32 should add terrain-local fields for the mountain stress recipe:

- `mountain_visual_source_height_m`
- `mountain_ridged_chain`
- `mountain_detail_weight`
- `mountain_morphology_delta_m`
- `mountain_crease_map`
- `mountain_ridge_map`

The final stress height should come from the visual source after bounded
morphology, then the normal river/material/slope/local-relief products should be
computed from that changed height. Default river recipes should keep these new
fields inactive.

## Review Targets

The refreshed capture set should include:

- `outputs/terrain/mountain-range-stress/mountain-visual-source-height.png`
- `outputs/terrain/mountain-range-stress/mountain-ridged-chain.png`
- `outputs/terrain/mountain-range-stress/mountain-morphology-delta.png`
- `outputs/terrain/mountain-range-stress/mountain-process-review.png`
- `outputs/terrain/mountain-range-stress/mountain-perspective.png`
- `outputs/terrain/mountain-range-stress/mountain-surface-height.png`

The expected result is not finished mountains. The expected result is a more
coherent source: broader range buildup, visible but not fin-like ridges, stronger
near-field relief, and fewer obvious graph/smoothing artifacts than revision 31.

## Revision 32 Result

Revision 32 landed the visual source and bounded morphology fields. The older
graph ridge/valley fields remain exported diagnostics, but
`mountain_valley_incision_m` is no longer subtracted from the visible mountain
height because its scalar view still reads like graph topology. The visible
stress height now follows:

```text
mountain_visual_source_height_m
  -> bounded mountain_morphology_delta_m
  -> mountain_profile_height_m / height_m
```

The regenerated 513 mountain stress manifest reports 64 fields, 59 scalar/debug
views, `height_m.span = 2470.630`,
`mountain_visual_source_height_m.span = 2387.518`,
`mountain_profile_height_m.span = 2380.816`, and
`mountain_morphology_delta_m.max = 47.635`. The 1025 surface capture was also
refreshed locally for close-range scale checking.

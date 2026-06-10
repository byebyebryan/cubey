# Ocean Rendering Direction

Cubey now keeps three ocean lanes with different jobs:

- `projects/ocean` is the active renderer. It uses the known-good
  GodotOceanWaves-derived spectrum/FFT/unpack core as a guardrail while keeping
  configurable cascade slots, atmosphere integration, terrain-field
  descriptors, expanded foam diagnostics, and debug views behind explicit
  feature-isolation controls.
- `projects/ocean_ref` is the frozen reference port. Keep it source-stable so
  active ocean changes can be checked against a working wave-shape guardrail,
  not treated as an oracle. It is intentionally exempt from current active-panel
  UI cleanup unless a bug blocks comparison work.
- `projects/ocean_legacy` is the previous Cubey experimental renderer. It is a
  feature donor for macro crests, persistent foam history, refraction, seafloor,
  atmosphere hooks, and shoreline/bathymetry ideas. It is not expected to track
  shared UI/config helper adoption except when a feature is actively ported.

This split keeps wave shape and foam quality grounded in a known-good
implementation while making experimental contributions directly inspectable in
the active renderer.

## Current Active Shape

`projects/ocean` is not a replacement for `projects/fluid/water_3d`: the tank
project starts from particle-grid liquid simulation, while ocean starts from a
camera-relative surface renderer and only adds simulation where interaction
needs it.

The current renderer has reached the horizon-scale/curved-local endpoint
captured in
[Ocean horizon and curved-local direction](ocean-horizon-and-planet-scale.md):
it derives effective ocean extent from camera altitude, keeps a
planet-compatible flat surface-mapping seam, routes
projection/atmosphere/datum metadata through an explicit `OceanSurfaceFrame`,
and bends the far field through the default `curved-far` surface mode.

This is a reasonable scale endpoint for `projects/ocean`. It should remain a
focused horizon/curved-local water renderer with strong diagnostics, not become
the owner of global planet coordinates, patch streaming, terrain, weather, or
clouds. Future planet work should port or wrap the ocean renderer once
`projects/planet` has hardened local/global morphing, persistent topology,
streaming, and render-order contracts enough to host it.

The active renderer includes:

- a camera-relative clipmap mesh with horizon-derived effective extent,
  mesh-cell-aware cascade LOD diagnostics, and explicit horizon coverage
  readouts;
- a local-tangent ocean surface frame that owns the effective mesh, projection
  far plane, water datum, planet radius, camera altitude, and flat/curved-far
  shader surface metadata for the current frame;
- the GodotOceanWaves-style multi-cascade spectrum, modulation, FFT, and unpack
  path;
- five regular cascade slots, with C0/C1 enabled by default as the
  reference-derived core pair and C2-C4 kept as opt-in candidates for
  large-scale breakup or fine detail experiments;
- displacement, normal, foam, LOD, curvature, environment-lighting, and
  terrain-field debug views;
- cascade isolation, camera presets, pause/step timing, and mesh diagnostics
  for interactive inspection;
- a 50 m sea-level-centered reference pillar in final view, currently for scale
  and as a simple analytic direct-light ocean shadow caster;
- experimental heightfield ray-marched wave self-shadowing that samples the
  FFT displacement cascades in the surface fragment shader and follows the same
  local cascade distance/cell-size fade used by rendered displacement;
- the shared atmosphere background path plus runtime sky/reflection probes for
  water aerial-perspective placeholder, fill, and reflection;
- a diagnostic terrain-ocean `RGBA32F` field texture bound through the shared
  height/depth/shore/slope contract;
- active `--ocean-*` CLI controls, while the frozen reference keeps
  `--ocean-ref-*` controls.

The active renderer should now be evaluated feature by feature rather than
pulled wholesale back to the reference ABI. The GUI's Feature Isolation section
exposes global shape and foam strength, foam history, shape and detail
anti-repeat, active cascade-slot work toggles, split atmosphere material
influence, shape/normal/foam fade controls, and terrain foam controls so each
addition can be checked against `ocean_ref` for quality and cost. Cascade work
toggles skip disabled cascade compute dispatches; contribution sliders only
change surface composition.

Performance work should start from the spectral ocean cost model captured in
[Ocean performance notes](../notes/ocean-performance.md). The current default
uses a `512` FFT map, half-precision wave fields, packed FFT storage fields,
and lazy allocation for enabled cascades only. `1024` remains a
maximum-quality brute-force inspection mode, not the default target. The
near-term direction is to keep FFT for coherent broad/mid ocean motion while
recovering close-up detail through cheaper shading, foam, and analytic shape
paths where possible.

## LOD And Far Field

The active ocean remains on a camera-relative `ClipmapGrid2D` mesh. That is
still the right near-term base for local, horizon-scale ocean work because the
wave data is camera-local, FFT cascades are sampled in tangent-space XZ, and the
renderer needs predictable ring diagnostics while crest, foam, and lighting are
still changing. Do not migrate ocean to the planet `AdaptivePatchLod` planner
until shoreline handoff, global weather/bathymetry, or object occlusion needs a
shared planet address space.

The current policy treats far-field repetition as a data-domain problem first,
not only a mesh problem. Enabled FFT slots can still be regular cascades, but
their contribution should be explicit:

- displacement fades by both camera distance and clipmap cell size;
- normal/foam contribution has a separate surface-distance fade;
- diagnostics report the effective horizon-expanded mesh, near/far cell size,
  patch load, and per-cascade shape/surface weight at the horizon;
- headless captures can pin `--ocean-camera-preset mid|high|wide` so far-field
  changes are comparable without relying only on interactive inspection.

The default fade bands are deliberately conservative after the anti-repeat
experiments: displacement fades across roughly 8-24 wavelengths, surface detail
across roughly 10-30 wavelengths, and coarse rings reject displacement between
about `tile / 10` and `tile / 4`. If this makes the far view too smooth, prefer
raising the exposed shape/normal/foam fade scales or adding a separate,
low-frequency far-field domain before reintroducing high-frequency tile cues at
the horizon.

## Feature Donor Boundaries

Useful `ocean_legacy` ideas to revisit later:

- macro or Stokes/Gerstner-style crest trains for stronger near-field silhouette;
- persistent foam coverage/freshness history and breakup detail;
- scene color/depth refraction, absorption, and seafloor visibility;
- local disturbances and shoreline/bathymetry masks;
- broader debug views for compression, thickness, foam source/history, and
  translucency inspection.

Legacy code is evidence, not the base. The default path should be to port the
smallest useful slice into `projects/ocean`, use `projects/ocean_ref` only as a
guardrail, and keep the reference intact.

## Rendering References

The active renderer is intentionally closer to a single public implementation
than the legacy renderer was:

- GodotOceanWaves for the immediate wave-generation baseline.
- Tessendorf-style FFT spectra for scalable deep-water motion.
- GPU Gems water guidance for separating geometric displacement from surface
  detail, depth/Fresnel shading, and sampling-footprint filtering.
- Crest-style whitecaps as a later model for foam persistence and art-directed
  controls.
- TDM/Inigo Quilez-style seascape presentation lessons for coherent sky color,
  sun glint, nonlinear wave shape, and fogging the far surface into the sky.

Pure ray-marched seascapes can look excellent in a standalone shader, but Cubey
still wants a mesh-backed route toward boats, shorelines, depth buffers, and
scene integration.

## Interaction Path

The renderer should expose inputs that later systems can feed:

- local disturbances for boat wakes, impacts, and gameplay ripples;
- shoreline/bathymetry masks for depth attenuation, surf foam, and shallow-water
  coupling;
- optional foam/whitewater fields that can be visual-only or simulation backed.

V1 keeps these as compact controls and shader inputs. Boat physics, buoyancy,
shoreline authoring, and shallow-water simulation should arrive as separate
projects or later integration slices.

The current terrain-field path is intentionally a diagnostic consumer, not a
full shoreline renderer. It proves the descriptor and shader contract, exposes
field debug views, and provides a small opt-in shoreline foam hook. Real
bathymetry, scene depth, seafloor shading, and surf-zone wave behavior still
belong to later terrain/ocean integration work.

## Breaking Waves

Deep-water whitecaps are reasonable in the renderer: detect steep/choppy crests
and add foam. True plunging shore breakers are not just a shader feature. They
need terrain or bathymetry, shallow-water/surf-zone state, nonlinear local wave
deformation, and often particles or volumes for aerated water.

Recommended sequence:

1. Keep active ocean close to the reference-derived wave core.
2. Port only the smallest legacy feature slices that improve crest shape,
   whitecaps, or diagnostics without regressing the baseline.
3. Add local wake/disturbance textures.
4. Add terrain/bathymetry and shoreline foam.
5. Couple to `fluid_25d` or a local surf simulation for actual breaking waves.

This keeps the project useful while avoiding another large, hard-to-debug ocean
rewrite.

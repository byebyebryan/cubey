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

The current renderer is now moving through the T1 horizon-scale path captured
in [Ocean horizon and planet scale](ocean-horizon-and-planet-scale.md):
derive effective ocean extent from camera altitude, keep a planet-compatible
flat surface-mapping seam, route projection/atmosphere/datum metadata through
an explicit `OceanSurfaceFrame`, and bend the far field through the default
`curved-far` surface mode before attempting full planet-scale terrain/ocean
streaming.

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

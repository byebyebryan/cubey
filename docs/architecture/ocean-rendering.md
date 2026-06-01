# Ocean Rendering Direction

Cubey now keeps three ocean lanes with different jobs:

- `projects/ocean` is the active renderer. It starts from the known-good
  GodotOceanWaves-derived spectrum/FFT/unpack core and should stay close to
  that baseline until changes clearly improve it.
- `projects/ocean_ref` is the frozen reference port. Keep it source-stable so
  active ocean changes can be checked against a working wave-shape guardrail,
  not treated as an oracle.
- `projects/ocean_legacy` is the previous Cubey experimental renderer. It is a
  feature donor for macro crests, persistent foam history, refraction, seafloor,
  atmosphere hooks, and shoreline/bathymetry ideas.

This split keeps wave shape and foam quality grounded in a known-good
implementation while preserving the older work for selective porting.

## Current Active Shape

`projects/ocean` is not a replacement for `projects/fluid/water_3d`: the tank
project starts from particle-grid liquid simulation, while ocean starts from a
camera-relative surface renderer and only adds simulation where interaction
needs it.

The active renderer currently focuses on:

- a camera-relative clipmap mesh with LOD diagnostics;
- the GodotOceanWaves-style multi-cascade spectrum, modulation, FFT, and unpack
  path;
- five regular cascades ordered from broad macro swell through primary crest to
  fine normal/foam detail, with storm-biased defaults for stronger long-wave
  displacement and crest energy;
- displacement, normal, foam, LOD, environment-lighting, and terrain-field
  debug views;
- cascade isolation, camera presets, pause/step timing, and mesh diagnostics
  for interactive inspection;
- the shared atmosphere background path plus runtime sky/reflection probes for
  water fog, fill, and reflection;
- a diagnostic terrain-ocean `RGBA32F` field texture bound through the shared
  height/depth/shore/slope contract;
- active `--ocean-*` CLI controls, while the frozen reference keeps
  `--ocean-ref-*` controls.

The active renderer should not immediately absorb all legacy features. Additions
should be ported one at a time only when they preserve or improve the
interactive inspection result. The first bias is toward diagnostics that help
reason about the wave core, not snapshot comparison tooling.

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

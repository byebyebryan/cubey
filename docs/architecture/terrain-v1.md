# Terrain V1 Runtime

Date: 2026-07-10

Status: reboot design. The previous CPU patch and analytical landscape work is
preserved in `projects/terrain_hydrology_lab`; it is not the terrain v1 product.

## Goal

Terrain v1 is a deterministic, directly sampleable planar heightfield runtime.
It should provide a credible procedural landscape for rendering-engine stress,
surface traversal, and future scene backdrops without requiring an offline
generation pass.

The first product is deliberately narrower than a terrain simulator:

- one coherent source model with `mountain`, `upland`, and `plains` presets;
- matching GPU rendering and CPU point queries;
- optional bounded local weathering;
- a camera-centered LOD renderer and a traversable standalone scene;
- neutral diagnostics and multi-seed visual review.

Hydrology, rivers, lakes, coastlines, biomes, vegetation, planet mapping, and
bulk field baking are separate later products or experiments.

## Source Contract

The source is evaluated in world coordinates. A preset is a parameter set for
one macro/structure/detail composition, not a separate formula or authored
map. The initial shape vocabulary follows the simple TerrainEngine reference:
coherent octave buildup followed by nonlinear elevation shaping. Cubey's shared
coherent-noise implementation replaces the reference hash/noise code.

The source stages are:

```text
world position + seed
    -> broad macro elevation
    -> structural relief and nonlinear elevation shaping
    -> footprint-filtered local detail
    -> optional local weathering
    -> height and gradient
```

All presets use the same evaluator. Their host-authored parameter tables select
frequency, amplitude, persistence, elevation power, detail balance, and physical
height scale. There are no centered masks, contours, hand-authored ridgelines,
or patch-local composition templates.

`TerrainQuery` carries world `xz` and a sample footprint in meters. A zero
footprint requests full detail; render LODs pass their geometric cell size so
unresolved octaves fade smoothly. `TerrainSample` publishes base height, final
height, gradient, and weathering delta. Normals are derived from the gradient.

The 64-bit world seed is resolved on the CPU into stable per-layer GPU seeds.
The resolved parameter block is the single preset truth consumed by generic CPU
and GLSL evaluators. CPU/GPU parity is required for all public sample outputs.

## Weathering Boundary

Local weathering is an optional finite-neighborhood transform over the source
height. It may add slope- and curvature-aware surface definition, but it must:

- remain deterministic and random-access;
- use rotationally balanced sampling rather than D8 routing;
- keep displacement bounded and preserve the macro silhouette;
- make no claim about runoff, catchments, drainage, rivers, or sediment state.

The clean source remains available in every query and debug view. Proper
hydrology stays in the paused hydrology lab until it is rebooted as its own
regional experiment.

## Runtime And Rendering

The implementation remains project-local during v1. A CPU source library and a
render library expose clean headers and shader includes, but terrain-specific
types are not promoted into the engine foundation until a second real consumer
tests the boundary.

The standalone renderer samples height in the vertex shader over a
camera-centered clipmap. The v1 default is eight LOD levels, 128 cells per axis,
a 2 m near cell, and about 16 km of outer radius. Each level snaps to its own
grid. Transition morphing and footprint-aware source filtering must prevent
cracks, popping, and high-frequency aliasing during motion.

The scene uses the shared atmosphere for sky and lighting, a project-local
surface camera whose clearance is maintained through CPU queries, and a
procedural material based on height, slope, and coherent color/normal detail.
Materials are presentation only; they do not become terrain truth. Cast terrain
shadows are outside this first slice.

## Configuration And Diagnostics

The public run controls are:

- `terrain.seed`;
- `terrain.preset`: `mountain`, `upland`, or `plains`;
- `terrain.weathering`: `off` or `local`;
- `terrain.weathering_strength`;
- existing terrain camera, cell-size, and vertical-scale controls.

The terrain app supports final surface, base/final height, slope, weathering
delta, and LOD views with top, oblique, and surface cameras. Small bounded CPU
sample grids are allowed for tests, statistics, and review metadata. The old
raw-field exporter remains with the hydrology lab; terrain v1 does not emit a
baked terrain product.

## Acceptance

Across seeds `0`, `9012`, and `12345`:

- mountain terrain builds from broad mass into substantial ridges and peaks,
  then local detail, without thin fins, flat shoulders, or spike fields;
- upland and plains preserve the same vocabulary at progressively lower relief;
- no strong axis-aligned or diagonal orientation survives into final height;
- weathering adds local definition without changing the large silhouette;
- CPU and GPU heights agree within `0.1 m` at tested coordinates and footprints;
- surface traversal keeps the camera above terrain and exposes no LOD cracks or
  discontinuities;
- rendering requires no per-frame CPU field generation or bulk artifacts.

The fixed review pack compares the reboot against `terrain-engine-ref`, but the
new runtime has no code or link dependency on `terrain_ref`.

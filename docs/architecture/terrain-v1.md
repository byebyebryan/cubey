# Terrain V1 Runtime

Date: 2026-07-15

Status: source v2.1 is frozen while terrain v1 pivots from direct runtime
sampling to the fixed-focus cached backdrop defined in
[`terrain-cached-backdrop-pivot.md`](../notes/terrain-cached-backdrop-pivot.md).
The control clipmap and quality tessellation paths remain historical review
controls, not the target product.

## Goal

Terrain v1 is a deterministic planar heightfield source plus a cached,
fixed-focus backdrop product. It should provide credible far terrain for
rendering-engine stress and scene composition without paying procedural source
or process cost every frame.

The first product is deliberately narrower than a terrain simulator:

- one coherent source model with `mountain`, `upland`, and `plains` presets;
- matching source evidence and CPU point queries;
- optional bounded local weathering;
- a setup-time baked field and cullable static backdrop mesh;
- neutral diagnostics and multi-seed visual review.

Hydrology, rivers, lakes, coastlines, biomes, vegetation, planet mapping,
translation, and streaming are separate later products or experiments.

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

The default control renderer samples height in the vertex shader over a
camera-centered clipmap. It uses eight LOD levels, 128 cells per axis, a 2 m
near cell, and about 16 km of outer radius. All levels use one origin snapped to
that finest grid. Ring overlap is an exact eleven parent cells so patch spans
retain their advertised power-of-two cell spacing. Transition vertices collapse
in `xz` while their source footprint moves toward the parent grid; height-only
snapping is not sufficient to close T-junctions. Every fragment has one LOD
owner, while a one-parent-cell raster guard and downward boundary skirts cover
residual rasterization gaps.

The opt-in mountain quality path keeps the same approximate coverage but does
not reuse the clipmap ownership topology. It submits a camera-centered,
world-aligned field of adjacent quad patches to Vulkan tessellation. The v1
quality field is 128 by 128 patches over a 32.768 km span, with a maximum 256 m
patch span and whole-patch recentering. Shared control edges and shared-edge
projected sizes select power-of-two tessellation factors from 1 through 64
against a configurable pixel target. The filtered source footprint comes from
camera distance, pixel angular span, and that same screen-space target; it does
not snap geometry or sampling to a nominal parent LOD. This is a finite
far-field quality product, not a streaming or planet-scale LOD contract. It
requires tessellation support and is not the default. Source v2 independently
extends only mountain's detail spectrum, allowing renderer and source changes
to be reviewed separately.

The scene uses the shared atmosphere integrator for sky and camera-to-surface
aerial perspective. Diffuse-irradiance spherical harmonics and the atmosphere
primary light feed a project-local dielectric GGX response. Broad direct-light
visibility comes from logarithmic samples of the clean terrain source toward
the light; it is heightfield self-shadowing, not a general scene shadow map.

The procedural material uses physical elevation, source slope, multi-scale
coherent variation, and per-layer roughness. Snow selection uses physical
elevation rather than normalized per-preset height. Material relief is filtered
from projected pixel footprint and contributes only a restrained normal
perturbation, so it cannot alter geometry or advertise LOD boundaries. Materials
remain presentation only; they do not become terrain truth.

Quality materials add four seeded, periodic, compute-generated ground, scree,
rock, and snow tiles with complete mip chains and warped triplanar projection.
These textures add sub-meter through tens-of-meter presentation bandwidth
without changing source height or introducing imported runtime assets.

The `layered` quality candidate keeps those same four material classes but
generates two 1024 x 1024 products per layer: linear albedo with blend height,
and tangent normal with roughness and bounded cavity. Explicit-gradient warped
triplanar sampling requests 8x anisotropy and falls back to trilinear filtering
when the device does not support anisotropic samplers. Height may refine an
existing material transition but cannot override slope/elevation macro weights.
Per-fragment source-normal recovery and mipmapped material normals add local
form without displacing geometry; both fade by projected footprint.
The geometry-footprint classification normal remains separate from that
recovered shading normal. Slope, vegetation, broad ambient visibility, and
macro material weights use the classification normal; layered projection and
lighting may use the more detailed shading normal. Height-assisted texture
blending refines only local layer composition and cannot rewrite macro weights.

An opt-in backdrop presentation may derive distant vegetation coverage from
height, slope, broad landform context, and footprint-filtered coherent fields.
This is material coverage only: it cannot displace terrain, change CPU queries,
provide collision, or claim individual grass or tree geometry. Its supported
scene contract begins at roughly 300 m from the visible lower frame edge.
Close-range foliage remains a separate future rendering product.

The production `backdrop` planner searches the random-access source for a local
360-degree orbit stage. It scores a bounded coarse grid, refines deterministic
shortlists, and fully evaluates 16 candidates over 24 azimuth sectors. The
selected source focus maps to local scene XZ without changing source equations,
geometry scale, or terrain shape. Terrain displacement, weathering, procedural
materials, diagnostics, and heightfield shadows all sample that translated
source location; renderer ownership and camera coordinates remain local.

Detached mode is the far-field product. The consumer owns the inner 300 m,
which terrain rendering excludes. The solver raises the physical focus enough
to keep lower-frame terrain at least 1.5 km away throughout the supported
50-250 m radius and 0-30 degree elevation envelope. Yaw is unrestricted.
Grounded mode keeps terrain continuous and searches for a naturally low-relief,
low-slope stage as a placement diagnostic. `midground` retains the older
directional 1.6 km surface camera for detail stress work; it is not the backdrop
product.

All quality views use the same screen-driven adaptive tessellation. Because the
quality mesh has shared world-aligned tile edges and no overlapping parent
levels, stage views do not need a fixed-factor exception to conceal parent/child
silhouette mismatches. The detached cutout owns rasterized geometry only. The
local heightfield-shadow approximation continues through the translated source
field so sparse horizon taps do not introduce rings at the ownership boundary.

## Configuration And Diagnostics

The public run controls are:

- `terrain.seed`;
- `terrain.preset`: `mountain`, `upland`, or `plains`;
- `terrain.source_version`: `v1`, mountain-only `v2`/`v2.1`, or retained
  experimental mountain hierarchy `v3`;
- `terrain.render_path`: `control` or mountain-only `quality`;
- `terrain.surface_detail`: `tile` or quality-only `layered`;
- `terrain.target_edge_px`: adaptive quality target from `2` through `16`;
- `terrain.weathering`: `off` or `local`;
- `terrain.weathering_strength`;
- existing terrain camera, cell-size, and vertical-scale controls;
- `terrain.backdrop_mode`: `detached` or `grounded`;
- optional `terrain.backdrop_azimuth_degrees`,
  `terrain.backdrop_orbit_radius_m`, and `terrain.backdrop_elevation_degrees`;
- `terrain.presentation`: `standard` or opt-in `backdrop` material coverage.

The terrain app supports final surface, base/final height, slope, weathering
delta, LOD, neutral clay, direct visibility, aerial transmittance, vegetation
coverage, source/material normals, material weights, albedo, roughness, blend
height, cavity, classification-normal, projected-edge, tessellation-factor, and
detached stage-ownership views.
Orbit, 70 m surface, 18 m surface-low, and 2 m ground cameras separate broad
shape review from eye-level rendering and LOD review. The `backdrop` camera
uses the deterministic orbit stage and pitch/radius bounds above. The
`midground` camera retains its deterministic 1.6 km directional target. Small
bounded CPU sample grids are allowed for tests, statistics, and review metadata.
The old raw-field exporter remains with the hydrology lab; terrain v1 does not
emit a baked terrain product.

Headless surface and midground video advance the camera at a deterministic fixed
forward speed while re-querying terrain clearance every frame. A headless
backdrop video completes one full orbit over the requested capture duration;
stills remain fixed at the selected or requested initial azimuth. Interactive
backdrop control allows unrestricted yaw while clamping radius and elevation to
the validated mode envelope. PNG behavior is unchanged.

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

The source v2.1 checkpoint preserves v2 geometry exactly at 64 m and coarser
footprints. It moves only the 108 m and smaller detail-octave deviations outside
the nonlinear elevation profile, scales them by 0.5, and caps their additive
relief at 30 m. V1 remains the default; v2 and v2.1 remain mountain-only opt-in
sources. V2.1 uses a dedicated shader bundle so its additional source evaluator
does not increase legacy v1/v2 pipeline compilation. Generate its review pack
with:

```sh
projects/terrain/capture_source_v2_1_review.sh
```

The v3 renderer review additionally requires byte-identical tile/layered height
views, a bounded material-normal detail increase, a moving midground traversal,
and measured frame/memory evidence. The accepted pack records zero changed
height pixels, `1.2927x` material-normal Laplacian energy, a `23.8544 ms`
layered wall-frame interval at 960 x 540, and `73.25 MiB` device-local use. The
layered renderer remains opt-in and near-ground terrain remains unsupported.

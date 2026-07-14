# Terrain V1 Runtime

Date: 2026-07-12

Status: implemented v1 source plus opt-in quality and layered-midground
rendering checkpoints. The previous CPU patch and analytical landscape work is
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
a 2 m near cell, and about 16 km of outer radius. All levels use one origin
snapped to that finest grid. Ring overlap is an exact eleven parent cells so
patch spans retain their advertised power-of-two cell spacing. Transition
vertices collapse in `xz` while their source footprint moves toward the parent
grid; height-only snapping is not sufficient to close T-junctions. Every
fragment has one LOD owner, while a one-parent-cell raster guard and downward
boundary skirts cover residual rasterization gaps.

An opt-in mountain quality path keeps the same coverage and ownership model but
submits coarse quad patches to Vulkan tessellation. Shared-edge projected sizes
select power-of-two factors against a configurable pixel target; generated
vertex spacing becomes the source footprint. This path requires tessellation
support and is not the default. Source v2 independently extends only mountain's
detail spectrum, allowing renderer and source changes to be reviewed separately.

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

The deterministic camera planner searches a fixed world-space anchor/heading
set against the random-access source and seeds the traversable surface camera
with the selected pose. The production `backdrop` profile considers 3.2 km and
6.4 km targets. The `midground` review profile fixes the target at 1.6 km so
detail cannot be hidden by the backdrop distance floor. Their 150 m minimum AGL
can rise when final terrain would enter the lower frustum within 300 m; center
and corner rays retain a 10 m safety margin. They are general framing tools
across presets and seeds, not tables of authored landmarks. Headless stills
keep either frame fixed. A separate center/upper-frame test samples 15 rays
through 75% of target distance and admits at most two early terrain hits, which
prevents a near side wall from consuming the intended backdrop or midground
composition.

The far-field v1 product tightens that general backdrop study for mountain
source v2.1. It reserves a 200 m local camera zone, a directional 30-degree yaw
cone, and a 3.2 km minimum effective target distance at the reference 40-degree
lens. The planner owns natural source-region selection; terrain masking and
camera-relative deformation are not part of the contract. Midground and free
terrain traversal remain diagnostics.

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
- `terrain.presentation`: `standard` or opt-in `backdrop` material coverage.

The terrain app supports final surface, base/final height, slope, weathering
delta, LOD, neutral clay, direct visibility, aerial transmittance, vegetation
coverage, source/material normals, material weights, albedo, roughness, blend
height, cavity, and classification-normal views.
Orbit, 70 m surface, 18 m surface-low, and 2 m ground cameras separate broad
shape review from eye-level rendering and LOD review. The `backdrop` camera
adds deterministic source-aware framing with a 40-degree lens, a 150 m AGL
floor, and candidate-specific foreground clearance. The `midground` camera
reuses that contract at a deterministic 1.6 km target. Small
bounded CPU sample grids are allowed for tests, statistics, and review metadata.
The old raw-field exporter remains with the hydrology lab; terrain v1 does not
emit a baked terrain product.

Headless surface and midground video advance the camera at a deterministic fixed
forward speed while re-querying terrain clearance every frame. Orbit-camera
video keeps the existing automatic rotation. Headless backdrop video and all
stills remain static; interactive backdrop use retains surface traversal at the
selected planned AGL. PNG behavior is unchanged.

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

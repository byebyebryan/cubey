# Terrain V1 Runtime

Date: 2026-07-19

Status: radial-v1 is the default asset-free fixed-focus cached backdrop.
Raster-v1 is an opt-in product for explicit external heightfields and retains
the stronger natural-raster morphology under the same cached renderer. The
previous hard-cut renderer, control clipmap, and quality tessellation path
remain explicit historical review controls. Close detail, cache persistence,
streaming, and sub-millisecond runtime performance remain open.

## Goal

Terrain v1 is a deterministic planar heightfield source plus a cached,
fixed-focus backdrop product. It should provide credible far terrain for
rendering-engine stress and scene composition without paying procedural source
or process cost every frame.

The first product is deliberately narrower than a terrain simulator:

- one graduated mountain source for the backdrop product and a retained
  coherent parameter source with `mountain`, `upland`, and `plains` presets;
- matching source evidence and CPU point queries;
- optional bounded local weathering;
- an opt-in regular-heightfield asset and deterministic natural-stage adapter;
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

Radial-v1 uses the separate graduated mountain evaluator derived from the
accepted source study. It composes broad range, massif, ridge, peak, and detail
noise bands in world space, then applies the setup-time radial relief envelope.
Its calibrated height range is frozen by source report tests. It contains no
authored ridge line or patch-local feature placement and is not exposed as a
generic preset parameter surface.

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

The external-generator bakeoff began as a source study. Its accepted regular
field boundary is now available through explicit `raster-v1`: Cubey reads a
`cubey.terrain.heightfield.v1` manifest plus one little-endian row-major float
elevation file, validates finite coverage and seed identity, then loads the
field and builds filtered mips at setup. Python, model weights, climate output,
and generator execution are not runtime dependencies. Terrain Diffusion is one
offline producer rather than part of the asset contract. See
[`terrain-external-generator-bakeoff.md`](../notes/terrain-external-generator-bakeoff.md)
for the original study contract,
[`terrain-external-generator-bakeoff-review.md`](../notes/terrain-external-generator-bakeoff-review.md)
for its initial reference-only verdict, and
[`terrain-raster-backdrop-v1.md`](../notes/terrain-raster-backdrop-v1.md) for the
completed promotion.

The production renderer is the fixed-focus `backdrop` path. Selecting a
`backdrop` or `backdrop-stage` camera defaults to `radial-v1`. The profile
samples the graduated source once over a 32.768 km global polar field around a
500 m stage focus. A setup-time radial wrapper retains 8 percent source relief
inside a 6 km foreground footprint, restores broad structure over 1-24 km, and
restores detail over 5-30 km. Stride-3 render indices reduce the baked high
field to 607,200 render-triangle capacity across 48 sectors. Neighboring
sectors duplicate identical global boundary samples.

Height, geometry normals, rock/snow classification, and bounded ambient
visibility are cached at setup. A reduced far-field index set retains the high
field's local normals while bounding submitted geometry. Conservative azimuth
selection and frustum bounds cull sectors for every unrestricted yaw. The
non-tessellated runtime shader performs environment lighting and aerial
perspective only; it does not evaluate terrain source noise, weathering,
terrain-shadow marches, or material tiles.

The default continuous center makes the standalone product complete. Explicit
`consumer-owned` center mode omits that mesh so a scene can own its foreground.
Both modes use the same outer sectors and source field. Radial composition is
evaluated only during the bake; it adds no per-frame source sampling or
procedural shaping to the runtime shader. The historical hard-cut product is
available through explicit `hard-cut-v1` and retains its v2.1 source controls,
16.384 km domain, and narrower stage contract.

Raster-v1 reuses this cached renderer with deterministic natural placement over
the unchanged external field. Its continuous center uses a seam-matched radial
distribution through `3.2 km`, then the existing logarithmic outer field
through `16.384 km`. One global decimated angular partition is shared by the
center and 48 sectors, preventing stride-3 T-junctions. The profile samples
`2,657,280` source points and submits a `607,200`-triangle capacity; it does not
fall back to a procedural source when its explicit asset is missing or invalid.

The `control` clipmap and `quality` tessellation renderers below are retained
explicit experiments and regression controls. They are not terrain v1
acceptance paths.

The control renderer samples height in the vertex shader over a
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

The retained live renderers' opt-in backdrop presentation may derive distant vegetation coverage from
height, slope, broad landform context, and footprint-filtered coherent fields.
This is material coverage only: it cannot displace terrain, change CPU queries,
provide collision, or claim individual grass or tree geometry. Its supported
scene contract begins at roughly 300 m from the visible lower frame edge.
Close-range foliage remains a separate future rendering product.

The backdrop stage planner searches the random-access source for a local
360-degree orbit stage. It scores a bounded coarse grid, refines deterministic
shortlists, and fully evaluates 16 candidates over 24 azimuth sectors. The
selected source focus maps to local scene XZ without changing source equations,
geometry scale, or terrain shape. The cached product samples that translated
location at setup. Live control and quality displacement, weathering,
procedural materials, diagnostics, and heightfield shadows sample it during
rendering. Renderer ownership and camera coordinates remain local.

Detached mode is the far-field product. The consumer owns the inner 300 m and
the cached renderer owns no visible terrain before 3.2 km. The solver raises
the physical focus enough to keep lower-frame terrain at least 3.2 km away
throughout the supported 50-250 m radius and 0-30 degree elevation envelope.
Yaw is unrestricted.
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
- `terrain.render_path`: production `backdrop`, legacy `control`, or
  mountain-only `quality`;
- `terrain.backdrop_profile`: default `radial-v1`, external `raster-v1`, or
  historical `hard-cut-v1`;
- `terrain.heightfield`: required manifest or asset directory for `raster-v1`;
- `terrain.backdrop_center`: default `continuous` or `consumer-owned`;
- `terrain.backdrop_mesh_density`: hard-cut `low`, `medium`, or default `high`;
- `terrain.surface_detail`: `tile` or quality-only `layered`;
- `terrain.target_edge_px`: adaptive quality target from `2` through `16`;
- `terrain.weathering`: `off` or `local`;
- `terrain.weathering_strength`;
- existing terrain camera, cell-size, and vertical-scale controls;
- `terrain.backdrop_mode`: `detached` or `grounded`;
- optional `terrain.backdrop_azimuth_degrees`,
  `terrain.backdrop_orbit_radius_m`, and `terrain.backdrop_elevation_degrees`;
- `terrain.backdrop_minimum_visible_distance_m`, fixed to `6000` for radial-v1
  and `3200` for raster-v1;
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
The old raw-field exporter remains with the hydrology lab. Terrain v1 builds an
in-memory cached backdrop product but does not yet persist or stream it.

Headless surface and midground video advance the camera at a deterministic fixed
forward speed while re-querying terrain clearance every frame. A headless
backdrop video completes one full orbit over the requested capture duration;
stills remain fixed at the selected or requested initial azimuth. Interactive
backdrop control allows unrestricted yaw while clamping radius and elevation to
the selected profile envelope. Radial-v1 uses 100-1000 m and 0-30 degrees;
raster-v1 uses 50-250 m and 0-30 degrees; hard-cut-v1 retains its narrower
mode-specific bounds. PNG behavior is unchanged.

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
- rendering requires no per-frame CPU field generation or bulk artifacts;
- exact product/study parity holds over the maintained six-heading comparison;
- the product records 1440p terrain-pass mean, p50, and p95; the current
  checkpoint requires mean and p50 at or below `2 ms`, while `<1 ms` remains an
  open engine target.

The 2026-07-16 radial-v1 product pack recorded six exact product/study PNG
pairs, stride 3, 607,232 render-triangle capacity, 2,657,280 source samples,
`10,509 ms`
setup/first-frame, and `364,200 KiB` peak RSS. Its maintained 2560 x 1440
active-clock profile measured `1.677 ms` mean, `1.517 ms` p50, and `2.552 ms`
p95 for the terrain surface pass. Mean and p50 pass the current `2 ms`
checkpoint. Identical p95 profiles varied from about `1.35-3.7 ms` with GPU duty
cycle, so p95 remains tail telemetry until clock residency is controlled. See
[`terrain-radial-backdrop-product-v1.md`](../notes/terrain-radial-backdrop-product-v1.md).
The shared seam-safe index revision found during raster-v1 acceptance now uses
`607,200` render triangles for both profiles without changing source samples.

The raster-v1 product pack records three external fields at six unrestricted
headings, the complete camera envelope, stride-1 comparisons, and five
diagnostic views. Its exact product has stride 3, `607,200` render triangles,
and `2,657,280` source samples. At 2560 x 1440 it measured `1.073 ms` mean,
`1.025 ms` p50, and `1.299 ms` p95 terrain GPU time; the same run measured
radial-v1 at `1.418 ms` mean and `1.410 ms` p50, passing the relative 10 percent
gate. Setup plus first frame at 640 x 360 took `9,851 ms` and `533,096 KiB` peak
RSS. See
[`terrain-raster-backdrop-v1.md`](../notes/terrain-raster-backdrop-v1.md).

The subsequent fixed-control stride A/B compared the same cached source and
camera with stride 1, 2, and 3. Increasing visible submissions from 190,464 to
1,668,096 triangles changed focused final surface frames by only about `0.17%`
normalized RMSE at both the 100 m stress and 400 m product distances. Runtime
LOD is therefore not the immediate visual-quality dependency for radial-v1;
source relief and normal/material bandwidth own the current image limit. LOD
remains a future range, transition, and workload-distribution concern.

The historical hard-cut pack recorded `0.876288 ms` terrain-surface p95 and
remains useful as a performance/regression control. It is no longer the default
composition. See
[`terrain-cached-backdrop-v1-review.md`](../notes/terrain-cached-backdrop-v1-review.md).

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

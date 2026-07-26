# Terrain V1 Runtime

Date: 2026-07-22

Status: active raster heightfield far-backdrop product.

## Goal

Terrain V1 provides convincing far-field terrain for isolated rendering review
and scene composition. The product prioritizes stable macro shape,
bounded per-frame cost, deterministic evidence, and a narrow consumer contract.
It does not attempt close or hero terrain.

The active application is `projects/terrain`. The reference ports and
hydrology experiments under `studies/terrain` are evidence, not dependencies or
alternate runtime modes.

## Product Spine

```text
offline producer
    -> cubey.terrain.heightfield.v1
    -> validated raster source + filtered mip chain
    -> deterministic directional placement
    -> focused natural-stage plan
    -> cached continuous sector mesh
    -> procedural detail material + shared atmosphere/cloud environment
    -> shared GPU runtime
    -> terrain review app + opt-in glTF Viewer consumer
```

Terrain Diffusion is the canonical development producer, not the API. Any tool
may provide a compatible manifest and elevation payload.

## Foundation Ownership

Terrain V1 separates CPU terrain modeling from renderer and GPU ownership:

```text
cubey::asset
    -> validated terrain height sources
cubey::terrain
    -> placement, stage planning, surface classification, cached CPU products
cubey::engine
    -> terrain GPU generations, replacement, culling, shadows, composition
cubey::render + cubey::vulkan
    -> mesh resources, draw vocabulary, transfer and command ownership
```

`cubey::terrain` has no Vulkan dependency and does not publish renderer mesh
configuration. Its cached meshes use a semantic terrain vertex contract with
position, material channels, normal, and surface channels. The engine maps that
contract to the terrain shader input layout and owns all GPU resources.

The product fingerprint, source/material statistics, seam checks, and bounds
describe the fully sampled terrain field. A deterministic storage compaction may
discard vertices not referenced by the selected draw topology after those
contracts are evaluated. Compaction must not change placement, topology, bounds,
material statistics, fingerprints, camera behavior, or pixels.

## Asset Contract

`cubey.terrain.heightfield.v1` contains:

- source identity, seed, and optional generator/model provenance;
- regular grid dimensions, origin, and sample spacing in meters;
- height offset, scale, and relief metadata;
- one little-endian row-major float32 elevation payload;
- byte count, shape, layout, and SHA-256 metadata.

Loading validates schema, finite dimensions/transforms, safe relative paths,
declared and actual byte counts, finite elevation values, field coverage, and
the exact SHA-256 of the loaded elevation bytes. The runtime builds a filtered
CPU mip chain so geometric sample footprint can select an appropriate source
level. Production assets, Python dependencies, and model weights remain outside
Git and outside the renderer process. One small deterministic raster fixture is
tracked under `tests/assets/terrain` for executable integration smokes.

An optional `cubey.terrain.surface-fields.study.v1` companion carries the
heightfield SHA-256 binding and ordered temperature/precipitation channels.
Loading verifies the declared climate digest against the actual bytes before
that verified identity can enter a derived-product recipe. Finite-value and
physical-range checks reject corrupt companions without weakening the elevation
contract.

The default worktree source bundle is generated only by the explicit
`cubey_terrain_generate_default_asset` target. Ordinary configure, build, and
test have no network or generation side effects. Missing data is an error, not
a reason to switch source models.

### Worktree Source Cache

Terrain Diffusion outputs remain source assets rather than procedural-cache
entries. Their JSON manifests carry terrain-specific grid transforms, model and
code provenance, channel semantics, and SHA-256 binding that the generic
artifact header does not replace. Explicit generation stores these bundles
under the Git-ignored worktree path `cache/terrain/sources/v1/`, shared by all
build presets. Terrain Diffusion checkout, environment, and downloaded data
also live under `cache/terrain/tooling/v1/` so deleting a build directory does
not discard them or duplicate them between Debug and Release.

The generation targets remain deliberate operations. A complete matching
bundle is reused only after schema, provenance, grid, transform, channel,
shape/layout, byte-count, finite-value, physical-range, and payload-digest
validation. The dependency-free reuse tests run through CTest; the larger
NumPy-backed producer tests remain available through the pinned tool
environment. Missing, mismatched, or explicitly forced output regenerates
through the pinned producer.

These source directories are intentionally not CMake byproducts. `clean`
targets own build outputs, not persistent worktree source assets. Normal
configure, build, test, and application startup still perform no model download
or inference.

### Derived Product Cache

The validated source is an input to a distinct, implemented recipe-keyed
derived cache under `cache/procedural/v1/terrain.backdrop.product/`. A terrain
backdrop recipe includes the elevation SHA-256, optional climate SHA-256,
source seed, placement request and resolved focus, topology/profile and render
stride, surface-model formula, and product codec version. A hit restores the
compact CPU terrain product plus project climate diagnostics; a miss follows
the normal source sampling and product build, then publishes atomically through
the shared worktree procedural-artifact cache.

Only deterministic CPU products are persistent. Vulkan buffers, descriptors,
shadow state, transfer submissions, and retirement tickets remain owned by the
runtime and are rebuilt through the existing GPU installation and frame-boundary
activation path. Cache rejection or IO failure is nonfatal and falls back to
the uncached builder. The typed codec validates request enums and ranges,
source identity, density/topology, mesh indices and finite values, diagnostics,
auxiliary climate data, and the reconstructed recipe before activation.

Deleting `cache/procedural/v1/terrain.backdrop.product/` forces only derived
terrain-product rebuilding. It does not delete or rerun the Terrain Diffusion
producer. Deleting source bundles remains a separate deliberate operation under
`cache/terrain/sources/v1/`.

The canonical 2048 x 2048 seed-0 source produced a 25 MiB stride-3 product cache
entry on the validation workstation. Release product preparation fell from
406 ms generation plus 60 ms encode/store to 26 ms load plus 10 ms decode.
Including source loading and placement, the measured CPU phases fell from
494 ms cold to 62 ms warm. Debug CPU phases fell from 3.23 seconds cold to
519 ms warm. Cold and warm captures were byte-identical. The broader staged
`prepare_ms` value starts when the request is queued and is observed only after
host/GPU startup, so the per-phase metrics are the authoritative cache
measurement.

## Placement And Camera

The runtime searches a bounded regular grid around the geometric center of the
source bounds for a focus with:

- low local relief and slope around the subject;
- a coherent mountain arc in some directions;
- an open arc in other directions;
- gradual rise rather than a circular wall.

The local gate evaluates a 500 m radius around the subject and accepts at most
120 m of relief and a 0.275 P95 height gradient. That radius covers the 300 m
stage and clearance-qualified 250 m orbit with margin. The review app's wider
orbit remains an inspection stress control, not a reason to reject an otherwise
usable backdrop placement.

Selected search ranks candidates that satisfy both the local gate and the
mountain/open directional composition first. When a source has no such
mountain composition, as with rolling hills or lowland fields, it falls back to
the highest-scoring candidate that still passes the local gate. Directional
metrics remain visible as `best available`; only local safety, source coverage,
and focused-stage clearance are hard activation gates.

The selected source coordinate is only a translation into the heightfield. The
source remains continuous and unchanged. The focused stage maps a 500 m
clearance-qualified reference height to local zero. The review app may move the
foreground and orbit target below that reference without rebuilding or moving
the terrain; low views intentionally do not retain the clearance guarantee.

The product exposes runtime-selectable `selected`, `raw-center`, and indexed
`raw-sample` controls. Raw modes choose one coverage-safe coordinate without
scoring, retry, or rejection and retain the same directional metrics for
comparison. They are diagnostics, not alternate product defaults. A placement
change stages a complete cached-product rebuild on the job system, keeps the
active product visible, then uploads and atomically swaps only a successful
replacement. A runtime generation groups compact product metadata, meshes, the
seed-derived detail texture, and its material descriptors. The prior generation
remains alive through the latest actual GPU submission ticket instead of forcing
a queue- or device-idle stall.

The standalone review app can also stage a source change between its startup
field and any available generated climate-calibration region. Height and
climate manifests are loaded and binding-validated together on the background
build. The validated source, filtered mip chain, placement, cached product, and
GPU meshes become active as one successful replacement; failed source
validation, local placement safety, or stage clearance leaves the previous
product untouched. This is a review control over explicit generated evidence,
not a runtime biome generator or a terrain consumer API.

The V1 camera contract is:

- foreground and orbit focus 200 m above the placed terrain center by default,
  selectable at startup and adjustable from 2-1000 m on a logarithmic review
  slider;
- unrestricted yaw;
- live inspection orbit radius from 50 m through 1000 m, with baked clearance
  qualified through 250 m;
- unrestricted inspection elevation;
- 40 degree vertical field of view;
- optional 20 m foreground sphere for composition review.

The 500 m setting remains the safe far-field comparison. The 100 m lane is the
explicit close stress view; lower heights remain hero and surface diagnostics
intended to expose intersections, topology, source, and material weaknesses.
This camera is a product constraint, not a terrain LOD or traversal system.

## Geometry

The backdrop product samples the source once during startup into a cached polar
mesh:

- high-density profile: 3072 angular intervals and 48 culling sectors;
- continuous seam-matched center;
- visible far-field transition beginning at 3.2 km;
- outer radius 16.384 km;
- full center radial rings with angular and outer-terrain render stride 3;
- exact shared samples at sector and center boundaries;
- CPU frustum and azimuth culling before draw submission.

A reference-only startup diagnostic can rebuild this same product at render
stride 1, 2, or 3 for matched topology captures. It does not change source
sampling, add runtime selection, or constitute an LOD contract; stride 3
remains the product default.

The cached mesh avoids per-frame source evaluation, tessellation, clipmap
updates, and LOD planning. CPU source loading, mip construction, placement,
sampling, normal/material classification, and product-cache IO run in the
staged preparation job. A complete product then receives a bounded GPU mesh
upload and frame-boundary activation. General source streaming, partial
residency, and upload overlap remain separately deferred.

The fixed topology is accepted for this narrow camera envelope. Low-poly shape
or insufficient silhouette at the envelope endpoints is a source/topology
failure; framebuffer resolution alone is not a fix.

## Material And Lighting

Vertex material channels classify ground, rock, snow, and ambient visibility
from source height and geometric normal. The filtered-detail presentation adds
one deterministic `1024 x 1024` RGBA8 compute-generated periodic texture with
11 mips. Two terrain-planar samples provide a `32.768 km` macro field and a
`2.048 km` local field for bounded albedo, roughness, and normal variation.
Generated relief, mineral, and roughness fields use decorrelated seed domains.
Normalized height, slope, and the baked material channels turn those fields
into gradual lowland, upland, exposed-rock, and snow presentation.

Local normal response fades with projected footprint and on steep faces where
planar projection would stretch. The flat presentation bypasses those texture
samples in the same shader and remains the geometry/lighting control. Triplanar
rock projection was rejected because it added three texture projections
without a visible benefit inside the accepted camera envelope.

Both presentations use:

- classification normals from the cached source mesh;
- shared atmosphere-derived direct light and pi-normalized diffuse irradiance;
- shared physical aerial perspective between camera and terrain, blended at a
  bounded 20% product default with a live 0-100% review control;
- the shared atmosphere background with a running daytime solar clock;
- depth-aware shared Cloud V1 composition in the final surface view;
- the shared HDR post path.

When composed through Forward PBR, the terrain product also contributes a
bounded specular reflection proxy for foreground objects. Product material
averages, current atmosphere lighting, and a relief-derived horizon produce a
lower-hemisphere radiance contribution without rendering the full terrain six
times into a reflection cube. Consumers may disable the contribution for A/B
review. It grounds reflective backdrop scenes, but does not claim reflected
peak silhouettes or local parallax.

Snow retains its daytime response and receives a smooth nocturnal damping as
the sun crosses below the horizon. This preserves snow/rock separation under
moonlight without letting high-albedo caps read as emissive under automatic
night exposure.

Filtered-detail lighting evaluates broad diffuse irradiance from the
classification normal while reserving the generated normal primarily for
direct and raking-light response. Material-aware ambient visibility deepens
existing cavities without changing the shared atmosphere, exposure, or aerial
perspective contracts.

Directional terrain shadows use one cached `2048 x 2048` full-product depth
map. A comparison sampler evaluates four bilinear taps at half-texel offsets,
which preserves the separable `1 / 2 / 1` tent footprint while avoiding nine
explicit depth fetches. Receivers apply a slope-aware depth bias plus a
shadow-texel-scaled geometric-normal offset; this prevents the polar terrain
triangles from self-shadowing while retaining larger valley occlusion. The map
reaches full contribution above roughly `18` degrees of solar elevation and
smoothly yields to unshadowed direct light below that point. This keeps sunset
color and directional lighting while preventing the fixed far-field shadow
texels from becoming visible under grazing projection. The map refreshes when
the light direction changes by `0.5` degree; below-horizon light suspends
updates.

The detail texture improves material frequency but does not add geometry or
claim grass, trees, scree, exposed strata, or close-surface fidelity.
The accepted matched visual closure is recorded in
[Terrain V1 Visual Closure](../notes/terrain-visual-closure-v1.md).

## Diagnostics

The product keeps diagnostics that directly inspect its supported contracts:

- height and slope;
- clay and geometric/classification normal;
- material weights, albedo, detail normal, roughness, and ambient visibility;
- projected triangle span;
- stage ownership boundary.

The review UI and profile output also publish placement mode/index, source
coordinate, directional contract and score, local evaluation radius and limits,
measured relief and P95 slope, mountain and open arcs, and baked clearance. The
UI separates staged placement controls and rebuild status from the active
product metadata so an in-flight or failed replacement cannot be mistaken for
the rendered terrain.

Retired procedural source bands, weathering, tessellation factors, clipmap LOD,
vegetation, and shadow placeholders are not product diagnostics.

## Runtime Boundary

The accepted backdrop is promoted through glTF Viewer as its first real
external consumer. Shared asset code validates and owns the height source while
building. Shared terrain code builds placement and the cached CPU product. The
engine runtime copies the request, diagnostics, source metadata, and section
bounds it needs, then owns one complete GPU generation, culling, terrain
self-shadowing, and optional Forward PBR composition. The producer, source, and
full CPU product may be discarded after runtime creation. Product replacement
builds a complete next generation before changing active state, rejects
mid-frame swaps, and retires the prior generation after its last actual GPU
submission completes.
Consumers keep stage composition, camera, world translation,
atmosphere/cloud/HDR policy, UI, and source-build scheduling.

The standalone terrain app also uses the shared
`AtmosphereBackgroundAtlasRuntime`. Windowed startup publishes placeholder
textures before cached lunar/night-sky preparation and activates the complete
GPU pair at an update boundary. Headless capture finishes that same request
before frame zero. This removes atmosphere generation from terrain's
first-present critical path without moving atmosphere policy into the terrain
runtime.

The shared surface boundary bakes generic material channels. Mineral control is
the production default. Climate rasters, climate-response formulas, calibration
labels, and their diagnostics remain terrain-project experiments rather than
foundation semantics. Planet terrain remains a separate spherical scale/LOD
problem.

## Explicit Deferrals

- close terrain and free traversal;
- clipmaps, adaptive tessellation, and projected-error LOD;
- streaming, partial residency, and floating origin;
- hydrology, erosion simulation, rivers, lakes, and coasts;
- biome/climate products and foliage placement;
- deformation, collision, and gameplay queries;
- planet projection and ocean composition;
- cross-shadowing between backdrop terrain and foreground scene geometry.
- direct generation into compact storage, mesh streaming, and upload overlap;
- GPU buffer suballocation, persistent staging arenas, and asynchronous transfer
  queues.

The reference-only
[Terrain Surface Semantics Study](../notes/terrain-surface-semantics-study.md)
may evaluate an optional climate companion and continuous surface masks without
changing this product contract or its default presentation. The follow-up
[Terrain Climate Surface Model Research](../notes/terrain-climate-surface-model-research.md)
defines the narrower climate-potential contract. The completed
[Terrain Climate Calibration V1](../notes/terrain-climate-calibration-v1.md)
records the five-region evidence and keeps the production default unchanged.
The broader climate-response candidate was rejected and retained only as
[archived evidence](../archive/terrain/climate-response-v1-1-rejected.md).

## Acceptance

Terrain V1 review includes the canonical source hash and provenance, four
headings, camera-envelope endpoints, clean and foreground composition, flat and
filtered-detail comparison, neutral and raking lighting, and the retained
diagnostics. A separate placement-control pack compares the selected result
against raw center and raw indexed locations at matched headings and 100/500 m
focus heights. The current rendering acceptance adds the 200 m product default
between those controls. The shape and placement contract must remain identical
across material modes.

The current macro shape is accepted for far-field use. The rendering-acceptance
batch refines center topology, directional-shadow sampling, and filtered snow
response without reopening the source or placement model. Performance is
tracked by mean and p50 for the accepted view; p95 remains diagnostic until
capture and startup noise are separated from steady-state rendering.

The rendering-envelope gate compared the default stride-3 product with the
same cached source rendered at stride 1. Full topology increased the complete
draw product from the earlier `607,200` to `5,305,344` triangles without
materially changing qualified silhouettes. Stride 3 therefore remains the V1
outer topology; the center-topology correction later raises the default to
`742,368` triangles without changing outer-terrain stride. Material refinement
still precedes LOD or further source work. The accepted shadowed composition
measured `0.897 ms` mean and `0.893 ms` p50 at `1600 x 900`; the default moving
clock measured `1.012 ms` mean and `0.925 ms` p50.

The cached product retains only that selected draw topology. Full-resolution
sample and triangle budgets remain diagnostic values; they are not materialized
as a second unused index buffer in either CPU or GPU product state.
The canonical stride-3 product compacts `2,694,289` sampled vertices to
`385,201` referenced render vertices while preserving `742,368` rendered
triangles. Its center and sector meshes total `25,857,260` bytes and upload as
one renderer mesh batch and one Vulkan transfer submission. The transfer path
uses bounded staging chunks, so larger products retain the same API without an
unbounded staging allocation.

The glTF proof consumer keeps terrain explicitly opt-in and preserves its
no-terrain control path. At `1600 x 900`, terrain adds `0.423 ms` mean and
`0.425 ms` p50 to the shared Forward PBR scene pass, below the `0.75 ms`
promotion gate. This establishes a reusable far-backdrop product, not close
terrain, streaming LOD, or a general terrain engine.

Normal test runs exercise both real consumers with the tracked deterministic
raster fixture. Each headless smoke must load and validate the payload, select
placement, create the shared runtime, render a PNG, and pass nonblank image
statistics; no network or model generation is involved.

See [Terrain Product Promotion](../notes/terrain-product-promotion.md),
[Terrain Rendering Envelope V1](../notes/terrain-rendering-envelope-v1.md),
[Terrain Rendering Acceptance V1](../notes/terrain-rendering-acceptance-v1.md),
[Terrain Project Map](../notes/terrain-project-map.md), and the project
[README](../../projects/terrain/README.md).

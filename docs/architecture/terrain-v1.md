# Terrain V1 Runtime

Date: 2026-07-19

Status: active raster heightfield far-backdrop product.

## Goal

Terrain V1 provides convincing far-field terrain for isolated rendering review
and eventual scene composition. The product prioritizes stable macro shape,
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
    -> standalone review app
```

Terrain Diffusion is the canonical development producer, not the API. Any tool
may provide a compatible manifest and elevation payload.

## Asset Contract

`cubey.terrain.heightfield.v1` contains:

- source identity, seed, and optional generator/model provenance;
- regular grid dimensions, origin, and sample spacing in meters;
- height offset, scale, and relief metadata;
- one little-endian row-major float32 elevation payload;
- byte count, shape, layout, and SHA-256 metadata.

Loading validates schema, finite dimensions/transforms, safe relative paths,
declared and actual byte counts, finite elevation values, and field coverage.
The runtime builds a filtered CPU mip chain so geometric sample footprint can
select an appropriate source level. Generated assets, Python dependencies, and
model weights remain outside Git and outside the renderer process.

The default build-tree asset is generated only by the explicit
`cubey_terrain_generate_default_asset` target. Ordinary configure, build, and
test have no network or generation side effects. Missing data is an error, not
a reason to switch source models.

## Placement And Camera

The runtime searches a bounded regular grid for a focus with:

- low local relief and slope around the subject;
- a coherent mountain arc in some directions;
- an open arc in other directions;
- gradual rise rather than a circular wall.

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
replacement. Source loading remains a startup operation; runtime placement
reuses the validated source and filtered mip chain.

The V1 camera contract is:

- foreground and orbit focus 100 m above the placed terrain center by default,
  selectable at startup and adjustable from 2-1000 m on a logarithmic review
  slider;
- unrestricted yaw;
- live inspection orbit radius from 50 m through 1000 m, with baked clearance
  qualified through 250 m;
- unrestricted inspection elevation;
- 40 degree vertical field of view;
- optional 20 m foreground sphere for composition review.

The 500 m setting remains the safe far-field comparison. Heights from 2-100 m
are hero and surface stress views intended to expose intersections, topology,
source, and material weaknesses. This camera is a product constraint, not a
terrain LOD or traversal system.

## Geometry

The backdrop product samples the source once during startup into a cached polar
mesh:

- high-density profile: 3072 angular intervals and 48 culling sectors;
- continuous seam-matched center;
- visible far-field transition beginning at 3.2 km;
- outer radius 16.384 km;
- fixed render stride 3;
- exact shared samples at sector and center boundaries;
- CPU frustum and azimuth culling before draw submission.

A reference-only startup diagnostic can rebuild this same product at render
stride 1, 2, or 3 for matched topology captures. It does not change source
sampling, add runtime selection, or constitute an LOD contract; stride 3
remains the product default.

The cached mesh avoids per-frame source evaluation, tessellation, clipmap
updates, and LOD planning. Startup currently includes source loading, mip
construction, placement, sampling, normal/material classification, and GPU mesh
upload. Persistence and asynchronous streaming are deferred.

The fixed topology is accepted for this narrow camera envelope. Low-poly shape
or insufficient silhouette at the envelope endpoints is a source/topology
failure; framebuffer resolution alone is not a fix.

## Material And Lighting

Vertex material channels classify ground, rock, snow, and ambient visibility
from source height and geometric normal. The filtered-detail presentation adds
one compute-generated periodic texture sampled triplanarly for bounded albedo,
roughness, and normal variation. The flat presentation bypasses those texture
samples in the same shader and serves as the geometry/lighting control.

Both presentations use:

- classification normals from the cached source mesh;
- shared atmosphere-derived direct light and diffuse irradiance;
- physical aerial perspective between camera and terrain;
- the shared atmosphere background with a running daytime solar clock;
- depth-aware shared Cloud V1 composition in the final surface view;
- the shared HDR post path.

The detail texture improves material frequency but does not add geometry or
claim grass, trees, scree, exposed strata, or close-surface fidelity.

## Diagnostics

The product keeps diagnostics that directly inspect its supported contracts:

- height and slope;
- clay and geometric/classification normal;
- material weights, albedo, detail normal, roughness, and ambient visibility;
- projected triangle span;
- stage ownership boundary.

The review UI and profile output also publish placement mode/index, source
coordinate, directional contract and score, local relief, p95 slope, mountain
and open arcs, and baked clearance. The UI separates staged placement controls
and rebuild status from the active product metadata so an in-flight or failed
replacement cannot be mistaken for the rendered terrain.

Retired procedural source bands, weathering, tessellation factors, clipmap LOD,
vegetation, and shadow placeholders are not product diagnostics.

## Runtime Boundary

The implementation remains project-local. This avoids promoting a one-consumer
API before its ownership and packaging are tested. The eventual reusable
backdrop would likely own an immutable height source, cached geometry product,
draw plan, material resources, and scene-facing placement metadata, while a
consumer would own stage composition and camera.

glTF Viewer is a plausible second consumer, but integration is deferred until
the isolated material and terrain-light response are convincing. Planet terrain
is a separate spherical scale/LOD problem and is not this product's second
consumer.

## Explicit Deferrals

- close terrain and free traversal;
- clipmaps, adaptive tessellation, and projected-error LOD;
- streaming, residency, floating origin, and cache persistence;
- hydrology, erosion simulation, rivers, lakes, and coasts;
- biome/climate products and foliage placement;
- deformation, collision, and gameplay queries;
- planet projection and ocean composition;
- engine/foundation promotion.

The reference-only
[Terrain Surface Semantics Study](../notes/terrain-surface-semantics-study.md)
may evaluate an optional climate companion and continuous surface masks without
changing this product contract or its default presentation. The follow-up
[Terrain Climate Surface Model Research](../notes/terrain-climate-surface-model-research.md)
defines the narrower climate-potential contract and the cross-climate evidence
required before promotion.

## Acceptance

Terrain V1 review includes the canonical source hash and provenance, four
headings, camera-envelope endpoints, clean and foreground composition, flat and
filtered-detail comparison, neutral and raking lighting, and the retained
diagnostics. A separate placement-control pack compares the selected result
against raw center and raw indexed locations at matched headings and 100/500 m
focus heights. The shape and placement contract must remain identical across
material modes.

The current macro shape is accepted for far-field use. Material fidelity and
terrain-light response are the next isolated refinement batch. Performance is
tracked by mean and p50 for the accepted view; p95 remains diagnostic until
capture and startup noise are separated from steady-state rendering.

The rendering-envelope gate compared the default stride-3 product with the
same cached source rendered at stride 1. Full topology increased the complete
draw product from `607,200` to `5,305,344` triangles without materially
changing qualified silhouettes. Stride 3 therefore remains the V1 topology;
Material V2 precedes LOD or further source work. The fixed clear composition
measured `0.996 ms` mean and `0.921 ms` p50 at `1600 x 900`.

See [Terrain Product Promotion](../notes/terrain-product-promotion.md),
[Terrain Rendering Envelope V1](../notes/terrain-rendering-envelope-v1.md),
[Terrain Project Map](../notes/terrain-project-map.md), and the project
[README](../../projects/terrain/README.md).

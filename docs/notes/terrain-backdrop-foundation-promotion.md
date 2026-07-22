# Terrain Backdrop Foundation Promotion

Date: 2026-07-21

Status: implementation plan.

## Decision

Promote the accepted fixed-focus far-field terrain backdrop through one real
second consumer. The shared contract owns raster height loading, deterministic
placement, the cached continuous mesh product, culling, procedural detail,
terrain self-shadowing, and draw recording. It does not become a general
terrain engine.

The terrain review app remains the product laboratory. glTF Viewer is the
second consumer and enables the backdrop only when an explicit terrain
heightfield is supplied. The viewer owns its model-centered orbit, foreground
composition, world placement, atmosphere/cloud selection, and UI. A missing
terrain option leaves its existing resource, camera, graph, and rendering path
unchanged.

## Foundation Boundary

- `cubey::asset` owns the immutable height-source interface and validated
  `cubey.terrain.heightfield.v1` raster loader with provenance.
- `cubey::render` owns placement and stage plans, cached mesh products, the
  default mineral surface classifier, culling plans, and terrain-shadow math.
- `cubey::engine` owns the GPU backdrop runtime and its composition hook in the
  forward PBR renderer.
- Consumers own cameras, scene entities, source selection policy, async build
  scheduling, world translation, atmosphere/cloud/HDR composition, UI, and
  capture policy.

The shared product accepts a narrow surface-classification interface and bakes
the returned channels into vertices. The default classifier is the accepted
mineral-control response. Imported climate fields, climate formulas, calibration
labels, source-choice UI, and climate diagnostics remain terrain-project code.
The rejected climate-response V1.1 branch is not part of this promotion.

## Composition Contract

The forward renderer records one HDR scene in this order: atmosphere or
skybox, terrain backdrop, opaque and alpha foreground geometry, depth-aware
cloud composition, and one display transform. Terrain and foreground geometry
share scene depth. Terrain keeps a separate cached full-product shadow map;
terrain and foreground objects do not cast shadows onto each other in this
version.

The cached product retains its accepted 500 m stage reference. A consumer may
translate the complete product so its focus is any supported height above the
local terrain without rebuilding or changing product hashes. Culling, shadow
projection, world positions, material sampling, and aerial perspective all use
that translation.

## glTF Proof Consumer

`--terrain-heightfield` enables the integration. The default is selected
placement, continuous high-density stride-3 geometry, mineral control,
filtered detail, terrain shadows, and a 200 m foreground altitude. Viewer UI
may hide the backdrop or change altitude, detail presentation, and shadows.
Camera orbit remains model-centered with unrestricted yaw and pitch; terrain
only raises the near plane to 0.1 m and extends the far plane to cover the
16.384 km product.

The first integration requires the atmosphere environment because the accepted
terrain material depends on atmosphere lighting and aerial perspective. Static
IBL plus terrain is rejected explicitly instead of introducing a second,
unvalidated terrain lighting path.

## Non-Goals

- terrain generation or Terrain Diffusion inference in normal builds;
- climate or biome foundation APIs;
- close terrain, traversal, collision, deformation, or gameplay queries;
- clipmaps, streaming, floating origin, or adaptive LOD;
- hydrology, erosion simulation, water, foliage, or planet projection;
- a generic render callback or public render-graph extension framework.

## Acceptance

The terrain app must retain accepted source/product hashes, topology, captures,
and its 1.10 ms mean/p50 gate at 1600 x 900. The glTF proof must preserve the
no-terrain control path, share scene depth correctly, remain continuous across
unrestricted headings, and add no more than 0.75 ms mean or p50 steady GPU time
against a matched viewer control. P95 and forced shadow updates remain
diagnostic.

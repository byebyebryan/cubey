# Terrain Backdrop Foundation Promotion

Date: 2026-07-24

Status: implemented and validated across glTF Viewer, Water3D, Fire3D, and
Explosion3D.

## Decision

Promote the accepted fixed-focus far-field terrain backdrop through several
materially different consumers. The shared contract owns validated raster
height loading, deterministic placement and product preparation, the continuous
mesh product, culling, procedural detail, terrain self-shadowing, and draw
recording. It does not become a general terrain engine.

The terrain review app remains the product laboratory. glTF Viewer proves
forward-PBR composition, Water3D proves a custom refractive graph, and the
shared Pyro3D renderer proves depth-aware volume composition for Fire3D and
Explosion3D. Each enables the backdrop only when an explicit terrain
heightfield is supplied and retains ownership of its camera, foreground,
atmosphere/cloud policy, UI, and capture behavior.

## Foundation Boundary

- `cubey::asset` owns the immutable height-source interface and validated
  `cubey.terrain.heightfield.v1` raster loader with exact payload provenance.
- `cubey::terrain` owns placement and stage plans, raster-to-product
  preparation, cached mesh products, and the default mineral surface
  classifier.
- `cubey::render` owns presentation policy, culling plans, and terrain-shadow
  math.
- `cubey::engine` owns the GPU backdrop runtime and its composition hook in the
  forward PBR renderer.
- Consumers own cameras, scene entities, source selection policy, async build
  scheduling, world translation, atmosphere/cloud/HDR composition, UI, and
  capture policy.

The runtime copies the compact request, diagnostics, metadata, and section
bounds required after upload; it does not borrow the source or full CPU product.
A caller may discard those producer objects after runtime creation. Successful
replacement retires prior GPU meshes through the latest frame submission ticket
instead of stalling the queue or device.

The shared product accepts a narrow surface-classification interface and bakes
the returned channels into vertices. The default classifier is the accepted
mineral-control response. Imported climate fields, climate formulas, calibration
labels, source-choice UI, and climate diagnostics remain terrain-project code.
The rejected climate-response V1.1 branch is not part of this promotion.

## Composition Contract

All promoted consumers render terrain into linear HDR color and scene depth
before their final display transform:

- Forward PBR records atmosphere or skybox, terrain, opaque and alpha
  foreground geometry, depth-aware clouds, and one display transform.
- Water3D records atmosphere, moon, and terrain before the screen-space water
  passes. Water refraction and occlusion consume the resulting color and depth;
  clouds remain the final depth-aware scene layer. It renders no project-owned
  floor.
- Pyro3D records atmosphere, moon, and terrain before the volume pass. The
  raymarch samples linear scene color, reconstructs scene distance from depth,
  stops at opaque terrain, composites the volume, and applies one display
  transform.

Terrain keeps a separate cached full-product shadow map. Terrain and foreground
objects do not cast shadows onto each other in this version.

The cached product retains its accepted 500 m stage reference. A consumer may
translate the complete product so its focus is any supported height above the
local terrain without rebuilding or changing product hashes. Culling, shadow
projection, world positions, material sampling, and aerial perspective all use
that translation.

## Consumer Proofs

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

Water3D uses the same `--terrain-heightfield` option and defaults to a 5 m
foreground altitude. Its simulation remains project-owned; terrain is an
optional scene backdrop and does not alter liquid boundaries, collision, or
reflection capture.

Fire3D and Explosion3D share the Pyro3D integration and default to a 0.60 m
terrain-to-volume-center distance: the normalized volume's 0.5 m half-height
plus 0.10 m of clearance. Terrain is shown only for the normal smoke
presentation, not density or velocity diagnostics. The volume does not
illuminate or shadow terrain, and the integration does not change the
underlying fire or explosion model.

## Non-Goals

- terrain generation or Terrain Diffusion inference in normal builds;
- climate or biome foundation APIs;
- close terrain, traversal, collision, deformation, or gameplay queries;
- clipmaps, streaming, floating origin, or adaptive LOD;
- hydrology, erosion simulation, water, foliage, or planet projection;
- terrain-driven water simulation or reciprocal fire/terrain lighting;
- a generic render callback or public render-graph extension framework.

## Acceptance

The terrain app must retain accepted source/product hashes, topology, captures,
and its 1.10 ms mean/p50 gate at 1600 x 900. The glTF proof must preserve the
no-terrain control path, share scene depth correctly, remain continuous across
unrestricted headings, and add no more than 0.75 ms mean or p50 steady GPU time
against a matched viewer control. P95 and forced shadow updates remain
diagnostic.

The wider custom-renderer promotion reuses that measured terrain runtime but
does not claim an isolated Water3D or Pyro3D steady-state increment. Their
existing headless profile paths emphasize simulation rather than final
presentation. A dedicated offscreen presentation benchmark should precede any
per-consumer backdrop budget or optimization work.

## Implementation

- The raster height-source API and loader live in `cubey::asset`.
- Placement, stage planning, raster product preparation, mesh products, and
  material classification live in `cubey::terrain`.
- Culling, presentation, and shadow planning live in `cubey::render`.
- `TerrainBackdropRuntime` owns the shared GPU resources and recording path.
- `ForwardPbrRenderer3D` composes an optional terrain backdrop into its HDR
  scene target before foreground PBR geometry.
- Water3D and Pyro3D compose the same runtime through their existing render
  graphs instead of introducing renderer-specific terrain copies.
- `prepare_raster_terrain_backdrop_product` centralizes source validation,
  placement, v1 product generation, and the baked foreground offset. Consumers
  retain runtime and application policy.

The glTF integration deliberately requires an externally supplied validated
heightfield. Normal builds and portable smoke tests do not fetch or generate
terrain data. A small deterministic tracked fixture drives real terrain and
glTF headless smokes; the no-terrain viewer path remains the default.

## Validation

The default glTF atmosphere and static-environment PNG smokes pass without a
terrain asset. An explicit terrain run at `1600 x 900` composes the fallback
cube over continuous selected-placement terrain with shared scene depth. A
static PBR environment plus terrain is rejected with the documented error.

A matched 180-frame, 30-frame-warmup, cloud-disabled video profile on the RTX
5070 Ti measured the Forward PBR `scene` pass as follows:

| Case | Mean | P50 | P95 |
| --- | ---: | ---: | ---: |
| Viewer control | 0.411 ms | 0.411 ms | 0.417 ms |
| Viewer + terrain | 0.835 ms | 0.836 ms | 0.853 ms |
| Increment | 0.423 ms | 0.425 ms | 0.436 ms |

The steady-state mean and p50 increments pass the `0.75 ms` integration gate.
The cached terrain shadow update occurs before the measured steady-state
window; forced updates remain diagnostic as planned.

The executable integration smokes load and hash-check the tracked raster,
select placement relative to its translated source bounds, create the shared
runtime, render each real consumer, and apply nonblank PNG statistics. This
closes the normal-test gap left by the generated canonical asset.

The 2026-07-24 wider-consumer gate adds real headless terrain captures for
Water3D and Fire3D, verifies the shared Pyro3D path with Explosion3D, and keeps
all no-terrain PNG and video smokes green. The focused raster-preparation test
uses the tracked source fixture and verifies placement, source identity, render
stride, and foreground-offset propagation.

The foundation cleanup retains the same product and pixels while compacting
the canonical CPU/GPU mesh from `2,694,289` sampled vertices to `385,201`
referenced vertices. The resulting `25,857,260`-byte center/sector payload is
validated and uploaded transactionally in one transfer submission. glTF source
meshes use the same renderer batch contract for their primitive payloads.

The compact tracked
[foundation evidence pack](../evidence/terrain-backdrop-foundation/README.md)
retains reviewable terrain and glTF frames from the canonical source together
with source, revision, output-hash, and image-stat metadata.

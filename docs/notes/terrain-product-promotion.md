# Terrain Product Promotion

Date: 2026-07-19

Status: implemented and validated.

## Decision

`projects/terrain` is the only active terrain application. The accepted product
is the fixed-focus far-field backdrop backed by a
`cubey.terrain.heightfield.v1` asset. The previous procedural source, radial
composition, clipmap, tessellation, weathering, and source-model lanes are
closed experiments rather than alternate product modes.

The product keeps:

- deterministic natural placement over an unchanged regular heightfield;
- the continuous seam-matched center and cached sector renderer;
- the clearance-qualified 500 m stage reference, unrestricted yaw, and accepted
  camera envelope;
- shared atmosphere, HDR composition, diagnostics, and the foreground sphere;
- flat and filtered-detail material presentations for controlled comparison.

The product does not claim close terrain, traversal, streaming, hydrology,
vegetation, water, planet projection, or external-consumer integration. Those
boundaries remain explicit while material fidelity is improved in isolation.

Post-promotion review makes 100 m the default foreground height and exposes a
2-1000 m logarithmic UI range. The 500 m reference remains available; lower
heights are deliberate stress views rather than a new close-terrain claim.

A later placement-control checkpoint retains selected placement as the default
and adds runtime-selectable raw center and deterministic indexed raw samples.
These controls never modify the source or retry failed composition. Runtime
changes rebuild a cached product asynchronously and replace the rendered
product only after a successful upload. The canonical comparison shows the
selected low-relief focus is materially more stable across unrestricted yaw,
so no synthetic clearing or prepared-stage transition is promoted.

## Asset Boundary

Generated terrain data and model weights remain outside Git. A deliberate
build target generates the canonical seed-0 field locally from pinned Terrain
Diffusion source and model revisions. Normal configure, build, and test do not
download or generate terrain data.

The canonical setup emits only a 2048 x 2048 float elevation field and its
runtime manifest. Climate data, preview images, multi-seed bakeoff products,
and research reports are not runtime assets. The app accepts any compatible
manifest through an explicit path and otherwise uses the canonical build-tree
location. Missing data is a startup error with the exact generation command;
there is no fallback to a retired source.

Generate the canonical field explicitly with:

```sh
cmake --build --preset dev --target cubey_terrain_generate_default_asset
```

The target uses a caller-provided `CUBEY_TERRAIN_DIFFUSION_ROOT` when set.
Otherwise it creates a pinned checkout and Python environment under
`build/dev/_deps`. The generated `heightfield.json` and `elevation.f32` live in
`build/dev/assets/terrain/default`; normal builds and tests never invoke this
target.

## Repository Boundary

The retained visual-reference, external-ShaderToy, and hydrology programs move
under `studies/terrain` and build only when
`CUBEY_BUILD_TERRAIN_STUDIES=ON`. The old workbench, terrain lab, and coastal
procedural-terrain implementations are removed after their durable conclusions
and terrain-ocean vocabulary are preserved in documentation. Git history
remains the implementation archive.

`projects/planet` remains a separate planet-scale renderer. Its local terrain
field is not an alternate implementation of the terrain backdrop product.

## Application Contract

The windowed terrain app is a product review surface rather than a capture-only
harness. Its control panel exposes:

- source identity, dimensions, spacing, and provenance;
- staged placement mode/index controls and active source coordinate, score,
  relief, slope, arcs, camera-clearance evidence, and rebuild status;
- orbit radius, elevation, reset, and foreground-sphere visibility;
- flat versus filtered-detail material presentation;
- raster-supported diagnostic views;
- shared atmosphere controls, open by default;
- submitted geometry and GPU timings.

Retired source, profile, weathering, LOD, and tessellation controls must not
remain disabled in the product UI. Expensive source loading stays a startup
operation. Runtime placement resamples the already-loaded source through the
job system; it is cached-product replacement, not terrain streaming.

## Closure Gate

Promotion requires default and studies-enabled builds, focused and full tests,
the canonical seed-0 capture matrix, and interactive inspection. The accepted
height samples, stage placement, topology, and silhouette must remain unchanged
from raster-v1. Flat material performance is compared with the existing raster
checkpoint, while filtered-detail cost is reported separately rather than used
as a promotion blocker.

After promotion, the next terrain batch is material and terrain-light-response
refinement. Consumer integration remains a later foundation-promotion gate.

## Closure Evidence

The promoted application was validated on the canonical seed-0 field with
elevation SHA-256
`27b49f12f29ae24629a8ec03d12b53c6986404c0354069529be75a5ea02c45df`.
The curated `outputs/terrain/product-v1` pack contains 19 captures at 1600 x
900: final and foreground composition, flat/detail, raking light, four
headings, three camera-envelope endpoints, and eight diagnostics. The previous
ignored terrain output dump was removed.

Focused terrain tests and the shared core suite pass in both default and
studies-enabled configurations. The complete studies-enabled CTest matrix
reported 100% across 176 tests; 22 windowed smoke tests were expected skips
under SSH. A separate one-frame Wayland windowed smoke reached the ready state
and closed cleanly, but remote execution prevented hands-on GUI input inspection
in this batch.

On the NVIDIA GeForce RTX 5070 Ti, a 90-frame 1600 x 900 filtered-detail run
with the foreground sphere measured these steady GPU pass medians: atmosphere
0.402 ms, terrain 0.448 ms, sphere 0.080 ms, and post 0.013 ms. Their sum is
0.943 ms; the sum of pass means is 0.941 ms. Startup remains approximately 7.6
seconds because raster loading, mip construction, placement, and the 2.66
million source-sample mesh bake are synchronous. Startup persistence and
asynchrony remain explicit later work rather than V1 blockers.

# Terrain Product Promotion

Date: 2026-07-19

Status: accepted implementation direction.

## Decision

`projects/terrain` is the only active terrain application. The accepted product
is the fixed-focus far-field backdrop backed by a
`cubey.terrain.heightfield.v1` asset. The previous procedural source, radial
composition, clipmap, tessellation, weathering, and source-model lanes are
closed experiments rather than alternate product modes.

The product keeps:

- deterministic natural placement over an unchanged regular heightfield;
- the continuous seam-matched center and cached sector renderer;
- the 500 m mid-air focus, unrestricted yaw, and accepted camera envelope;
- shared atmosphere, HDR composition, diagnostics, and the foreground sphere;
- flat and filtered-detail material presentations for controlled comparison.

The product does not claim close terrain, traversal, streaming, hydrology,
vegetation, water, planet projection, or external-consumer integration. Those
boundaries remain explicit while material fidelity is improved in isolation.

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
- orbit radius, elevation, reset, and foreground-sphere visibility;
- flat versus filtered-detail material presentation;
- raster-supported diagnostic views;
- shared atmosphere controls, open by default;
- submitted geometry and GPU timings.

Retired source, profile, weathering, LOD, and tessellation controls must not
remain disabled in the product UI. Expensive source loading stays a startup
operation; this batch does not add asynchronous terrain streaming.

## Closure Gate

Promotion requires default and studies-enabled builds, focused and full tests,
the canonical seed-0 capture matrix, and interactive inspection. The accepted
height samples, stage placement, topology, and silhouette must remain unchanged
from raster-v1. Flat material performance is compared with the existing raster
checkpoint, while filtered-detail cost is reported separately rather than used
as a promotion blocker.

After promotion, the next terrain batch is material and terrain-light-response
refinement. Consumer integration remains a later foundation-promotion gate.

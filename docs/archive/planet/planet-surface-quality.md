# Planet Surface Quality Pass

This note records the planet surface quality batch that has since landed. The
goal was to make the procedural planet surface read less like placeholder bands
and more like a useful foundation for later ocean, clouds, and streamed terrain.
Current terrain-field status now lives in
[`planet-terrain-field-v2.md`](planet-terrain-field-v2.md).

## Goals

- Keep the work inside `projects/planet`; do not promote new core APIs yet.
- Improve procedural terrain shape with continent-scale land masks, mountain
  belts, lowland variation, and fine detail.
- Expose richer surface fields from the CPU and GLSL paths: land mask,
  moisture, temperature, material class, and roughness.
- Expand material bands enough for visual inspection: deep water, shallow
  water, beach, lowland, highland rock, and snow.
- Tune default LOD for better near-ground readability while staying within
  existing live patch and patch-resolution caps.
- Keep repeatable headless captures documented for surface and LOD comparison.

## Non-Goals

- No ocean integration, clouds, imported GIS data, real erosion simulation, or
  out-of-core streaming in this batch.
- No renderer-wide PBR material system changes. The surface shader may become
  richer, but it remains a project-local planet material preview.
- No new quality preset system; defaults can be tuned directly.

## Acceptance Targets

- Orbit view should show recognizable land/water structure, mountain regions,
  snow bands, and shoreline material variation.
- Surface view should be less blocky at the default settings.
- Debug views should continue to validate terrain height, slope, material,
  bathymetry, shoreline, wireframe, and LOD behavior.
- CPU tests and shader build should pass together so procedural field contracts
  do not drift between validation and live rendering.

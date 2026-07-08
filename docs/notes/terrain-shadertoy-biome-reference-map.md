# Terrain ShaderToy Biome Reference Map

Date: 2026-07-08

This note maps the local ShaderToy terrain examples into reference lanes for
`projects/terrain_ref`. The goal is to add more terrain and biome source-shape
targets without turning ShaderToy snippets into production terrain architecture.

## Decision

Use selected ShaderToy examples as visual and source-model references for clean
room `terrain_ref` recipes. Do not copy restrictive or unclear shader code into
Cubey, and do not treat the recipes as production biome contracts.

The first expansion batch should add three broad comparisons:

- `shadertoy-alpine`: high mountain mass, ridges, valleys, snow/rock/grass.
- `shadertoy-dunes`: wind-aligned dune forms and sand/ripple material cues.
- `shadertoy-lake-basin`: basin terrain, standing water, shoreline/wetness cues.

These sit beside `terrain-engine-ref` and `shadertoy-mountain` in
`terrain_ref`. They are visual/source references only; the future
`projects/terrain` product lane can borrow the useful fields after the source
models prove worthwhile.

## Local Reference Classification

| Reference files | Use | Notes |
| --- | --- | --- |
| `mountains.glsl`, `mountain_peak.glsl`, `swiss_alps_*`, `eroded_mountains_*` | Alpine source/material target | Use for visual vocabulary: broad mass, peak/ridge drama, snowline, rock/grass zoning, atmospheric scale. Keep code clean-room because licensing is often noncommercial/share-alike or unclear. |
| `desert_sand.glsl` | Dune/source-material target | Use the concept of wind-aligned rolling ridges, domain warp, and small ripple normals. Avoid copying the implementation directly. |
| `mountains_and_lakes_*`, `day_at_the_lake_*`, `misty_lake.glsl`, `castaway_*` | Lake basin and shoreline target | Use for waterline, shallow/deep tint, shoreline wetness, and basin contrast. Keep real lake generation and water rendering deferred. |
| `dry_rocky_gorge.glsl`, `desert_canyon.glsl`, `canyon.glsl` | Later canyon/badlands study | Useful for arid color/material and canyon composition, but several examples are path-authored or restrictive. Do not make them first-batch recipes. |
| `rainforest_*`, `the_forest.glsl`, `windy_plains.glsl` | Later material/foliage study | These are more about vegetation, atmosphere, and scene composition than heightfield terrain. Wait until terrain fields can drive foliage eligibility. |
| `terrain_*`, `terrain_erosion_noise_*`, `clean_terrain_erosion_filter_*`, `advanced_terrain_erosion_filter_*` | Process/operator study | Keep under the existing ShaderToy operator extraction path, not this visual recipe batch. |

## Extraction Rules

- Implement the recipes as clean-room C++ and GLSL samplers with deterministic
  seed handling.
- Keep recipe names stable and explicit; do not hide them behind a generic
  "biome" enum yet.
- Keep recipe output in the `terrain_ref` visual path: height, normals, material
  color, optional waterline.
- Do not add product fields, hydrology simulation, foliage rendering, ocean
  integration, or planet adapters in this batch.
- Preserve provenance in docs and captures. The local ShaderToy files are study
  material, not imported source packages.

## First Review Matrix

Generate side-by-side outputs for each recipe:

- material and height color;
- oblique and surface-low camera;
- water enabled and disabled where relevant.

The first review should judge large-scale read first:

- alpine should build to high ridges/peaks rather than noisy hills;
- dunes should read as rolling wind-shaped ridges, not random streaks;
- lake basin should make water/shoreline contact obvious without claiming lake
  generation.

## Deferred

- Badlands/canyon recipe.
- Forest/plains recipe with vegetation eligibility.
- Real water pass with reflection/refraction.
- Product-field promotion into `projects/terrain`.
- ShaderToy manifest normalization or browser-assisted metadata import.

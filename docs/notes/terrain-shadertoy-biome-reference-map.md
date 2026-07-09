# Terrain ShaderToy Biome Reference Map

Date: 2026-07-08

This note maps the local ShaderToy terrain examples into reference lanes for
`projects/terrain_ref`. The goal is to add more terrain and biome source-shape
targets without turning ShaderToy snippets into production terrain architecture.

## Decision

Use selected ShaderToy examples as visual and source-model references for clean
room `terrain_ref` recipes. Do not copy restrictive or unclear shader code into
Cubey, and do not treat the recipes as production biome contracts.

The first expansion batch added three broad comparisons:

- `shadertoy-alpine`: high mountain mass, ridges, valleys, snow/rock/grass.
- `shadertoy-dunes`: wind-aligned dune forms and sand/ripple material cues.
- `shadertoy-lake-basin`: basin terrain, standing water, shoreline/wetness cues.

The next expansion batch should add two more terrain-family probes:

- `shadertoy-badlands`: arid plateau, eroded cuts, dry washes, cliffs, and
  strata-like material.
- `shadertoy-coast-island`: island/coastal landform, beach shelf, inland
  buildup, local coastal cliffs, and simple sea-level contact.

These sit beside `terrain-engine-ref` and `shadertoy-mountain` in
`terrain_ref`. They are visual/source references only; the future
`projects/terrain` product lane can borrow the useful fields after the source
models prove worthwhile.

## Local Reference Classification

| Reference files | Use | Notes |
| --- | --- | --- |
| `mountains.glsl`, `mountain_peak.glsl`, `swiss_alps_*`, `eroded_mountains_*` | Alpine source/material target | Use for visual vocabulary: broad mass, peak/ridge drama, snowline, rock/grass zoning, atmospheric scale. Keep code clean-room because licensing is often noncommercial/share-alike or unclear. |
| `desert_sand.glsl` | Dune material/detail target | Use for sand material, wind-aligned ripple normals, subtle layer crossing, and close surface texture. Do not treat it as a strong macro dune-height source; the source itself calls out the scene as artificial and texture-focused. |
| `Hesiod/WaveDune`, `TerraForge3D/dunes.glsl`, `SimpleWindErosion` | Dune source-shape study | Better references for macro dune shape than `desert_sand.glsl`. Use them clean-room: asymmetric windward/slip-face profiles, envelope modulation, and domain displacement should drive the heightfield. |
| `mountains_and_lakes_*`, `day_at_the_lake_*`, `misty_lake.glsl`, `castaway_*` | Lake basin and shoreline target | Use for waterline, shallow/deep tint, shoreline wetness, basin contrast, and explicit water-depth/shoreline fields. Keep real lake generation and water rendering deferred. |
| `dry_rocky_gorge.glsl`, `desert_canyon.glsl`, `canyon.glsl` | Badlands/gorge source and material target | Use clean-room ideas for plateau cuts, dry washes, arid strata color, and slope/cliff exposure. Avoid direct code copy and avoid a single authored path line as the source model. |
| `waterworld.glsl`, `castaway_*`, `sunrise_at_pulau_sibu.glsl`, `alien_waterworld.glsl` | Coast/island source and material target | Use for island silhouette, sea-level mask, beach shelf, shallow-water tint, and local coastal cliffs. Keep water rendering simple: fixed sea level plus existing flat water surface. |
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

## Current Port Review

- `shadertoy-alpine` is the strongest current port. It reads as a mountain mass
  with ridges, snow, rock, grass, and small water bodies. Remaining weaknesses
  are mostly render/capture issues: finite patch framing, snow over-brightness,
  and clipmap faceting in low views.
- `shadertoy-dunes` is accepted as a reference slice after switching from
  symmetric periodic bands to a broader asymmetric dune-field source. Remaining
  weaknesses are material/lighting issues: the height view reads better than the
  sand material.
- `shadertoy-lake-basin` is accepted as a reference slice after strengthening
  the surrounding rim, basin depression, and shoreline response. It remains
  visually plain because real water rendering is intentionally deferred.
- `shadertoy-badlands` is acceptable as a first-pass reference slice. It reads
  as dry eroded terrain with useful roughness and strata cues, but the drainage
  hierarchy still needs broader gorge/wash structure before it can be treated as
  a strong canyon or badlands source model.
- `shadertoy-coast-island` needs a corrective pass before adding more biome
  families. The first pass reads too much like a centered radial mask: compact
  island disk, ring beach, and smooth inland mound. The next version should be a
  layered coastal field with shoreline/headland/shelf/inland hierarchy rather
  than another blob-shaped mask.

## Next Expansion Review Targets

- `shadertoy-badlands` should read as dry eroded terrain: plateau mass,
  branching/noise-driven cuts, dry wash floors, local cliffs, and arid strata.
  It should not be a single authored canyon line.
- `shadertoy-coast-island` should read as an island or coastal patch: clear
  land/sea boundary, beach shelf, inland height buildup, and occasional coastal
  cliffs. The water surface can remain the existing flat review plane.

## Coast Correction Target

- Replace the radial island source with a warped coast field that can form a
  partial coastline, headland, or broken island edge within the fixed review
  patch.
- Derive beach, shelf, inland buildup, and coastal cliffs from the same signed
  coast field so the features remain coherent instead of being tacked on as
  unrelated masks.
- Add a closer coastal review camera so the shoreline shape is readable without
  relying on the low surface view alone.

## Deferred

- Forest/plains recipe with vegetation eligibility.
- Real water pass with reflection/refraction.
- Product-field promotion into `projects/terrain`.
- ShaderToy manifest normalization or browser-assisted metadata import.

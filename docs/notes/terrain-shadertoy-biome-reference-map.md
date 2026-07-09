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

The second expansion batch added two more terrain-family probes:

- `shadertoy-badlands`: arid plateau, eroded cuts, dry washes, cliffs, and
  strata-like material.
- `shadertoy-coast-island`: island/coastal landform, beach shelf, inland
  buildup, local coastal cliffs, and simple sea-level contact.

The next wide batch should add four more comparison probes:

- `shadertoy-plains`: low-relief rolling grassland, subtle swales, and wind or
  grass material variation.
- `shadertoy-gorge`: dry high-incision canyon terrain with corridor hierarchy,
  tributary cuts, cliff walls, floors, and strata.
- `shadertoy-glacial-highland`: icy highland terrain with snowfields, broad
  glacial valley hints, exposed rock ribs, and talus/ice contrast.
- `shadertoy-crater-field`: barren cratered terrain with overlapping
  depressions, raised rims, ejecta roughness, and regolith material.

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
| `rainforest_*`, `the_forest.glsl` | Later material/foliage study | These are more about vegetation, atmosphere, and scene composition than heightfield terrain. Wait until terrain fields can drive foliage eligibility. |
| `windy_plains.glsl`, `cloudy_terrain_*` | Plains source/material target | Use for low-relief height variation, swales, grass/wind color variation, and subtle drainage hints. Do not make the patch empty or noise-flat. |
| `dry_rocky_gorge.glsl`, `desert_canyon.glsl`, `canyon.glsl` | Gorge/canyon target | Use for high-incision corridor vocabulary, tributary cuts, cliff exposure, arid floors, and strata. Keep the source field procedural; do not author one straight centerline. |
| `swiss_alps_*`, `eroded_mountains_*`, `foggy_mountains_2.glsl` | Glacial highland target | Use for broad snow/ice coverage, rock ribs, valley carving hints, talus/scree, and high-altitude atmosphere. Keep it distinct from green alpine material zoning. |
| `a_battered_alien_planet.glsl`, `alien_landscape.glsl`, `lunar_cubemap_*` | Crater field target | Use for crater depressions, raised rims, ejecta roughness, and barren regolith material. Treat this as a terrain-driver stress test, not a planet renderer. |
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

- Forest/rainforest recipe with vegetation eligibility.
- Real water pass with reflection/refraction.
- Product-field promotion into `projects/terrain`.
- ShaderToy manifest normalization or browser-assisted metadata import.

## Wide Batch Review Targets

- `shadertoy-plains` should read as broad low-relief terrain with gentle rolling
  structure, swales, and material variation. It should not read as a blank
  plane or as high-relief hills.
- `shadertoy-gorge` should read as coherent dry incision: a main corridor plus
  tributary cuts, cliff walls, floor material, and strata. It should not be a
  single straight authored canyon line or scattered cracks.
- `shadertoy-glacial-highland` should read differently from `shadertoy-alpine`:
  broader snow/ice fields, U-shaped valley hints, rock ribs, and talus/ice
  material rather than mostly green alpine mountains.
- `shadertoy-crater-field` should show crater rims and floors at macro scale,
  with ejecta and regolith roughness. It should not devolve into speckled noise.
- Keep all four as waterless reference recipes in this batch; real lakes,
  wetlands, water rendering, and foliage stay deferred.

## Wide Batch Initial Review

- `shadertoy-plains` is accepted as a first-pass low-relief slice. The oblique
  read is intentionally subtle, with broad grassland variation and shallow
  swales rather than strong landforms. Later work can add clearer drainage or
  vegetation eligibility fields.
- `shadertoy-gorge` is accepted as a first-pass dry incision slice. It has a
  coherent corridor and tributary hints, though the strongest corridor can land
  near the patch edge. Later work can improve corridor framing and hierarchy.
- `shadertoy-glacial-highland` is accepted as a first-pass icy highland slice.
  It is brighter and more snow-dominant than alpine by design, but later
  material work should add stronger ice, exposed rock, and talus contrast.
- `shadertoy-crater-field` is accepted as a first-pass non-biome terrain stress
  case. Crater rims and floors read at macro scale; later work can vary crater
  density and regolith color if this lane remains useful.

## Gorge Refinement Target

- Focus the next correction pass on `shadertoy-gorge`; plains and crater-field
  are good enough as coverage references, and glacial-highland can wait for a
  later snow, ice, rock, and talus material-separation pass.
- Current gorge problems: the main incision can land too close to the patch
  edge, the surface-low view reads more like arid texture than terrain relief,
  tributaries are too scratch-like, and material zoning relies too much on
  normalized height instead of the actual gorge source field.
- The refined gorge should use one procedural source model for corridor,
  floor, walls, and tributaries. Keep it clean-room and noise-field driven:
  no authored line, no hand-placed center path, and no isolated mask that does
  not also drive the heightfield.
- The visual target is a broad dry plateau with a warped incised corridor,
  variable-width floor, coherent wall bands, and tributary cuts that feed the
  main gorge without becoming hairy disconnected scratches.

## Gorge Refinement Review

- The refined `shadertoy-gorge` pass is accepted as a stronger reference slice:
  the main incision now sits inside the patch, has a broader variable-width
  floor, and reads clearly from the surface-low camera instead of appearing as
  mostly arid surface texture.
- Material now follows the same source field as the height driver, so floor,
  wall, and tributary cues remain aligned with the landform. The dark bands are
  intended as opposing gorge walls around one floor, not separate parallel
  river channels.
- Remaining caveats: tributaries are still subtle from the default cameras,
  wall/floor material contrast may need art tuning, and this remains a
  reference recipe rather than a production canyon or river generator.

## Crater Field Refinement Target

- Focus the next correction pass on `shadertoy-crater-field`; the first-pass
  jittered-cell source reads too regular because it gives every grid cell one
  similarly sized crater opportunity.
- Replace the single-grid crater source with sparse multi-scale populations:
  occasional large craters, medium craters, and smaller impacts, each with
  dropout and low-frequency density variation so empty regions remain.
- Vary crater age and response so rims, floors, ejecta, and overlap do not look
  uniform. Older craters should soften; fresher craters can keep stronger rims
  and darker floors.
- Keep this as a clean-room crater stress recipe, not a planet renderer or
  physically correct impact simulator.

## Crater Field Refinement Review

- The refined `shadertoy-crater-field` source is accepted as a stronger
  reference slice: crater placement now comes from sparse large, medium, and
  small populations with density dropout instead of one candidate per cell.
- The resulting field has more empty regions, stronger size variation, and a
  few dominant impacts, which reduces the previous wallpaper-like regularity.
- Material tuning was left unchanged in this pass because the visual weakness
  was the source distribution. Remaining work, if this lane stays useful, is to
  add stronger ejecta scars, partial burial, and more visibly eroded old rims.
- A follow-up variation pass rotated and warped each crater population domain
  and added a rare intermediate impact population. This further hides the
  underlying cell lattice, though the recipe still remains a procedural
  reference rather than a physically realistic impact history.

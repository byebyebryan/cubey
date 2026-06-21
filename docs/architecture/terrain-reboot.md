# Terrain Reboot Direction

Date: 2026-06-21

This document captures the current terrain reboot direction before a new
`projects/terrain` implementation starts. It promotes the useful lessons from
`terrain_lab_legacy`, `procedural_terrain_legacy`, `planet`, the shared
procedural foundation, and local reference projects into one design checkpoint.

## Decision

Start a new terrain project as a local terrain product generator, not as a
direct continuation of either legacy terrain app and not as a planet renderer.

The first implementation should prove a deterministic terrain data product:
coherent source fields, process fields, terrain feature fields, material and
vegetation hints, diagnostics, and simple consumers. Visual rendering matters,
but the renderer should consume terrain products rather than becoming the source
of truth.

The core pipeline should be:

```text
region config -> coherent source fields -> reusable operators ->
terrain drivers/processes -> terrain tile products -> consumers
```

Biomes should be recipes over those products. They should not be the first unit
of implementation.

## Current Project Boundaries

The existing terrain-adjacent projects keep distinct roles:

| Project | Reboot role |
| --- | --- |
| `terrain_lab_legacy` | Preserved R&D evidence for local terrain fields, diagnostics, river hierarchy, and failure modes. Do not extend its contract. |
| `procedural_terrain_legacy` | Preserved coastal terrain and bathymetry experiment. Keep its shoreline/ocean field contract as a later adapter target. |
| `planet` | Current scale, cube-sphere LOD, tile identity, local-detail host, and eventual integration target. Do not start the local terrain reboot inside it. |
| `ocean` | Future consumer of shoreline, bathymetry, wetness, and water-depth products. It should not own terrain generation. |
| `atmosphere` / `cloud` | Environment and lighting consumers/producers that pressure terrain material, weather, climate, and capture integration later. |

The new project should live as `projects/terrain` once implementation starts.
It should initially be a local workbench over kilometer-scale regions, with a
product vocabulary that can later map to `planet` tile keys and ocean field
views.

## Previous Attempt Lessons

The most important lesson is that hand-authored terrain features do not scale.
Lines, disks, quadrants, centered masks, one-off canyons, and fixture-like
watersheds can look plausible in one camera view, but they do not answer where
the feature starts, where it ends, how it continues outside the patch, or how
materials and downstream systems should consume it.

The terrain model should instead start from coherent fields:

- broad elevation and relief;
- uplift, resistance, strata, basin, and ridge support;
- runoff, flow, accumulation, stream order, and discharge;
- wetness, deposition, talus, snow or sand supply, and other process fields;
- material, biome, and vegetation-potential fields derived from terrain state.

High-frequency noise remains useful, but only as a detail layer controlled by
larger fields. The terrain should still read correctly when the detail layer is
disabled.

The earlier biome-by-biome work was useful because it exposed which drivers are
missing, not because each slice should be preserved as a template. Mountain,
river, lake, dune, plain, glacial, canyon, coast, and biome work should now be
organized around reusable drivers and products.

## Foundation Available Now

The shared `cubey::procedural` layer already provides a reasonable first base:

- `Grid2DDesc` and `ScalarField2D` for deterministic local scalar fields;
- `FieldSet2D` for named field collections;
- source recipes and source fields for layered coherent noise;
- FastNoiseLite-backed sampling and legacy deterministic noise helpers;
- field operators for smoothing, normalization, composition, slope, curvature,
  local relief, remap, percentile remap, ridge, terrace, and similar shaping;
- patch-domain helpers for deterministic future tile sampling;
- metadata and content hashes for generated artifacts.

Keep this layer domain-neutral. Terrain ridges, rivers, dunes, materials, and
biomes should live in the terrain project until their abstractions are stable
and clearly useful outside terrain.

## Product Contract

The reboot should define an explicit terrain product before pursuing visual
polish. A first `TerrainRegionConfig` should carry:

- seed and named seed domains;
- local/world origin and extent;
- grid resolution and cell size;
- recipe id and generator revision;
- optional debug/capture labels.

A first `TerrainTileProduct` or `TerrainRegionProduct` should carry named
fields such as:

- height in meters and height above water level;
- base elevation, broad relief, structure contribution, process contribution,
  and detail contribution;
- normal, slope, curvature, and local relief;
- flow direction, flow accumulation, stream order, and discharge proxy;
- river mask, active trunk, tributaries, water presence, channel width, valley
  width, and lake/wetland masks when available;
- wetness, sediment/deposition, incision, talus, snow/ice, or sand supply
  fields as process outputs;
- material masks and dominant material;
- coarse climate hints such as moisture and temperature;
- vegetation potential fields such as grass, shrub, tree, and canopy density;
- summaries, range data, coverage, dominant classes, and content hashes.

Consumers should read the product:

- final terrain renderer and debug views;
- ocean shoreline and bathymetry adapter;
- planet tile or local-detail adapter;
- future foliage eligibility;
- collision/navigation/export tools;
- tests and capture summaries.

This keeps the shader, mesh, ocean, and planet paths from each inventing their
own terrain truth.

## First Slice

The first vertical slice should be a temperate mountain river catchment.

This is a better first slice than isolated mountains, isolated rivers, or a
canyon scene because it exercises the core product contract:

- broad terrain structure;
- mountain/ridge/uplift fields;
- drainage over the generated terrain;
- a trunk river with occasional tributaries;
- valley width, channel width, wetness, and deposition;
- material response to elevation, slope, moisture, and river hierarchy;
- vegetation potential as data fields, without a foliage renderer.

Canyons should later become a dry, high-incision expression of the river and
drainage fields. Dunes, glacial valleys, lakes, coast, alpine peaks, and plains
should become later sentinel recipes over the same product model.

The first slice should include headless diagnostics before GUI polish:

- final shaded capture;
- height, slope, curvature, and local relief views;
- source/driver field views;
- flow accumulation, stream order, river mask, trunk, tributaries, river width,
  wetness, and deposition views;
- material and vegetation-potential views;
- JSON or text summaries for seed, recipe, ranges, coverage, and hashes.

## Reference Takeaways

Use references as design literature and targeted algorithm sources, not as whole
engines to port.

Borrow these ideas:

- Skybolt: tile products first, consumers second; async tile loading and loaded
  tree separation; vegetation and water as terrain-data consumers.
- Proland: `TileProducer -> cache/storage -> dependent producers`, borders,
  ancestor fallback, elevation and normal producer separation, and hydro graph
  concepts.
- Terrain3D: region resources, height/control/color maps, material control
  packing, shader-side normals, clipmap tradeoffs, and per-region instancing
  ideas.
- Planet-Generator: explicit layer stacks, first-layer-as-mask support, patch
  generation separated from scene insertion, and per-patch summaries.
- TerraForge3D: code-centric graph shape:
  `base shape -> source/detail/process layers -> biome mixer -> outputs`.
- FastNoise2: inspectable generator recipes, domain warp, blends, modifiers,
  and deterministic serialized generator concepts.
- `slterraingeneration`: practical static hydrology tools such as sink filling,
  flow accumulation, slope/aspect, Strahler order, and erosion/deposition
  approximations.
- SimpleHydrology, SoilMachine, SimpleWindErosion, and related erosion refs:
  process-field and diagnostic ideas for later passes, not first-slice runtime
  requirements.
- `terrain-diffusion`: tile-oriented elevation plus climate conditioning,
  deterministic requests, quantile-style remapping, and climate/material fields
  as first-class outputs. Do not make it a runtime dependency.

Avoid these traps:

- copying GPL or engine-bound code into Cubey;
- treating OSG, Godot, OpenGL, or editor-node details as Cubey architecture;
- adding a UI node graph before the code-centric recipe model is proven;
- making a full hydraulic simulation or foliage renderer the first milestone;
- hiding terrain generation inside one shader or one final-view camera path.

## Testing Bar

Early tests should be data-first:

- deterministic output for fixed seed, extent, and recipe;
- finite field values and expected ranges;
- stable content hashes or summaries for small grids;
- terrain remains legible with detail disabled;
- river network has a long connected trunk and limited disconnected fingers;
- channel and valley widths vary with stream order or discharge;
- material and wetness respond to slope, elevation, and drainage fields;
- no centered demo footprint, quadrant layout, or single authored line is
  required to make the slice work.

Visual tests should follow:

- headless PNG capture for final and debug views;
- optional GUI smoke once the product and debug views are stable;
- capture directories that clearly separate current reboot outputs from legacy
  terrain outputs.

## Current Implementation Checkpoint

The reboot now has a CPU/reference `projects/terrain` product generator and
headless PNG review path. The current slice is `temperate-mountain-river` over a
local region, with deterministic source fields, height/slope analysis, static
flow accumulation, active river trunk and tributary masks, wetness/deposition,
material masks, vegetation potential, summaries, and tests.

The river driver intentionally moved away from a single authored line. It routes
over a coherent low-frequency drainage potential derived from the terrain seed,
then extracts a main channel from the strongest routed catchment and paints soft
trunk/tributary product fields. This is a useful midpoint because downstream
fields can consume a connected river product, but it is not a complete hydrology
solution.

Known limitations:

- D8 routing still leaves unnaturally straight channel reaches, sharp turns, and
  angular segments in close inspection.
- The drainage pass does not yet perform real depression fill, breach routing,
  erosion, or lake/wetland resolution.
- Tributary selection is intentionally conservative and should be replaced by a
  more explicit network extraction pass once the route model improves.
- The final PNG is an inspectable debug composition, not the target renderer.

## Deferred

Keep the first implementation narrow. Defer:

- full planet curvature, out-of-core streaming, or async residency;
- ocean rendering, surf, foam, or water animation;
- complete vegetation rendering, wind, impostors, and foliage LOD;
- physically complete hydraulic erosion;
- real-world GIS, DEM, satellite data, or ML runtime generation;
- GPU compute terrain generation before the CPU/reference contract is stable;
- migration of legacy Terrain Lab or procedural-terrain payloads into shared
  foundation APIs.

## Next Implementation Batches

The next terrain batches should improve the underlying drivers before adding
more biome labels:

1. Replace or augment the current D8 channel path with vectorized/smoothed
   channel extraction, so rivers do not read as straight grid-aligned reaches
   with sharp turns.
2. Evaluate a small depression-fill or breach-routing pass from the hydrology
   references before adding lakes, canyons, or wider river systems.
3. Split mountain/ridge drivers into explicit terrain products instead of
   treating mountains as only material response over height noise.
4. Add capture summaries or manifest metadata for the review set so image
   artifacts can be compared without relying only on manual inspection.

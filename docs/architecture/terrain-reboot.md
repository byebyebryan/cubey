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
local region. Generator revision `14` emits deterministic source fields,
height/slope analysis, repaired routing diagnostics, smoothed active river trunk
and tributary masks, wetness/deposition, material masks, vegetation potential,
summaries, and tests.

The river driver intentionally moved away from a single authored line. It routes
over a coherent low-frequency drainage potential derived from the terrain seed
on a padded hidden routing domain, then crops diagnostics back to the visible
region. The diagnostic catchment now uses D-Infinity-style continuous flow
angles and fractional accumulation, reducing the obvious D8 lattice in
`flow-accumulation`. Revision 10 added a bounded priority-flood epsilon repair
over the hidden routing surface before D8, D-Infinity, accumulation, stream
order, sink masks, and active river extraction run. The repaired surface is the
published `drainage_potential`; `routing_fill_delta` exposes where the raw
routing surface was raised. Connected support components are selected from the
hidden-domain drainage hierarchy, a trunk is traced through the selected
support, and limited tributaries are accepted only when they visibly connect
back into an active channel.

Revision 11 keeps D8 graph traversal as connectivity scaffolding but stops
using raw grid paths as the rendered centerline. Selected paths are converted to
sub-cell channel points, smoothed, nudged along the continuous D-Infinity flow
direction, constrained against large uphill moves on the repaired routing
surface, and then rasterized with bounded lateral meander,
discharge/stream-order width, and strength variation. The default recipe no
longer paints raw support cells. The stress recipe uses an additional
procedural basin-convergence routing profile and paints connected support paths
through the same de-gridded channel pipeline rather than painting unrelated
corridors. This is a useful midpoint because downstream fields can consume a
connected river product, but it is not a complete hydrology solution.

The `temperate-mountain-river-stress` recipe is a diagnostic variant of the same
slice. It keeps the same terrain/routing sources but expands active channel
extraction with a stronger basin-grade routing source and extra connected
support paths feeding one selected basin. Its purpose is to make routing
artifacts visible across more of a review patch, not to define the desired
default composition. Revision 11 also raises support-order gates, spaces
accepted confluences, caps low-value branch clutter, and adds tests for stress
endpoint density plus long straight or diagonal high-strength runs.

Revision 12 is a coverage-recovery pass after revision 11 pruned the network too
far. The default recipe accepts more tributary and order-seed branches, and
order-seed paths can trace farther upstream before accumulation cutoff. The
stress recipe broadens the selected support hierarchy and restores more
connected support coverage, but adds a support-path spacing guard so newly
accepted support routes cannot run too close to previously accepted support
geometry. This keeps the de-gridded channel renderer while reducing the earlier
coverage regression.

Revision 13 is a stress-only trunk hierarchy pass. The default recipe remains
stable, but the stress recipe now classifies visible support/order-seed/branch
paths with stream-order and normalized discharge metrics before painting them.
Qualified major paths are promoted into `river_trunk` up to a capped branch
count, while lower-order attached paths remain in `tributaries`. The stress
trunk band is wider but softer: it preserves meaningful `0.30` and `0.50`
coverage for review while avoiding the long `0.80` axis-aligned and diagonal
runs that exposed the D8-like outlet core. This is hierarchy classification over
the current routing fields, not a new hydrology solver.

Revision 14 adds a stress-only branch distinctness gate on top of the revision
13 hierarchy. Promoted candidates are compared against the already promoted
trunk skeleton and must contribute enough visible samples at a meaningful
distance from that skeleton. Near-parallel candidates that add almost no unique
visible area are skipped as redundant instead of being repainted as trunk or
tributary clutter. This reduces the visible cluster of parallel promoted trunks,
but it also makes the stress trunk field sparser; the diagnostic recipe now
prioritizes distinct branch shape over maximizing trunk coverage.

Known limitations:

- Fractional accumulation reduces receiver quantization, but active trunk
  tracing still uses support-graph and local routing fallbacks. Branch placement
  and some bends can remain less organic than real rivers.
- The drainage pass performs only an epsilon fill for routing continuity. It
  does not yet perform breach routing, erosion, or lake/wetland resolution.
- Padded routing makes local review slices less artificial, but the route model
  is still static and should not be mistaken for simulated river evolution.
- Default river composition now has a stronger review footprint than the
  revision 11 prune pass, but it is still trunk-dominant and can read too sparse
  compared with a mature drainage network.
- The stress recipe now promotes major branches into a trunk hierarchy and
  rejects the worst near-parallel promoted alternatives, but it can still expose
  straight-ish support strokes because it is still using a static routing
  hierarchy rather than evolved hydrology.
- Stress generation is currently expensive enough that performance should be
  revisited before adding heavier hydrology checks.
- The final PNG is an inspectable debug composition, not the target renderer.

A revision 4 experiment tried replacing the active river product with direct
channel-graph edge rendering after an epsilon fill pass. It was reverted because
the captures were worse: disconnected segments, hard straight or diagonal runs,
and schematic clusters. The preserved lesson is in
`docs/notes/terrain-river-graph-routing-attempt.md`.

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

1. Evaluate breach routing and simple process erosion references now that the
   routing surface has a bounded fill pass.
2. Improve corridor scoring/tiling so the default review composition is less
   sparse without turning into the stress recipe.
3. Replace the current support-path hierarchy and distinctness classifier with a
   more principled basin/tributary model so visual coverage does not depend on
   capped promoted branch counts.
4. Split mountain/ridge drivers into explicit terrain products instead of
   treating mountains as only material response over height noise.
5. Add capture summaries or manifest metadata for the review set so image
   artifacts can be compared without relying only on manual inspection.

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
local region. Generator revision `26` emits deterministic source fields,
explicit mountain support/ridge/peak/uplift fields, height/slope analysis,
repaired routing diagnostics, smoothed active river trunk and tributary masks,
pre-process and carved height fields, channel/valley incision diagnostics,
wetness/deposition, material masks, vegetation potential, summaries, and tests.
Revision `24` extends that product with diagnostic-only mountain gully fields:
`erosion_delta_m`, `gully_mask`, `crease_proxy`, and
`post_erosion_height_m`.
Revision `25` adds mountain macro source fields:
`mountain_mass`, `mountain_shoulder`, and `mountain_summit_core`. These expose
broad highland mass, foothill/shoulder buildup, and sparse summit cores before
local detail and diagnostic erosion are judged.
Revision `26` adds `mountain_profile_height_m` and changes the mountain stress
recipe so `height_m` is generated from one coherent profile plus bounded
detail. Ridge, peak, and uplift fields remain diagnostics and attribution
fields instead of independently stacked visible height layers.

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

Revision 15 corrects the resulting sparse stress read by separating trunk
continuity from broad network reach. The stress recipe keeps `river_trunk` as a
continuous mainstem, restores the longer accumulation-trunk candidate, and grows
additional connected basin-tree paths into `tributaries` from edge-biased
stream-order candidates. Those paths must add visible samples and coarse review
footprint while still contacting the active network, so the stress product reads
as one connected basin tree rather than disconnected watershed clusters or a
single trunk with short fingers. Extra trunk promotion is disabled until the
branch hierarchy can be made continuous at confluences.

Revision 16 re-enables stress trunk promotion only behind a rendered
connectivity guard. A candidate path is first painted into a temporary trunk
field and accepted only if the high-strength trunk remains dominated by one
connected component. Connected-support, order-seed, basin, and corridor branch
paths also get tighter grid-aligned source caps plus an additional pre-curving
pass before rasterization. The result is cleaner than the revision 15
stress-network output: long vertical promoted stubs are rejected and the worst
tributary fingers are filtered. It is still not the final broad river-network
model; the stress product currently favors a clean connected mainstem with
some attached branches over forcing a third crop-edge branch.

Revision 17 pivots the stress recipe to a deterministic graph-first river source
over the padded hidden domain. The graph is a source topology and diagnostic,
not the rendered product mask; accepted graph paths still pass through the
channel rasterization pipeline.

Revision 18 promotes selected high-discharge or high-order graph tributaries
into `river_trunk` with the mainstem. Smaller attached graph paths remain in
`tributaries`, so the hierarchy reads as major channels versus smaller branches
instead of trunk-as-one-line.

Revision 19 adds an isolated `temperate-mountain-range-stress` recipe and makes
mountain structure first-class in the product: `mountain_support`,
`ridge_support`, `peak_support`, `mountain_uplift`, `ridge_uplift`, and
`peak_uplift`. Existing river recipes still emit the new fields, but broad
mountain and peak uplift are disabled there so the river work remains stable.
Revision 20 adds `mountain_range_spine`, `mountain_ridge_hierarchy`, and
`mountain_peak_candidates` so the mountain stress recipe exposes macro range
organization, ranked ridge source, and sparse summit candidates before material
or biome polish.

Revision 21 adds a peak-first mountain skeleton source path for the mountain
stress recipe: `mountain_envelope`, `mountain_peak_anchors`,
`mountain_peak_prominence`, `mountain_ridge_skeleton`, and
`mountain_ridge_influence`. The stress recipe now derives the revision 19 and
20 support/ridge/peak fields from that envelope/anchor/skeleton model. Default
river recipes still emit inactive values for the new stress-only diagnostics.

Revision 22 keeps the same mountain field contract but improves peak hierarchy
readability. The mountain stress recipe now uses an envelope-driven base
elevation instead of the generic regional tilt, increases peak uplift, broadens
ridge shoulders, gates residual detail against mountain structure, and retunes
`mountain-relief.png` around a softer elevation-first ramp.

The terrain workbench now also has a renderer-backed `terrain_preview` app. It
consumes `TerrainRegionProduct`, adapts `height_m` into an indexed local
heightfield mesh with normals and review colors, and renders through the normal
Vulkan windowed/headless host. It is a preview consumer for judging relief in
perspective, not a replacement for the CPU product generator, planet terrain, or
future clipmap renderer.

Revision 23 changes the river product from a mostly visual overlay into a
terrain-form process. The generator now preserves `pre_process_height_m`, uses
active river, trunk, tributary, stream-order, discharge, channel-width, and
valley-width fields to derive `channel_incision` and `valley_incision`, and
publishes the carved surface as `height_m`. Slope, local relief, material masks,
wetness/deposition, and vegetation potential are computed against the carved
height. The renderer-backed preview adds material, height, river, and channel
color modes so geometry review can be separated from water/material tint.

Revision 24 adds a clean-room gully diagnostic over the mountain stress recipe.
It consumes `height_m`, slope, curvature, local relief, and mountain support to
publish `erosion_delta_m`, `gully_mask`, `crease_proxy`, and
`post_erosion_height_m`. It is deliberately diagnostic-only: `height_m` remains
unchanged, and downstream slope, material, wetness, vegetation, and river fields
still describe the current carved product rather than the post-erosion review
height.

Revision 25 improves the mountain stress recipe's source hierarchy rather than
applying the gully diagnostic to final terrain. `mountain_mass` carries broad
highland support, `mountain_shoulder` widens foothill/shoulder buildup around
that mass, and `mountain_summit_core` isolates sparse high-summit support. The
renderer-backed preview can now select `height`, `post-erosion`, or
`pre-process` surfaces, which makes diagnostic geometry comparisons explicit.

Revision 26 applies the coherent height rule to the mountain stress recipe.
`mountain_profile_height_m` is solved from range mass, shoulder ramp, smooth
ridge influence, and broad summit influence; `pre_process_height_m` stays close
to that profile plus bounded detail. Visible ridge influence now uses smooth
distance-to-connection fields rather than 8-neighbor raster paths, and peak
support is broadened so summits no longer become isolated needle cones.

Revision 27 keeps `mountain_profile_height_m` as the visible height source while
changing the mountain drivers to curved crest fields, elongated summit support,
and explicit `mountain_saddle_gate` suppression between high structures. The
result is directionally better than revision 26 because source fields no longer
read as straight bands and round blobs, but the perspective captures still show
that crest/summit shaping is synthetic and needs erosion, stratified ridge
cleanup, or better process shaping before it reads as a finished mountain range.

The next work should be foundation-shaped inside the terrain project rather
than another isolated biome image. Keep the per-revision river and mountain
recipes as diagnostics while extracting reusable process-field helpers, adding
capture manifests, and using those outputs to tune incision and hierarchy. The
working roadmap lives in
[`docs/notes/terrain-process-roadmap.md`](../notes/terrain-process-roadmap.md).
The broader lane map for source drivers, process operators, product fields,
review consumers, and integration adapters lives in
[`docs/notes/terrain-project-map.md`](../notes/terrain-project-map.md).
That first foundation pass is now in place: `terrain_process_fields` owns the
spread and relief-clamped lowering helpers used by river incision, and scalar
review directories write `manifest.json` beside their PNGs. The first
ShaderToy-informed process diagnostic is also in place as terrain-local gully
fields, without promoting it into shared procedural foundation.

The ShaderToy terrain/hydro review does not change the pipeline. It adds a
reference-backed operator lane inside the terrain project: clean-room
gully/erosion diagnostics first, shallow-water/lake relaxation later, and
shoreline material cues only when terrain/ocean handoff work resumes. ShaderToy
river scenes remain visual references; river topology should stay tied to the
graph and hydrology references. See
[`docs/notes/terrain-shadertoy-operator-extraction.md`](../notes/terrain-shadertoy-operator-extraction.md).

Known limitations:

- The terrain reboot is currently a CPU debug/product workbench. Retaining many
  named fields, writing scalar PNGs, and building dense preview meshes is
  intentional because it gives source/process visibility, stable manifests, and
  reviewable regressions. This is a temporary compromise, not the runtime terrain
  architecture for scene-scale or planet-scale terrain.
- ShaderToy-style terrain examples remain relevant to the runtime direction even
  when their topology is not directly reusable: scene terrain should eventually
  use view-dependent sampling, shader-side detail, tiled or clipmap meshes, and
  minimal retained state. The current 55-field product path should stay a debug
  artifact unless a downstream consumer explicitly needs a field.
- CPU multithreading can improve the workbench, but it does not replace LOD or
  tile streaming. Add phase profiling before parallelizing generation/export so
  the first threading pass targets a measured bottleneck rather than the most
  visible loop.
- The first phase profiles confirm that the fixed-extent surface previews are
  generation-bound. Height-color preview on the `16.384km` mountain stress
  patch spends about `47.56s`, `96.27s`, and `412.75s` in generation at `513`,
  `1025`, and `2049`; host rendering remains below half a second. Material
  color adds a secondary mesh-build cost because it samples more fields per
  vertex.
- Fractional accumulation reduces receiver quantization, but active trunk
  tracing still uses support-graph and local routing fallbacks. Branch placement
  and some bends can remain less organic than real rivers.
- The drainage pass performs only an epsilon fill for routing continuity. It
  does not yet perform breach routing, erosion, or lake/wetland resolution.
- Revision 23 incision is a deterministic field-propagation pass over active
  river sources, not hydraulic erosion. It makes channels visible in geometry,
  but it does not yet enforce longitudinal bed profiles, sediment budgets, or
  bank/terrace formation.
- Revision 24 gully fields are also not hydraulic erosion. They are bounded
  slope/curvature/local-relief diagnostics for mountain review and do not affect
  final height or downstream fields yet.
- Padded routing makes local review slices less artificial, but the route model
  is still static and should not be mistaken for simulated river evolution.
- Default river composition now has a stronger review footprint than the
  revision 11 prune pass, but it is still trunk-dominant and can read too sparse
  compared with a mature drainage network.
- The stress recipe now broadens reach through connected tributary basin growth
  while keeping the trunk continuous, but it can still read too sparse and
  schematic because it is still using a static routing hierarchy rather than
  evolved hydrology.
- Stress generation is currently expensive enough that performance should be
  revisited before adding heavier hydrology checks.
- The mountain stress recipe is an early diagnostic driver, not a finished
  alpine biome. It does not yet include erosion time, talus, snow/ice, glacial
  valley carving, or world-scale range continuity. Revision 27 removes the
  straight ridge bands and round summit blobs from revision 26, but the result
  can still read synthetic because crests and summit lobes are source-shaped
  rather than process-eroded.
- The final PNG is an inspectable debug composition, not the target renderer.
  Use `mountain-relief.png` for mountain-form review because `final.png` still
  includes the river/material overlays.
- `terrain_preview` is a local mesh preview only. It improves peak/basin
  readability, but it deliberately does not solve tiled scale, LOD, water,
  foliage, atmosphere integration, or terrain algorithm quality.
- The `surface` and `surface-low` preview cameras make foreground terrain
  softness visible. They are useful review tools, but higher resolution mostly
  cleans silhouettes; it does not create the missing near-field detail or fix
  synthetic source/process shape.

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

The next terrain batches should use the new process/product evidence before
adding more biome labels:

1. Use manifest ranges and renderer-backed captures to tune river incision
   against height-only and channel
   perspective captures.
2. Review the clean-room gully/erosion diagnostic over the mountain stress
   recipe before deciding whether any erosion-like pass should affect height.
3. Refine the coherent mountain profile with erosion-aware crest cleanup,
   alpine material/valley contrast, and better world-scale range continuity.
4. Return to river topology with graph/hydrology references once mountain
   process/source quality is easier to inspect.
5. Promote additional process helpers only when they prove reusable across
   river, mountain, water-body, or material passes.
6. Revisit breach routing, simple process erosion, lakes/wetlands, dunes,
   snow/talus, and foliage eligibility after the process fields are easier to
   inspect and compose.

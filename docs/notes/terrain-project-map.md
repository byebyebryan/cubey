# Terrain Project Map

Date: 2026-06-30

This note maps the rebooted terrain project after the river, mountain, reference,
and ShaderToy operator passes. It is a design map for `projects/terrain`, not a
commit plan for one batch.

## Project Shape

`projects/terrain` should remain a local terrain product workbench. Its job is
to make terrain state credible and inspectable before that state is handed to
planet, ocean, foliage, water, or final rendering work.

The project should be organized around this spine:

```text
source drivers -> process operators -> product fields -> review consumers -> adapters
```

Biomes come after that spine. A biome should be a recipe over source drivers,
process operators, material rules, and vegetation hints. It should not be the
first owner of mountain, river, lake, dune, coast, or foliage logic.

## Work Lanes

| Lane | Owns | Current state | Direction |
| --- | --- | --- | --- |
| Source drivers | Coherent macro structure before process effects. | River graph source, mountain envelope/peak/ridge source, broad relief, drainage potential. | Build reusable mountain, basin/river, lake-basin, dune, coast, plains, and glacial source drivers. |
| Process operators | Terrain morphology and derived process fields. | River incision, spread/lowering helpers, wetness/deposition proxies. | Add clean-room gully/erosion diagnostics first, then talus, snow, sand, shallow water, and process-memory experiments. |
| Product fields | The public terrain truth. | Named `TerrainRegionProduct` fields, summaries, hashes, manifests, scalar PNG views. | Keep every meaningful output inspectable before feeding renderers or adapters. |
| Review consumers | Ways to see field quality. | Scalar exports, manifests, `terrain_preview`, stress recipes. | Add focused debug renders and scenic review only after field truth exists. |
| Integration adapters | Translation into other Cubey systems. | Deferred, with legacy terrain/ocean and planet contracts as targets. | Feed ocean, planet, foliage, and fluid/water only after local fields are stable. |

## Source Driver Lane

Source drivers answer where a feature exists before process detail is applied.
They should use coherent fields, graph sources, or deterministic world/tile
sources, not authored local marks.

Near-term source drivers:

- **Mountain range / peaks / ridges**: broad mountain support, peak anchors,
  prominence, ridge skeleton, ridge influence, and uplift. This is the active
  shape-quality priority after river incision.
- **River / basin graph**: connected trunk, major tributaries, minor tributaries,
  discharge, stream order, channel width, and valley width. The graph/hydrology
  refs remain the topology donors.
- **Standing-water basin**: later lake/wetland source fields such as basin mask,
  overflow, outlet, and water level. Do not implement this before mountain and
  erosion diagnostics stabilize.

Later source drivers:

- dunes and sand supply;
- coast, shoreline, beach slope, bathymetry, and estuary hints;
- plains, badlands, glacial valleys, snow/ice, and talus support;
- climate/material source fields such as moisture, temperature, exposure, and
  vegetation zones.

## Process Operator Lane

Process operators should be deterministic scalar-field transforms with explicit
inputs, outputs, stats, and limits. They should live in `projects/terrain` until
their names and contracts are useful outside terrain.

Current operators:

- spread/decay fields;
- relief-clamped split lowering;
- subtract lowering from height;
- river channel and valley incision over active river fields.

Next operator:

- clean-room gully/erosion diagnostic for mountain stress.

Expected fields for the first gully pass:

- `erosion_delta_m`
- `gully_mask`
- `crease_proxy`
- optional `post_erosion_height_m` while diagnostic-only

Guardrails:

- It is not hydraulic erosion.
- It should not copy ShaderToy formulas.
- It should consume meter-aware fields such as height, slope/derivatives, local
  relief, and optional mountain support.
- It should not affect final `height_m` until the diagnostic views prove useful.
- Downstream slope, local relief, material, wetness, and vegetation views must be
  recomputed if the pass becomes height-affecting.

Later process operators:

- river topology refinement from graph/hydrology refs;
- discharge/momentum/channel-memory diagnostics from SimpleHydrology-style
  process state;
- shallow-water/lake relaxation over `height_m + water_depth_m`;
- talus/scree, snow/ice, sand transport, wetness/deposition, and material
  eligibility.

## Product Field Lane

Product fields are the contract. Every high-value terrain feature should have a
named field before it becomes a material or renderer effect.

Field groups:

- geometry: `height_m`, `pre_process_height_m`, slope, curvature, local relief;
- mountain: envelope, support, ridge hierarchy, peak anchors, prominence,
  skeleton, influence, uplift;
- river: drainage potential, flow direction, accumulation, stream order,
  graph plan, graph discharge, mask, trunk, tributaries, channel width, valley
  width, channel incision, valley incision;
- process: wetness, deposition, future erosion/talus/snow/sand/water fields;
- material and vegetation: rock/soil/grass masks, vegetation potential, later
  foliage eligibility fields;
- metadata: recipe id, generator revision, grid, seed, summaries, content hash,
  field stats, and capture filenames.

The renderer, ocean, planet, and foliage paths should consume these fields rather
than recreating their own terrain truth.

## Review Consumer Lane

Review consumers exist to reveal field quality, not to hide field problems.

Keep using:

- per-field scalar PNGs;
- `manifest.json` stats and hashes;
- stress recipes for river and mountain drivers;
- `terrain_preview` material, height, channel, profile, and perspective captures.

Add later:

- a scenic terrain debug render with slope/height/material bands and simple fog;
- shoreline/water-contact review once water-depth or shoreline fields exist;
- capture matrices for alpine, lake, dune, coast, plain, canyon, and glacial
  sentinel recipes.

Do not make final shading the source of terrain credibility. If a feature only
works in `final.png`, it is not stable enough.

## Integration Adapter Lane

Adapters should be thin translations from `TerrainRegionProduct` or future tile
products into other systems.

| Consumer | Terrain should provide | Terrain should not own |
| --- | --- | --- |
| Ocean | Bathymetry, shoreline SDF, water depth, wet sand, beach/sediment masks, river-mouth hints. | FFT waves, Fresnel, refraction, animated foam, ocean rendering. |
| Planet | Deterministic tile fields, world/sample domains, halo/border policy, material and water hints. | Cube-sphere LOD policy inside the terrain workbench. |
| Foliage | Grass/shrub/tree/canopy eligibility, slope/moisture/elevation masks. | Tree rendering, wind, impostors, foliage LOD. |
| Fluid/water | Terrain height, water depth, basin/outlet hints, roughness/friction masks. | Full fluid solver policy. |
| Atmosphere/cloud | Capture conditions, lighting/material review targets, optional weather/moisture hints. | Sky, cloud, or weather rendering. |

## Reference Routing

Use references by role:

- **ShaderToy terrain/hydro**: compact process and visual vocabulary. Borrow
  clean-room gully diagnostics, shallow-water diagnostic shape, and shoreline
  visual cues. Do not borrow river topology or renderer architecture.
- **terrain-erosion-3-ways**: river topology and basin graph refinement. Use it
  for upstream/downstream graph mechanics, directional inertia, upstream volume,
  and capped downcut ideas.
- **SimpleHydrology**: process-state diagnostics. Use it for discharge,
  momentum, channel memory, sediment proxy, and erosion/deposition deltas, not a
  full runtime particle sim.
- **Planet-Generator / TerraForge3D**: source-driver and recipe structure. Use
  layer stacks, first-layer masks, base-shape/detail/process staging, and
  inspectable parameter vocabulary.
- **terrain-diffusion**: later macro conditioning, climate/elevation field
  vocabulary, quantile/stat shaping, and offline comparison. Do not add ML
  runtime dependency.
- **3DWorld / Proland / scene LOD refs**: later systems and rendering lessons.
  Use them for diagnostics, tiling, materials, water/terrain integration, and
  LOD vocabulary, not direct code ports.

## Sequencing

### Phase 0: Preserve Current Evidence

Keep the current river and mountain stress recipes, scalar exports, manifests,
and `terrain_preview` captures as review evidence. Do not remove fields just
because a better model is planned.

Done when:

- current capture commands stay documented;
- field manifests make old/new comparisons possible;
- default and stress recipes remain deterministic.

### Phase 1: Mountain Process Diagnostic

Add the clean-room gully/erosion diagnostic over the mountain stress recipe.

Done when:

- `erosion_delta_m`, `gully_mask`, and `crease_proxy` can be reviewed as scalar
  fields;
- tests cover deterministic finite output and bounded deltas;
- perspective captures show whether the operator improves or harms ridge/peak
  readability;
- the pass remains diagnostic until proven.

### Phase 2: Mountain Source Cleanup

Use the diagnostic evidence to improve the mountain source hierarchy.

Done when:

- broad mass, ridges, shoulders, and peaks read separately;
- local detail no longer dominates macro form;
- mountain support builds plausibly above lowlands;
- review does not depend on `final.png` material tint.

### Phase 3: River Topology Refinement

Return to river graph quality with the hydrology refs.

Done when:

- trunk, major tributaries, and minor tributaries form a broader connected basin
  without D8-looking straight or diagonal strokes;
- discharge and stream order drive width and hierarchy;
- channel/valley incision remains visible in height-only and channel previews;
- the topology can explain continuation beyond the local patch.

### Phase 4: Standing-Water Diagnostic

Add lake/wetland source and shallow-water relaxation diagnostics.

Done when:

- water depth, water surface, outlet/overflow, basin, and lake/wetland masks are
  visible fields;
- boundaries and mass changes are explicit;
- shoreline review is field-driven, not just a water shader.

### Phase 5: Sentinel Recipe Composition

Create representative recipes from the source/process pieces.

Candidate sentinels:

- alpine range;
- mountain river catchment;
- lake basin / wetland;
- dune field;
- coast / island / estuary;
- plains / low-relief basin;
- canyon / dry high-incision river expression;
- glacial valley.

Done when:

- each sentinel mostly reuses existing drivers and operators;
- each has an explicit reason to exist as a stress case;
- failures are attributed to a source, process, product, or consumer lane.

### Phase 6: Scale And Adapters

Move from local patches toward tile/world integration.

Done when:

- source drivers sample from deterministic world or patch domains;
- halo/border policy is explicit;
- terrain products can feed planet and ocean adapter experiments;
- local recipe quality survives at larger extents and higher resolutions.

## Non-Goals For The Next Few Batches

- No biome gallery before source/process operators are credible.
- No direct ShaderToy code import.
- No full hydraulic erosion claim.
- No foliage renderer.
- No ocean/coast renderer inside `projects/terrain`.
- No ML terrain runtime.
- No shared `cubey::procedural` promotion until a terrain-local operator proves a
  stable, renderer-independent contract.

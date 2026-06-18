# Procedural Terrain Reference Review

Date: 2026-06-18

This note captures a local reference pass over more complex procedural terrain
projects in `~/code/ref`. The purpose is to calibrate Cubey's terrain and
procedural-foundation direction, not to port any one project wholesale. Cubey's
near-term target remains a code-centric CPU/reference layer with explicit source
recipes, field operators, diagnostics, and project-owned landform drivers.

## Current Cubey Baseline

Cubey now has the start of a shared procedural layer under
`include/cubey/procedural`:

- `ScalarField2D` and `Grid2DDesc` for local scalar fields;
- deterministic legacy hash/value/FBM helpers and wrapped FastNoiseLite
  sampling;
- field operators for smoothing, normalization, composition, remap/clamp,
  ridge/terrace shaping, slope/curvature, and local relief;
- source-field sampling with explicit domain transforms and optional coherent
  warp;
- small GLSL helper mirrors for repeated hash/noise/remap formulas.

Terrain Lab and `projects/procedural_terrain` are current pressure tests, not
contract owners. Both can be deprecated or rebooted if their early terrain
payloads conflict with a cleaner shared foundation. They have already exposed
the failure mode to avoid: hand-authored lines, disks, quadrants, and local masks
can look acceptable in one camera view, but they do not scale into believable
terrain systems. The foundation should therefore keep moving toward coherent
source fields, named process fields, and inspectable drivers.

## References Reviewed

### 3DWorld

Path: `~/code/ref/3DWorld`
License: GPLv3

3DWorld is useful as systems inspiration, not as a code source. The terrain
work is embedded in a broad OpenGL engine with procedural terrain, voxel terrain,
water, snow/material blending, cities, roads, buildings, editing, and scene
configuration.

Relevant takeaways:

- The terrain generator is treated as part of an environment system. Height,
  water, snow, roads, tunnels, bridges, fog, material blends, and tiled meshes
  all consume terrain state.
- Scene configs expose practical generator knobs: generator mode, seed, mesh
  size/scale, height range, frequency filtering, heightmap read/write, tiled
  terrain, and erosion iteration counts.
- City/road code shows terrain as a mutable field with downstream constraints:
  flattening, sloped road regions, bridge/tunnel decisions, and water checks.
- Voxel and tiled terrain code reinforces the need for deterministic patch or
  tile boundaries, not just a one-shot demo heightmap.

What Cubey can borrow:

- Diagnostic and configuration vocabulary.
- The idea that terrain fields should be useful to downstream systems, not only
  to a height shader.
- Heightmap import/export and repeatable scene/capture recipes as validation
  tools.

What Cubey should not borrow:

- Source code, because GPLv3 is incompatible with casual reuse here.
- The large engine/global-state structure.
- Shader-local visual noise as the main generation model.

### Planet-Generator

Path: `~/code/ref/Planet-Generator`
License: MIT

Planet-Generator is a Godot addon for layered-noise planets with quadtree LOD
terrain patches. It is relevant because it keeps planet patch generation,
generator resources, and mesh/job boundaries fairly legible.

Relevant takeaways:

- Terrain height comes from a layer stack. Each noise layer has explicit
  parameters such as enabled state, seed, strength, frequency/period, octave
  count, center offset, and whether it uses the first layer as a mask.
- The first noise layer can define broad support while later layers add detail
  under that support. This maps well to Cubey's source-field and driver-field
  direction.
- Shape evaluation is independent from the terrain patch mesh. A patch samples
  points, normalizes them to the sphere, evaluates the generator, then emits
  mesh data and approximate min/max values.
- Job queue boundaries separate generation from scene insertion and allow patch
  generation to be cancelled.

What Cubey can borrow:

- A code-level layer-stack concept for `SourceRecipe2D` or `SourceRecipe3D`.
- First-layer-as-mask support as a named operation, not an implicit one-off
  trick inside each biome.
- Per-recipe summaries such as approximate min/max, useful for materials and
  debug normalization.
- The boundary between generator evaluation and planet LOD/mesh ownership.

What Cubey should not borrow:

- Godot-specific resource, node, mesh, or job APIs.
- Planet LOD responsibilities in the shared procedural foundation.

### TerraForge3D

Path: `~/code/ref/TerraForge3D`
License: MIT

TerraForge3D is a larger procedural terrain tool with GPU/CPU generation,
heightmap buffers, texture baking/export, erosion, biome controls, and a node or
shader-editor workflow. The UI graph is not a target for Cubey right now, but
its generator data flow is highly relevant.

Relevant takeaways:

- The central generation manager owns heightmap data, swap buffers, seed/input
  textures, a biome mixer, and a set of biome managers.
- A biome manager builds a local biome height field from base shape, optional
  custom base shape or DEM input, and base noise.
- A biome mixer composes biome outputs into the final heightmap with enable,
  strength, and optional biome-mask controls.
- Base shapes are named generator programs such as classic terrain, cliff,
  cracks, crater, dunes, islands, mountain, and volcano.
- Shader configs embed parameter metadata, so generators are serializable and
  inspectable even when their implementation is not a UI graph.

What Cubey can borrow:

- A code-centric equivalent of a terrain graph:
  `base shape -> source/detail/process layers -> biome mixer -> outputs`.
- Named biome or landform buffers that can be composed instead of overwriting a
  single global height field.
- Swap-buffer style field processing and explicit generator data objects.
- Input/seed textures as a future analog for masks, imported maps, or selected
  regions.
- Export/review tooling as part of the generation loop.

What Cubey should not borrow:

- The UI node editor as the primary foundation model.
- OpenGL/compute-shader implementation details before the CPU reference layer is
  stable.
- Tool-specific ImGui and resource-manager architecture.

### terrain-diffusion

Path: `~/code/ref/terrain-diffusion`
License: MIT

terrain-diffusion is a modern ML terrain generator. It describes itself as a
learned successor to procedural noise, using a hierarchical diffusion pipeline
for deterministic, random-access terrain and climate tiles.

Relevant takeaways:

- It treats elevation and climate as companion outputs. Terrain is not only a
  height field; temperature, precipitation, variability, and similar fields help
  control biome/material results.
- The public API is tile-oriented: a caller requests a coordinate range and
  scale, then receives binary elevation and climate channels.
- The pipeline uses deterministic tile-seeded noise, coarse conditioning maps,
  quantile transforms against real-world datasets, and a coarse-to-detail model
  stack.
- Conditioning strength and custom conditioning imports provide a useful mental
  model for controllable terrain: broad maps steer generated detail without
  hand-authoring every feature.

Applicability to Cubey:

- Not a good runtime dependency now. It brings Python/PyTorch, model weights,
  GPU/latency/caching concerns, and external data assumptions that do not fit
  Cubey's C++ rendering foundation.
- Potentially useful later as an offline comparison, dataset generator, or
  terrain-sketch inspiration if Cubey grows an asset-baking path.
- Very relevant conceptually: Cubey should make climate/material/process fields
  first-class, use deterministic random-access tile contracts, and consider
  distribution or quantile remapping even without ML.

## Cross-Cutting Lessons

- A UI node graph is not required to get graph-like benefits. Cubey can use
  typed C++ recipe structs, named source layers, and explicit field pipelines.
- Single scalar outputs are too narrow. The reusable output should become a
  named field set: height, density, masks, process fields, material hints,
  climate or environment drivers, water/process diagnostics, and summaries.
- Source recipes should capture intent and parameters, not hide them in one
  slice's shader. Layer enable flags, weights, masks, domain transforms, warp,
  distribution shaping, and stats should be inspectable.
- Tile and patch determinism should be part of the contract early. The same
  source recipe should sample consistently for local captures, future terrain
  tiles, planet patches, and offline exports.
- Distribution shaping is a foundation feature. Quantile remap, histogram-style
  normalization, and target range controls are more useful than another
  hand-tuned visual curve in each project.
- Biomes should be composition recipes over landform and process fields. The
  project should first build mountain, river, dune, plains, basin, and climate
  drivers, then combine them into biomes.
- Hydrology and erosion should stay reference-backed. Weak process simulation
  can mislead materials and biomes more than no simulation at all.
- Diagnostics are product features for procedural work: final view, driver view,
  per-field PNGs, summaries, and stable capture recipes should be preserved as
  the foundation grows.

## Cubey Implications

Initial foundation pieces promoted from this review:

1. `SourceRecipe2D` and eventually `SourceRecipe3D`: named layer stacks around
   existing `NoiseSource2D`, domain transforms, masks, first-layer-as-mask,
   weights, distribution shaping, and field summaries.
2. `FieldSet2D`: a deterministic collection of named scalar fields plus
   summaries, intended for terrain height, cloud density, masks, water/process
   fields, material hints, climate or environment fields, and debug exports.
3. Distribution operators: quantile-like remap, histogram summaries, percentile
   clamps, and target-range normalization.

Remaining near-term candidates:

1. A tile sampling contract: seed, world origin, extent, resolution, tile id,
   border policy, and stable sample coordinates that do not depend on one
   renderer.
2. Terrain Lab biome or landform mixer prototype: compose named field sets or
   landform outputs with explicit masks and strengths before promoting anything
   terrain-specific to core.
3. Cross-project review of atmosphere/environment, cloud, ocean, fluid, and
   generated-texture consumers before adding terrain-only foundation APIs.
4. Reference-backed mountain and river driver work: mountains should prove broad
   uplift/ridge/peak structure; rivers should prove trunk, tributary, discharge,
   valley width, and water/process fields.
5. Export and capture improvements: stable per-field PNG/output directories and
   small metadata files so old and new captures are easy to compare.

Non-goals for the next foundation slice:

- no procedural UI node editor;
- no ML terrain runtime dependency;
- no GPU compute terrain generator before the CPU/reference contracts settle;
- no code copying from GPLv3 3DWorld;
- no promotion of full hydrology, erosion, foliage, material, or streaming
  policy into `cubey::procedural` until the data contracts are proven.

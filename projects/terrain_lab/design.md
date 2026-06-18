# Terrain Lab Design

This document captures the initial terrain direction before implementation. It
is project-local by design: the work should be concrete enough to build and test
inside `projects/terrain_lab`, but not broad enough to become the planet,
ocean, or foliage system all at once.

## Problem Statement

The earlier terrain work came from two useful but narrow pressures:

- `procedural_terrain` grew from ocean and shoreline needs, so it leans toward
  islands, bathymetry, coast distance, and terrain-ocean fields.
- `planet` needs scale, LOD, local detail, celestial context, atmosphere, and
  integration boundaries, so terrain there carries a lot of unrelated weight.

Those projects should not be forced to answer the whole terrain problem. Terrain
Lab exists to isolate terrain generation itself and to avoid the common failure
mode where terrain becomes a set of local shader noises without coherent global
structure.

## Core Thesis

Terrain should be coherent before it is detailed.

The generator should first establish the structure of a region:

- high-level intent, such as temperate mountain rivers or arid canyon;
- coherent driver fields, such as broad land support, relief potential,
  process potential, and feature selection masks;
- landform graph, such as ridges, basins, stream paths, or strata;
- scalar fields derived from that graph, such as elevation potential,
  resistance, flow, wetness, and deposition;
- process passes that make fields agree, such as valley incision, talus,
  sediment, and slope relaxation;
- material and biome fields derived from the terrain state;
- detail residuals controlled by the above fields.

Detail noise, shader displacement, color breakup, rocks, grass patches, and
small cracks belong at the end of the pipeline. They should not be the source of
the terrain's main shape.

The pivot for the next batch is to make the coherent source explicit. Procedural
noise and randomness are still valid inputs, but the generator should not author
isolated local primitives like "one canyon line", "one ridge line", or a set of
manual dune streaks as the primary shape. Instead, each slice should first build
named driver fields over the whole region, then derive feature masks, height,
materials, and detail from those fields. A feature can still be sparse or
directional, but it must be traceable back to a coherent source field.

The unit of reuse is a landform driver rather than a biome preset. Biome slices
are validation recipes that combine:

- source fields, such as base elevation, relief, runoff, resistance, sand
  supply, wind exposure, and ice support;
- feature drivers, such as ridge systems, river trunks and tributaries, basins
  and plains, dune crests and interdunes, and glacial valley floors;
- process modifiers, such as incision, deposition, talus, slope relaxation,
  wetness, snow, and material response.

A new biome should usually add a missing driver or modifier before it adds
slice-specific shaping. If a slice needs a local exception, that exception
should stay small and visible in the driver/debug fields.

This lesson is broader than terrain. Cubey already uses procedural assets in
planet detail, clouds, fluids, generated textures, ocean-adjacent fields, and
environment rendering. Once a terrain helper becomes a reusable source field,
operator, or process utility, it should be considered for a shared procedural
foundation rather than copied across projects.

## Spatial Scope

Start with a local tangent-plane heightfield.

The first practical region size should be 4-16 km across. That is large enough
for ridges, river hierarchy, valley hierarchy, and material transitions, while
still small enough to reason about without planet curvature, global climate, or
streaming.

The renderer may later use clipmaps or tiled meshes, but the first generator
contract should be independent of the chosen draw path:

- a deterministic region configuration;
- a field grid or sampled field view;
- tile or patch summaries;
- debug views for every major field.

## Data Model

The first implementation should prefer explicit fields over opaque procedural
expressions.

Candidate region configuration:

- seed;
- region extent and cell size;
- slice preset;
- elevation range;
- climate preset;
- process iteration counts and strengths;
- detail amplitude toggles.

Candidate feature data:

- ridge lines and ridge influence;
- local drainage-region ids;
- stream or drainage graph;
- optional fault, strata, dune, or volcanic feature sets for later slices;
- influence masks for plains, steep slopes, valley floors, and high relief.

Initial scalar fields:

- driver base potential;
- driver relief potential;
- driver process potential;
- driver selection mask;
- height;
- structure, process, and detail height contributions;
- slope;
- curvature;
- flow direction;
- flow accumulation;
- stream power;
- wetness;
- deposition or sediment;
- ridge, valley, and basin influence;
- drainage-region id, divide influence, channel influence, and channel distance;
- material masks;
- grass, shrub, tree, and canopy density fields.

The field vocabulary should stay stable enough that tests, debug views, and
future adapters can consume it without knowing the exact generator internals.

## Generation Pipeline

The preferred pipeline is:

```text
1. Resolve region intent
2. Build coherent driver fields
3. Derive feature influence fields
4. Compose initial terrain fields
5. Run process passes
6. Classify materials and biome-density fields
7. Add controlled render detail
8. Emit field summaries and debug outputs
```

### Region Intent

The slice preset should describe the landform goal in concrete terms:
temperate mountain rivers, arid mesa canyon, alpine glacial valley, dunes,
volcanic field, wetland, or coastal shelf. It should not be a grab bag of noise
settings.

### Driver Fields And Feature Graph

Coherent driver fields are the main guardrail against incoherent terrain. For a
slice, they should describe the broad support and process sources before local
feature masks are derived. The feature graph remains useful when a terrain type
needs explicit topology, such as drainage paths or local region ids, but it
should sit downstream of the driver fields rather than replace them.

The shared driver vocabulary is intentionally small:

- base potential: broad land support or accumulation source;
- relief potential: where height contrast, ridges, walls, crests, or peaks are
  allowed to emerge;
- process potential: where routing, incision, deposition, wind transport, ice,
  talus, or other process response should be strongest;
- selection mask: the physical region under test. For current sentinel slices
  this is the full local patch.

Feature-specific sources can be richer, but they should map back to this common
vocabulary before height, materials, or debug rendering consume them.

For sentinel slice presets, the local patch is the region mask. Driver selection
should not add centered disks, ellipses, quadrant gates, vignetted islands, or
other composition-shaped footprints unless that boundary is the landform under
test. Internal masks are still expected, but they should represent terrain state
or process: snowline, ice support, sand supply, talus, wetness, vegetation, lava
flow, channels, deposition, or exposure.

The feature graph does not have to be geologically complete. It only has to
carry enough structure that valleys, ridges, water, and materials line up in the
rendered result.

The shared drainage core should become river-first. It should derive a river
network from base elevation, runoff, relief, and erosion-resistance fields, then
expose discharge, stream order, channel width, valley width, and water presence
as named fields. Canyons should consume that network as an arid, high-incision
variant rather than own a separate canyon-specific skeleton.

The arid core should still be regional and network-first. It should build a
hidden macro region larger than the final view, route drainage through base
elevation, runoff, relief, and erosion-resistance fields, then select a local
canyon window from that network. Canyon floors, tributaries, walls, rims,
benches, and talus should come from river hierarchy, stream power, incision
depth, and distance-to-network rather than from an authored line across the
visible patch. Eight-neighbor raster routing is allowed as a compact
compatibility/debug output, but it should not be the visible river or canyon
skeleton because grid directions create straight sections and sharp
diagonal/orthogonal turns.

Processed noise and Voronoi-like fields may support uplift, runoff, lithology,
resistance, fracture tendency, basin partitioning, and sidewall detail. They are
not the main canyon path source. L-system or rewriting approaches are useful as
research context for branching structures, but they should not become the arid
canyon driver unless constrained by drainage, slope, source, and base-level
rules.

The old temperate watershed fixture was removed because the fixed four-basin
layout read as an artificial quadrant/H composition. The temperate river slice is
now the first wet river reference: it treats the local patch as one drainage
region, derives channels from routed terrain, and renders visible water from a
trunk-first downstream activation pass rather than from isolated per-cell
channel scores. The river hierarchy should guide structure, process, material,
wetness, vegetation, and diagnostic rendering.

The arid, dunes, and alpine sentinels now have explicit driver fields. They are
still visual pressure tests rather than production terrain systems, but the
driver view should show coherent terrain causes instead of post-hoc height or
feature fallback. Dunes keep hydrology diagnostic; alpine can use river fields
as support until meltwater becomes a focused target.

The next two driver blockers are canyon ownership and mountain/ridge structure.
Arid canyon should become a dry, high-incision expression of the shared river
hierarchy instead of a slice-local canyon skeleton with river fields applied
afterward. Mountain ridges/peaks should own the mountain-driver proof; alpine
glacial valley should consume that driver as a valley/process slice rather than
author the mountain topology itself. The shared driver starts from broad uplift,
local relief, and resistance fields, then derives ridges, valleys, cliffs,
peaks, and scree from provisional height, slope/curvature, divide support, and
routed flow. Fixed "central valley plus two side ridges" layouts are a
scaffolding smell, not a terrain model.

The first field-operator batch promotes slope/curvature and local-relief scans
into `cubey::procedural`, then routes Terrain Lab mountain and glacial analysis
through those shared operators. The mountain-ridges/peaks sentinel now uses a
broader uplift/fold source before layering peaks and scree, while alpine
glacial valley keeps its separate valley/wall/process source. Review captures
for this batch were written under
`outputs/terrain_lab/field-operators-20260617-191435/`; they show a clearer
mountain-vs-valley split, though mountain peaks remain intentionally rough for
this foundation slice rather than a final mountain model.

The source-field batch adds shared scalar-field composition and `NoiseSource2D`
sampling. Desert dunes are the first proof consumer: legacy FBM remains the
default source, FastNoiseLite remains opt-in, and both paths now share one
procedural source-field interface. Review captures for this batch were written
under `outputs/terrain_lab/source-fields-20260617-194345/`.

The source-warp batch extends that interface with optional coherent domain
warping and unit-field shaping operators. Desert dunes add a separate
`fastnoise-lite-warped` opt-in path so the stronger deformation can be reviewed
without changing the legacy or plain FastNoiseLite captures. Review captures for
this batch were written under `outputs/terrain_lab/source-warp-20260617-214539/`.

The sentinel slice set should prevent overfitting this model to canyon terrain.
The first representative set is:

- arid mesa canyon, for strata, rims, benches, dry washes, and sparse scrub;
- temperate mountain rivers, for wetter drainage, forest/meadow, and
  divide-channel relationships;
- desert dunes, for wind-shaped terrain where hydrology should be diagnostic
  rather than causal;
- mountain ridges/peaks, for broad range support, ridges, crests, peaks,
  cliffs, scree, and rock/snow material response;
- alpine glacial valley, for U-shaped valley structure, moraine/deposition,
  snow/ice masks, and ice-shaped relief.

These slices are probes for the shared field vocabulary. They are not a
commitment to finish several production biomes in parallel.

The required visual review for each slice is final view plus driver view. The
driver view should explain the landforms without revealing an artificial region
footprint.

### Process Passes

Process passes should be small, named, and inspectable. Early passes can be
process-informed approximations rather than expensive simulations:

- valley incision from drainage area and slope;
- slope relaxation and talus on steep faces;
- deposition in valley floors and low-gradient areas;
- wetness from flow accumulation and concavity;
- snow or altitude masks from height, slope, and exposure.

Each process pass should expose a debug field or contribution so the project can
tell whether the final terrain shape is caused by structure, process, or detail.

Hydrology is now structure-first but still simulation-light. The project may
compute flow directions, accumulation, discharge proxy, stream order, stream
power, sink diagnostics, channel width, water presence, and channel/non-channel
comparisons for every slice. It should not add particle droplet solvers,
discharge/momentum simulation, or destructive hydraulic erosion as default
terrain shaping until those diagnostics are already credible and a sandboxed
prototype visibly improves multiple slices. A weak solver would be worse than no
solver because it would make downstream materials and biomes trust false
river-like artifacts.

### Detail Layer

The detail layer should be controlled by semantic fields:

- rough rock detail on steep exposed slopes;
- low detail on wet valley floors and deposition zones;
- ridge-scale residuals near high relief;
- grass and soil color breakup where vegetation density is high;
- fine snow, scree, or sand residuals only where the material fields allow it.

One required diagnostic should be a noise-off view. If terrain quality collapses
when detail residuals are disabled, the structure and process layers are not
strong enough yet.

## Biomes And Foliage

Biome work starts as terrain data, not a full foliage renderer.

The project should own fields that make foliage possible:

- grass density;
- shrub density;
- tree density;
- canopy height;
- bare ground;
- wetland or meadow masks;
- wind/exposure hints if useful later.

A simple proxy renderer can come after the density fields are credible. A full
foliage system with assets, GPU instancing, culling, LOD, impostors, animation,
and wind should remain a later engine/rendering project unless Terrain Lab needs
one narrow prototype to validate the fields.

## Rendering Boundary

The first renderer should be a workbench, not the final terrain renderer:

- heightfield or tiled mesh output;
- material/debug shading;
- field-view switching;
- optional atmosphere backdrop and sun lighting;
- headless PNG captures;
- deterministic camera/capture presets.

The generator should not depend on the renderer. CPU/reference field generation
should stay testable without opening a Vulkan window.

The current workbench uses CPU mesh extraction from the field grid and keeps the
renderer project-local. It is sufficient for orbit inspection, debug-view
switching, and headless PNG smoke tests, but it is not yet a tiled terrain
renderer, editor, or shared engine contract.

## Integration Boundary

Terrain Lab should eventually feed other projects, but only through explicit
adapters:

- `planet` can consume terrain slice ideas as local-detail residuals, patch
  summaries, or future tile payloads.
- `procedural_terrain` can consume mature shoreline, bathymetry, and material
  fields where coastal slices overlap.
- `ocean` can consume height, bathymetry, shoreline, and wetness outputs after
  those fields are stable.
- shared `cubey::render` contracts should be introduced only after two projects
  need the same boundary.

Until then, keep the project-local vocabulary clear and avoid prematurely moving
terrain types into engine-level code.

## Validation

Terrain quality is partly visual, but the first project should still have
mechanical checks:

- deterministic output for a fixed seed and config;
- finite field values;
- normalized material and biome masks;
- valid drainage-region ids, divide influence, channel influence, and channel
  distances;
- valid flow directions;
- no isolated negative drainage sinks unless the slice intentionally supports
  closed basins;
- stream and valley fields agree within tolerances;
- material masks agree with slope, wetness, and elevation constraints;
- noise-off terrain remains recognizable.

Headless captures should cover both final and diagnostic views. The current
foundation validates deterministic CPU fields, mesh payload extraction, shader
debug-view constants, a windowed startup smoke, default final,
flow-accumulation, river diagnostics, and feature-graph PNG captures, plus an
explicit drainage-region PNG capture. UI editing, runtime regeneration, tiled
rendering, and shader-displacement validation are still deferred.

Recent naturalization diagnostics also track channel/divide sample counts,
divide-channel height separation, channel-flow alignment, material entropy, and
edge steps. These checks keep the generator honest during tuning, but visual
review remains required because the current terrain is still an approximate
workbench slice rather than a physically complete landscape model.

## References

Useful starting points for coherent terrain generation:

- `docs/notes/procedural-terrain-reference-review.md`: current reference pass
  over `~/code/ref/3DWorld`, `~/code/ref/Planet-Generator`,
  `~/code/ref/TerraForge3D`, and `~/code/ref/terrain-diffusion`. The useful
  direction is code-centric source recipes, field sets, tile contracts,
  landform/biome mixers, and diagnostics, not a UI node graph or ML runtime
  dependency.
- `~/code/ref/SimpleHydrology`: useful reference for process state and visual
  diagnostics, especially discharge/momentum/deposition fields, cascade-style
  slope relaxation, and vegetation feedback. Use it as design pressure, not as
  code to port directly; the first mountain pass uses static routing rather
  than a particle droplet solver.
- `~/code/ref/TerrainEngine-OpenGL`: useful reference for terrain presentation,
  camera-centered tiles, tessellation, fog/water context, and material blending.
  Do not copy its shader-noise displacement as the generation model.
- Hydrology-driven procedural terrain generation by Genevaux et al.:
  https://www.semanticscholar.org/paper/Terrain-generation-using-procedural-models-based-on-G%C3%A9nevaux-Galin/84ae6bc48dc8297ff3bf2cb24df0ed8afd21e8a0
- Large-scale terrain from uplift and fluvial erosion by Cordonnier et al.:
  https://onlinelibrary.wiley.com/doi/10.1111/cgf.12820
- Terrain erosion notes from MIT/Cesium:
  https://web.mit.edu/cesium/Public/terrain.pdf

The first implementation should use these as design pressure, not as a mandate
to build a full scientific erosion simulator immediately.

## Open Questions

- Should the first generator be entirely CPU, or should GPU compute start early
  once the field vocabulary is stable?
- How much of the field view should match `TerrainOceanFieldView`, and how much
  should stay terrain-specific until a second consumer exists?
- Should the first renderer share any planet local-detail clipmap code, or use a
  simpler tiled heightfield until the generation side is proven?
- Which terrain slice after the arid mesa canyon gives the best contrast:
  alpine glacial valley, dunes, volcanic terrain, wetland, or coastal
  reconnection?

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

- high-level intent, such as temperate mountain watershed or arid canyon;
- landform graph, such as ridges, basins, watersheds, stream paths, or strata;
- scalar fields derived from that graph, such as elevation potential,
  resistance, flow, wetness, and deposition;
- process passes that make fields agree, such as valley incision, talus,
  sediment, and slope relaxation;
- material and biome fields derived from the terrain state;
- detail residuals controlled by the above fields.

Detail noise, shader displacement, color breakup, rocks, grass patches, and
small cracks belong at the end of the pipeline. They should not be the source of
the terrain's main shape.

## Spatial Scope

Start with a local tangent-plane heightfield.

The first practical region size should be 4-16 km across. That is large enough
for watersheds, ridges, valley hierarchy, and material transitions, while still
small enough to reason about without planet curvature, global climate, or
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
- basin and watershed ids;
- stream or drainage graph;
- optional fault, strata, dune, or volcanic feature sets for later slices;
- influence masks for plains, steep slopes, valley floors, and high relief.

Candidate scalar fields:

- height;
- base elevation potential;
- uplift or relief potential;
- erosion resistance;
- normal;
- slope;
- curvature;
- flow direction;
- flow accumulation;
- stream power;
- wetness;
- deposition or sediment;
- talus/scree potential;
- material masks;
- grass, shrub, tree, and canopy density fields.

The field vocabulary should stay stable enough that tests, debug views, and
future adapters can consume it without knowing the exact generator internals.

## Generation Pipeline

The preferred pipeline is:

```text
1. Resolve region intent
2. Build feature graph
3. Rasterize feature influence fields
4. Compose initial terrain fields
5. Run process passes
6. Classify materials and biome-density fields
7. Add controlled render detail
8. Emit field summaries and debug outputs
```

### Region Intent

The slice preset should describe the landform goal in concrete terms:
temperate mountain watershed, arid mesa canyon, alpine glacial valley, dunes,
volcanic field, wetland, or coastal shelf. It should not be a grab bag of noise
settings.

### Feature Graph

The feature graph is the main guardrail against incoherent terrain. For the
first watershed slice, it should describe ridge hierarchy, basin ownership, and
drainage paths before final height detail is applied.

The feature graph does not have to be geologically complete. It only has to
carry enough structure that valleys, ridges, water, and materials line up in the
rendered result.

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
- valid flow directions;
- no isolated negative drainage sinks unless the slice intentionally supports
  closed basins;
- stream and valley fields agree within tolerances;
- material masks agree with slope, wetness, and elevation constraints;
- noise-off terrain remains recognizable.

Headless captures should cover both final and diagnostic views. The first GUI
smoke can wait until the field generator and a minimal renderer exist.

## References

Useful starting points for coherent terrain generation:

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
- Which terrain slice after the watershed gives the best contrast: arid mesa
  canyon, alpine glacial valley, dunes, volcanic terrain, or wetland?

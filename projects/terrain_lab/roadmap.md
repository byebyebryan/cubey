# Terrain Lab Roadmap

Terrain Lab should advance in small slices. Each slice should leave behind
usable fields, tests, debug views, and capture recipes instead of only a prettier
final image.

## Phase 0: Project Direction

Status: current.

Deliverables:

- project-local docs;
- explicit ownership boundary relative to `planet`, `procedural_terrain`,
  `ocean`, and `atmosphere`;
- first target slice;
- initial field vocabulary and validation expectations.

No CMake target is needed in this phase.

## Phase 1: Field Foundation

Goal: build the terrain generator as deterministic data before building the
full renderer.

Deliverables:

- `TerrainLabConfig` with seed, extent, cell size, slice preset, and process
  strengths;
- `TerrainLabFieldData` with height, slope, curvature, flow, wetness, material,
  and biome-density fields;
- config and field validation tests;
- a small debug-image or text-summary path if useful before Vulkan rendering;
- docs updated with exact commands and current field names.

Success criteria:

- fixed seed produces repeatable field summaries;
- fields are finite and normalized where required;
- field generation can run without a window;
- detail residuals can be disabled independently from structure.

## Phase 2: Temperate Mountain Watershed

Goal: prove one coherent inland terrain slice.

Deliverables:

- ridge and basin feature graph;
- drainage graph or flow-direction field;
- flow accumulation and stream-power fields;
- valley incision, slope relaxation, and deposition passes;
- material masks for exposed rock, soil, scree, meadow/grass, forest potential,
  and snow/altitude where appropriate;
- vegetation density fields for grass, shrubs, trees, and canopy height;
- debug views for feature graph, height, slope, flow, wetness, materials, and
  density fields.

Success criteria:

- streams connect from upper basins toward lower outlets unless a closed basin
  is explicitly configured;
- valleys are lower and wetter than adjacent ridges;
- steep exposed slopes favor rock/scree, while valley floors favor soil,
  meadow, or forest potential;
- the terrain remains readable with detail noise disabled.

## Phase 3: Renderer Workbench

Goal: make field inspection fast enough for terrain iteration.

Deliverables:

- standalone `terrain_lab` app target;
- heightfield or tiled mesh rendering;
- material/debug view switching;
- basic sun lighting and optional atmosphere backdrop;
- deterministic headless capture;
- windowed UI for staged config edits and regeneration.

Success criteria:

- final and diagnostic headless captures can run in CI-style smoke tests;
- debug views expose the fields that shape the terrain;
- config changes can be compared without rebuilding unrelated projects.

## Phase 4: Detail And Material Pass

Goal: add local richness without breaking terrain coherence.

Deliverables:

- semantic detail residuals gated by slope, material, wetness, and relief;
- material roughness/color channels or equivalent shader inputs;
- noise-off diagnostic retained as a first-class view;
- contribution debug views for structure, process, and detail layers.

Success criteria:

- detail improves the terrain without changing the intended landform identity;
- cliffs, ridges, valley floors, and wet/depositional areas receive different
  detail behavior;
- material masks remain stable under moderate detail tuning.

## Phase 5: Biome Dressing Proxies

Goal: validate vegetation-related fields without committing to a full foliage
renderer.

Deliverables:

- proxy grass/shrub/tree visualization;
- density, canopy-height, and bare-ground debug views;
- slope, wetness, elevation, exposure, and material controls for density;
- capture presets that show the terrain with and without proxy dressing.

Success criteria:

- terrain does not rely on proxy plants to read correctly;
- density fields look plausible in debug views;
- future foliage renderer requirements are captured as data contracts rather
  than hidden in rendering code.

## Phase 6: Additional Terrain Slices

Candidate slices:

- arid mesa canyon: strata, cliffs, dry washes, talus, sparse vegetation;
- alpine glacial valley: U-shaped valleys, moraine/deposition, snow and ice
  fields;
- dunes: wind direction, slip faces, interdune flats, sparse plants;
- volcanic terrain: cones, lava flows, ash fields, rough basalt;
- wetland: low relief, saturated soil, channels, reeds, ponds;
- coastal reconnection: beach, bluff, estuary, bathymetry handoff to
  `procedural_terrain` and `ocean`.

Each slice should add only the feature/process vocabulary needed for that
terrain type. Shared abstractions should wait until at least two slices need the
same mechanism.

## Phase 7: Integration Adapters

Goal: let mature Terrain Lab fields feed the rest of Cubey.

Deliverables:

- adapter experiments for `planet` local-detail residuals or tile payloads;
- optional bridge to `TerrainOceanFieldView` where coastal terrain overlaps
  ocean needs;
- capture recipes that compare standalone Terrain Lab output with integrated
  planet or ocean use;
- promotion candidates for shared `cubey::render` contracts.

Success criteria:

- integration uses explicit field contracts instead of copying generator
  internals;
- Terrain Lab remains useful as an isolated workbench after integration starts;
- shared contracts are introduced because there are real consumers, not because
  the first implementation guessed a global API.

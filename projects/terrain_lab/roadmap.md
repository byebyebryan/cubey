# Terrain Lab Roadmap

Terrain Lab should advance in small slices. Each slice should leave behind
usable fields, tests, debug views, and capture recipes instead of only a prettier
final image.

## Phase 0: Project Direction

Status: landed.

Deliverables:

- project-local docs;
- explicit ownership boundary relative to `planet`, `procedural_terrain`,
  `ocean`, and `atmosphere`;
- first target slice;
- initial field vocabulary and validation expectations.

No CMake target is needed in this phase.

## Phase 1: Field Foundation

Status: landed.

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

Current implementation notes:

- common `RunConfig` grid and debug-view inputs are accepted;
- coast-oriented `terrain.*` flags remain outside Terrain Lab;
- CPU fields cover height, contribution, slope, curvature, drainage, wetness,
  deposition, material, vegetation-density, watershed, and influence data.

## Phase 2: Temperate Mountain Watershed Reference

Status: naturalized watershed core landed and retained as an explicit fixture;
deeper graph and stream-network work remains.

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

Current implementation notes:

- a deterministic four-basin watershed raster produces watershed id, soft
  divide influence, channel guide influence, and channel distance fields;
- an initial flow solve derives the stronger channel influence used for
  incision, wetness, deposition, and detail suppression;
- a small slope-relaxation pass smooths harsh process artifacts while preserving
  structure/process/detail contribution accounting;
- tests compare channel samples against non-channel terrain and divide samples
  against channels;
- `--terrain-lab-slice temperate-mountain-watershed` keeps this behavior
  available for tests and comparison captures after the arid slice became the
  default;
- remaining work is a richer feature graph with stronger stream connectivity,
  basin hierarchy, and slice-specific controls if the watershed slice becomes
  active again.

## Phase 3: Renderer Workbench

Status: v1 visual workbench landed; live editing remains deferred.

Goal: make field inspection fast enough for terrain iteration.

Deliverables:

- standalone `terrain_lab` app target;
- CPU heightfield mesh rendering;
- material/debug view switching;
- basic sun lighting and optional atmosphere backdrop;
- deterministic headless capture;
- windowed UI for staged config edits and regeneration.

Success criteria:

- final and diagnostic headless captures can run in CI-style smoke tests;
- debug views expose the fields that shape the terrain;
- config changes can be compared without rebuilding unrelated projects.

Current implementation notes:

- windowed orbit inspection and headless PNG capture are registered;
- final and flow-accumulation PNG smokes use conservative image stats to catch
  blank output;
- feature-graph PNG smokes cover the default arid feature payload, while the
  watershed PNG smoke pins the explicit watershed slice;
- fragment shader debug constants are checked against the C++ enum;
- live ImGui controls, runtime regeneration, tiled meshes, and atmosphere-backed
  lighting are intentionally deferred.

## Phase 4: Detail And Material Pass

Status: first naturalization pass landed; richer material response remains.

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

Current implementation notes:

- detail residuals are damped on channel floors and separated into ridge,
  slope, and broad residual components;
- material masks use slope, wetness, elevation, deposition, channel influence,
  and semantic noise breakup;
- final shading uses existing field payloads for subtle wet/channel/deposition
  tinting without adding renderer-global terrain state.

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

Status: active as sentinel slices for model pressure.

Pivot note: slices should now prove that coherent source fields can drive the
terrain. Procedural randomness remains valid, but isolated authored features
should be treated as temporary scaffolding. A slice passes this phase when its
main landforms can be inspected through named driver fields before feature masks,
height, materials, and detail are applied.

Mask rule: for sentinel slices, the local patch is the biome boundary. Driver
selection should be full-patch unless the slice explicitly tests a physical
boundary. Internal masks should encode terrain/process state, not crop the demo.

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

Current implementation notes:

- arid mesa canyon is the default slice and owns the first non-watershed
  terrain identity;
- generated fields include mesa/rim/divide influence, dry canyon and side-wash
  channels, low wetness, no snow, rock/scree/soil-heavy material masks, sparse
  scrub density, and very low tree density;
- desert dunes are the first correction target for explicit wind/sand/relief
  driver fields instead of locally authored dune streaks;
- alpine glacial valley remains available as a minimal sentinel slice, but its
  current guide-line ridges are known debt until an alpine driver migration;
- hydrology remains analysis-only and avoids a toy erosion solver;
- the material vocabulary now includes sand for wind-shaped terrain, while snow
  continues to stand in for snow/ice in the glacial sentinel.

Success criteria:

- dunes remain readable with flow ignored as a shaping force;
- dune crests, slip faces, and interdune flats are derived from inspectable
  driver fields rather than hand-placed streaks or lines;
- driver debug views do not expose centered disks, ellipses, quadrants, or
  other artificial slice footprints;
- glacial terrain reads through valley shape, snow/ice, and deposition rather
  than river carving;
- shared fields remain useful across all four sentinels without becoming
  water-centric.

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

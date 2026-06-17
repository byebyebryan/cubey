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
  deposition, material, vegetation-density, drainage-region, and influence data.
- next foundation work should organize generation around reusable source fields,
  feature drivers, and process modifiers. Biome and landform slices remain the
  visual/test fixtures that prove those drivers compose.
- reusable noise, field-processing, and hydrology helpers should move toward a
  shared procedural foundation once they are useful outside Terrain Lab.

## Phase 2: Temperate Mountain River Reference

Status: the authored four-basin watershed fixture has been removed; the active
temperate reference is now one local drainage region with flow-derived channels
and trunk-first visible river activation.

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

- streams connect through routed terrain without a fixed quadrant or H-shaped
  fixture;
- valleys are lower and wetter than adjacent ridges;
- steep exposed slopes favor rock/scree, while valley floors favor soil,
  meadow, or forest potential;
- the terrain remains readable with detail noise disabled.

Current implementation notes:

- a coherent height/driver field is routed first; channel influence and channel
  distance are derived from flow instead of an authored watershed guide;
- visible water uses a mild downstream base-level grade, longest-path trunk
  extraction, sparse tributary attachment, component pruning, and light widening
  so rivers read as main stems rather than scattered stream fragments;
- a small slope-relaxation pass smooths harsh process artifacts while preserving
  structure/process/detail contribution accounting;
- tests compare channel samples against non-channel terrain and divide samples
  against channels, and require the visible wet component to span a long trunk;
- `--terrain-lab-slice temperate-mountain-rivers` is the canonical wet river
  reference; the old `temperate-mountain-watershed` name remains only as a
  compatibility alias.
- remaining work is a richer river driver with better stream connectivity,
  side-channel structure, and slice-specific controls.

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
- feature-graph and drainage-region PNG smokes cover the default terrain payload
  without pinning an authored watershed slice;
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

## Phase 6: River/Drainage Network Reference

Status: active.

Goal: make rivers the shared drainage abstraction before continuing canyon
styling.

Current blocker: the arid canyon slice still reads weaker than the temperate
river reference because canyon features are too much of a slice-local skeleton.
The next correction is to make the river hierarchy own the visible dry wash,
canyon floor, wall, rim, and talus fields.

Deliverables:

- discharge proxy from runoff-weighted contributing area;
- stream order;
- river/channel width and valley width fields;
- slice-specific water presence;
- debug views for river network, width, and water presence;
- tests proving stream hierarchy, width variation, and wet/dry slice behavior.

Success criteria:

- temperate mountain rivers read as a river/drainage reference without needing a
  separate canyon interpretation;
- higher-order/discharge channels are wider than tributaries on average;
- arid canyon reuses the river hierarchy as dry washes with zero water presence;
- dunes and alpine remain valid sentinel slices without becoming water-centric.

## Phase 7: Additional Terrain Slices

Status: active as sentinel slices for model pressure.

Pivot note: slices should now prove that coherent source fields can drive the
terrain. Procedural randomness remains valid, but isolated authored features
should be treated as temporary scaffolding. A slice passes this phase when its
main landforms can be inspected through named driver fields before feature masks,
height, materials, and detail are applied.

Current blocker: mountain ridges/peaks should own a mountain-range driver,
while alpine glacial valley should own a valley-first driver with mountains as
contrast. The mountain driver should prove broad uplift, rolling ridges, peaks,
cliffs, shoulders, and scree. The glacial driver should prove a coherent
noise-warped trough, flanking mountain walls, headwall/cirque support, ice,
moraine/deposition, and process response. Sharing one source between these
slices hides the question each slice is supposed to answer.

Mountain-source rule: the driver must first read as mountain terrain before any
ridge, cliff, material, or glacial dressing is applied. A valid local mountain
patch can contain rolling ridges, broad uplift, and several peak supports, but
should not start from a corner/quadrant mass or a single hand-authored range
line that downstream features merely decorate.

Glacial-source rule: the glacial slice should start from valley geometry, not
from a general mountain range with a valley mask over it. Its source fields
should place valley/channel samples near a coherent trough and use ridges,
peaks, cliffs, and scree as wall and headwall contrast around that trough. The
trough should remain open along the downstream axis rather than becoming a
closed basin/rim shape.

Driver note: new slice work should normally start by identifying the missing
source, feature-driver, or process-modifier vocabulary it needs. A biome should
compose drivers such as ridges, rivers, basins/plains, dunes, glacial support,
and material response rather than invent a complete slice-local shaping stack.

Mask rule: for sentinel slices, the local patch is the biome boundary. Driver
selection should be full-patch unless the slice explicitly tests a physical
boundary. Internal masks should encode terrain/process state, not crop the demo.

Candidate slices:

- arid mesa canyon: strata, cliffs, dry washes, talus, sparse vegetation;
- alpine glacial valley: U-shaped valleys, moraine/deposition, snow and ice
  fields;
- mountain ridges/peaks: broad range support, ridges, crests, peaks, cliffs,
  scree, and rock/snow material response without valley-process dominance;
- dunes: wind direction, slip faces, interdune flats, sparse plants;
- volcanic terrain: cones, lava flows, ash fields, rough basalt;
- wetland: low relief, saturated soil, channels, reeds, ponds;
- coastal reconnection: beach, bluff, estuary, bathymetry handoff to
  `procedural_terrain` and `ocean`.

Each slice should add only the feature/process vocabulary needed for that
terrain type. Shared abstractions should wait until at least two slices need the
same mechanism.

Current implementation notes:

- temperate mountain rivers are the default slice while the river hierarchy is
  being established;
- arid mesa canyon remains the first dry/incised consumer of river fields;
- generated fields include mesa/rim/divide influence, dry canyon and side-wash
  channels, low wetness, no snow, rock/scree/soil-heavy material masks, sparse
  scrub density, and very low tree density;
- desert dunes use explicit wind/sand/relief driver fields instead of locally
  authored dune streaks;
- arid canyon routing should reuse shared river hierarchy and derive dry wash,
  canyon floor, wall width, and incision from discharge/order/slope/resistance;
- alpine glacial valley consumes its own valley-first source fields before
  feature masks and glacial response;
- mountain ridges/peaks consumes a separate mountain-range driver as a
  hydro-light landform sentinel for ridge/crest/peak/cliff/scree structure;
- hydrology is now structure-first but still avoids a toy erosion solver;
- the material vocabulary now includes sand for wind-shaped terrain, while snow
  continues to stand in for snow/ice in the glacial sentinel.

Success criteria:

- dunes remain readable with flow ignored as a shaping force;
- dune crests, slip faces, and interdune flats are derived from inspectable
  driver fields rather than hand-placed streaks or lines;
- driver debug views do not expose centered disks, ellipses, quadrants, or
  other artificial slice footprints;
- arid channels read as a canyon system with trunk and tributary structure, not
  one regular centerline across the patch;
- glacial terrain reads through valley shape, snow/ice, and deposition rather
  than river carving;
- glacial valley channels sit inside a coherent trough while ridges and peaks
  frame the trough as mountain-wall contrast;
- glacial driver fields expose a longitudinal valley path, not the same broad
  range silhouette as the mountain ridge slice;
- mountain ridge terrain reads through broad range support, distributed ridges,
  peaks, cliffs, and scree rather than glacial valley dressing;
- mountain high relief is distributed by source fields across the patch instead
  of being concentrated in one corner/quadrant;
- shared fields remain useful across all five sentinels without becoming
  water-centric.

## Phase 8: Integration Adapters

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

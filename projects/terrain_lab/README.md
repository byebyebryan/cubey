# Terrain Lab

`terrain_lab` is the standalone terrain generation workbench for Cubey. It
exists because terrain is large enough to deserve its own project boundary
instead of living only as a shoreline helper, a planet subroutine, or a shader
detail stack.

The project starts with deterministic CPU terrain fields and a small Vulkan
workbench. The immediate goal is to implement narrow biome and landform slices
that can be tested, inspected visually, and reused by other projects later.

## Direction

Terrain Lab focuses on coherent local terrain. The default scale should be a
multi-kilometer tangent-plane region where ridges, basins, drainage, slopes,
materials, and biome masks agree with each other before shader noise or foliage
is added.

The core rule is:

```text
region intent -> coherent driver fields -> feature masks -> process pass -> material and biome fields -> render detail
```

Procedural noise and randomness are still useful, but they should feed coherent
driver fields rather than hand-authored local features. High-frequency noise is
only a detail layer controlled by larger terrain fields. The terrain should
remain legible when that detail layer is disabled.

For biome and landform sentinel slices, the test patch itself is the slice
boundary. Driver selection should not add a centered disk, ellipse, quadrant
layout, or other demo footprint. Internal masks are valid when they describe
terrain state or process, such as snow, ice, wetness, talus, sand supply,
vegetation density, channels, or deposition.

See [design.md](design.md) for the generation model,
[river_research.md](river_research.md) for the river-network pivot, and
[roadmap.md](roadmap.md) for the planned slice sequence.

## Project Role

Terrain Lab should stay distinct from the existing terrain-adjacent projects:

| Project | Role |
| --- | --- |
| `terrain_lab` | Local terrain generation R&D, biome and landform slices, deterministic field vocabulary, debug rendering. |
| `procedural_terrain` | Coastal terrain, bathymetry, shoreline signed distance, and the terrain-ocean data contract. |
| `planet` | Planet-scale frame, cube-sphere LOD, local-detail host, sky/celestial state, and eventual integration target. |
| `ocean` | Water surface rendering that should consume terrain, bathymetry, and shoreline fields rather than own them. |
| `atmosphere` | Shared sky, lighting, and environment backdrop that Terrain Lab can use without depending on planet scope. |

## Goals

- Build terrain from coherent region structure: ridges, basins, river/drainage
  networks, strata, climate, and material response.
- Work slice by slice, starting with one terrain type that can be made credible
  before broadening to more biomes.
- Keep deterministic CPU/reference generation available for tests, summaries,
  and debug views even when the renderer later uses GPU displacement.
- Export explicit fields that other projects can consume: height, normal,
  source drivers, slope, curvature, flow, river hierarchy, wetness,
  sediment/deposition, material masks, and future vegetation density.
- Keep local and planet-scale concerns separate until the local terrain
  contract is strong enough to adapt into `planet` local detail or streaming.

## Non-Goals For The First Implementation

- Full planet curvature, global climate, or out-of-core streaming.
- A complete vegetation renderer with asset LOD, impostors, wind, and culling.
- Real-world GIS ingestion or satellite terrain.
- Ocean rendering, surf, and shoreline interaction beyond field compatibility.
- A physically complete erosion simulator. The first passes should be
  process-informed and testable before they become heavy simulations.

## Active Target Slice

The active target is now a temperate mountain river/watershed reference:

- 4-16 km local region.
- River/drainage network generated as shared terrain structure before canyon,
  wetland, coast, or glacial variants consume it.
- Discharge, stream order, channel width, valley width, and water presence
  derived from flow, runoff, slope, and slice climate.
- Wetness, deposition, material response, and vegetation fields driven by the
  river hierarchy rather than by a generic channel mask alone.
- Simple static water presence is allowed for visual readability, but animated
  water rendering remains out of scope.
- Grass, shrub, tree, and canopy density fields remain data outputs, not a full
  foliage renderer.
- Headless and windowed debug views for height, slope, flow, feature graph,
  river hierarchy, material, and vegetation-density fields.

The arid mesa canyon remains available with
`--terrain-lab-slice arid-mesa-canyon`. It should consume the same river fields
as a dry, high-incision expression: water presence should be zero, while
discharge/order drive dry-wash width, canyon floors, walls, rims, and talus.
Coastal work can reconnect later through `procedural_terrain` once the general
terrain field model is stronger.

Two representative sentinel slices are also available:

- `--terrain-lab-slice desert-dunes`: wind-shaped dune crests, slip faces,
  interdune flats, sand-dominant materials, sparse shrubs, and diagnostic-only
  flow fields. This is the first slice being migrated to explicit coherent
  driver fields instead of locally authored dune streaks.
- `--terrain-lab-slice alpine-glacial-valley`: U-shaped trunk valley, high
  ridges, cirque/headwall influence, moraine/deposition bands, snow/ice masks,
  and moderate meltwater diagnostics. It now exports explicit base, relief, and
  process drivers so the driver view can explain the valley and ridge layout
  without relying on a post-hoc fallback.

Hydrology is now a first-class terrain structure, but still not a heavy erosion
solver. Flow direction, accumulation, stream power, discharge proxy, stream
order, channel width, water presence, and sink diagnostics should expose whether
terrain is coherent before any droplet or destructive hydraulic erosion pass is
considered.

## Current Workbench

`TerrainLabConfig` reads common grid width, grid height, slice-preset, and
camera-preset, and debug-view settings from `RunConfig`, while leaving
coast-oriented `terrain.*` flags to `procedural_terrain`.

Generated CPU fields include:

- driver base, relief, process, and selection fields;
- height;
- structure, process, and detail height contributions;
- slope and curvature;
- flow direction, flow accumulation, and stream power;
- river discharge, stream order, river width, valley width, and water presence;
- wetness and deposition;
- ridge, valley, and basin influence fields;
- watershed id, divide influence, channel influence, and channel distance;
- material masks for rock, soil, scree, meadow, forest, and snow;
- sand material masks for slices that need explicit wind-blown sediment;
- grass, shrub, tree, and canopy-height density fields.

Field summaries also expose naturalization diagnostics for channel/divide
sample counts, divide-channel height separation, channel-flow alignment,
material entropy, and boundary edge steps. These are intended as iteration
guardrails for terrain shaping. Some checks are more meaningful for the
watershed fixture than for the arid default, where the divide field marks mesa
rims and interfluves rather than basin ownership.

The renderer converts those fields into a CPU heightfield mesh and packs the
main diagnostic payloads into vertex attributes. The current standalone app
supports windowed orbit inspection, `D` to cycle debug views, and headless PNG
captures for final and diagnostic views.

Supported debug views:

- `final`
- `height`
- `structure`
- `process`
- `detail`
- `slope`
- `curvature`
- `flow-direction`
- `flow-accumulation`
- `stream-power`
- `river-network`
- `river-width`
- `water-presence`
- `wetness`
- `deposition`
- `material`
- `biome-density`
- `canopy-height`
- `noise-off`
- `driver`
- `feature-graph`
- `watershed`
- `channel`
- `divide`

Useful validation commands:

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_project_terrain_lab cubey_project_terrain_lab_tests cubey_png_stats
ctest --preset dev -R terrain_lab --output-on-failure
```

Useful run commands:

```sh
./build/dev/projects/terrain_lab/terrain_lab --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --debug-view river-network --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --debug-view river-width --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --debug-view water-presence --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --debug-view flow-accumulation --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --debug-view feature-graph --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --debug-view driver --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --terrain-lab-slice temperate-mountain-watershed --debug-view watershed --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --terrain-lab-slice desert-dunes --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --terrain-lab-slice alpine-glacial-valley --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --headless --grid-width 65 --grid-height 65 --output /tmp/cubey-terrain-lab.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view material --grid-width 65 --grid-height 65 --output /tmp/cubey-terrain-lab-material.png
```

Useful local review output commands:

```sh
mkdir -p outputs/terrain_lab
./build/dev/projects/terrain_lab/terrain_lab --headless --width 1280 --height 720 --output outputs/terrain_lab/final.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view river-network --width 1280 --height 720 --output outputs/terrain_lab/river-network.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view river-width --width 1280 --height 720 --output outputs/terrain_lab/river-width.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view water-presence --width 1280 --height 720 --output outputs/terrain_lab/water-presence.png
./build/dev/projects/terrain_lab/terrain_lab --headless --terrain-lab-camera-preset profile --width 1280 --height 720 --output outputs/terrain_lab/final-profile.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view feature-graph --width 1280 --height 720 --output outputs/terrain_lab/feature-graph.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view channel --width 1280 --height 720 --output outputs/terrain_lab/channel.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view material --width 1280 --height 720 --output outputs/terrain_lab/material.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view biome-density --width 1280 --height 720 --output outputs/terrain_lab/biome-density.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view slope --width 1280 --height 720 --output outputs/terrain_lab/slope.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view flow-accumulation --width 1280 --height 720 --output outputs/terrain_lab/flow-accumulation.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view noise-off --width 1280 --height 720 --output outputs/terrain_lab/noise-off.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view driver --width 1280 --height 720 --output outputs/terrain_lab/driver.png
./build/dev/projects/terrain_lab/terrain_lab --headless --terrain-lab-slice temperate-mountain-watershed --debug-view watershed --width 1280 --height 720 --output outputs/terrain_lab/watershed-temperate.png
./build/dev/projects/terrain_lab/terrain_lab --headless --terrain-lab-slice desert-dunes --width 1280 --height 720 --output outputs/terrain_lab/desert-dunes.png
./build/dev/projects/terrain_lab/terrain_lab --headless --terrain-lab-slice desert-dunes --debug-view material --width 1280 --height 720 --output outputs/terrain_lab/desert-dunes-material.png
./build/dev/projects/terrain_lab/terrain_lab --headless --terrain-lab-slice desert-dunes --debug-view driver --width 1280 --height 720 --output outputs/terrain_lab/desert-dunes-driver.png
./build/dev/projects/terrain_lab/terrain_lab --headless --terrain-lab-slice alpine-glacial-valley --width 1280 --height 720 --output outputs/terrain_lab/alpine-glacial-valley.png
./build/dev/projects/terrain_lab/terrain_lab --headless --terrain-lab-slice alpine-glacial-valley --debug-view material --width 1280 --height 720 --output outputs/terrain_lab/alpine-glacial-valley-material.png
./build/dev/projects/terrain_lab/terrain_lab --headless --terrain-lab-slice alpine-glacial-valley --debug-view driver --width 1280 --height 720 --output outputs/terrain_lab/alpine-glacial-valley-driver.png
```

`outputs/` is ignored by Git; these captures are local review artifacts.

## Status

Current status: CPU field foundation plus a minimal standalone visual workbench.
The default generator now targets a temperate mountain river/watershed reference
so drainage hierarchy can mature before canyon-specific styling. The arid mesa
canyon, desert dunes, and alpine glacial valley sentinels remain available as
explicit slice fixtures. The
workbench has deterministic mesh extraction, field/debug shading, windowed and
headless render paths, PNG smoke coverage, shader/debug-view sync tests, and
analysis-only drainage guardrails.

Still out of scope for the current slice: live ImGui editing, runtime
regeneration, particle hydraulic erosion, meander simulation, lakes, animated
water rendering, tiled or clipmap terrain rendering, full foliage assets,
atmosphere-backed lighting, and adapters into `planet`, `ocean`, or
`procedural_terrain`.

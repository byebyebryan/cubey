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
region intent -> feature graph -> scalar fields -> process pass -> material and biome fields -> render detail
```

High-frequency procedural noise is still useful, but only as a detail layer
controlled by larger terrain fields. The terrain should remain legible when
that detail layer is disabled.

See [design.md](design.md) for the generation model and [roadmap.md](roadmap.md)
for the planned slice sequence.

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

- Build terrain from coherent region structure: ridges, basins, watersheds,
  strata, climate, and material response.
- Work slice by slice, starting with one terrain type that can be made credible
  before broadening to more biomes.
- Keep deterministic CPU/reference generation available for tests, summaries,
  and debug views even when the renderer later uses GPU displacement.
- Export explicit fields that other projects can consume: height, normal,
  slope, curvature, flow, wetness, sediment/deposition, material masks, and
  future vegetation density.
- Keep local and planet-scale concerns separate until the local terrain
  contract is strong enough to adapt into `planet` local detail or streaming.

## Non-Goals For The First Implementation

- Full planet curvature, global climate, or out-of-core streaming.
- A complete vegetation renderer with asset LOD, impostors, wind, and culling.
- Real-world GIS ingestion or satellite terrain.
- Ocean rendering, surf, and shoreline interaction beyond field compatibility.
- A physically complete erosion simulator. The first passes should be
  process-informed and testable before they become heavy simulations.

## First Target Slice

The first implementation slice should be a temperate mountain watershed:

- 4-16 km local region.
- Ridge and basin structure generated before detail noise.
- Drainage network, flow accumulation, valley floors, and wetness fields.
- Slope-driven rock, soil, scree, meadow, and snow/altitude masks.
- Grass, shrub, and tree density fields as data outputs, not a full foliage
  renderer.
- Headless and windowed debug views for height, slope, flow, watershed,
  material, and vegetation-density fields.

This slice is intentionally inland. Coastal work can reconnect later through
`procedural_terrain` once the general terrain field model is stronger.

## Current Workbench

`TerrainLabConfig` reads common grid width, grid height, and debug-view settings
from `RunConfig`, while leaving coast-oriented `terrain.*` flags to
`procedural_terrain`.

Generated CPU fields include:

- height;
- structure, process, and detail height contributions;
- slope and curvature;
- flow direction, flow accumulation, and stream power;
- wetness and deposition;
- ridge, valley, and basin influence fields;
- watershed id, divide influence, channel influence, and channel distance;
- material masks for rock, soil, scree, meadow, forest, and snow;
- grass, shrub, tree, and canopy-height density fields.

Field summaries also expose naturalization diagnostics for channel/divide
sample counts, divide-channel height separation, channel-flow alignment,
material entropy, and boundary edge steps. These are intended as iteration
guardrails for terrain shaping, not as a claim that the slice is physically
complete.

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
- `wetness`
- `deposition`
- `material`
- `biome-density`
- `canopy-height`
- `noise-off`
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
./build/dev/projects/terrain_lab/terrain_lab --debug-view flow-accumulation --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --debug-view feature-graph --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --debug-view watershed --frames 300 --width 1280 --height 720
./build/dev/projects/terrain_lab/terrain_lab --headless --grid-width 65 --grid-height 65 --output /tmp/cubey-terrain-lab.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view material --grid-width 65 --grid-height 65 --output /tmp/cubey-terrain-lab-material.png
```

Useful local review output commands:

```sh
mkdir -p outputs/terrain_lab
./build/dev/projects/terrain_lab/terrain_lab --headless --width 1280 --height 720 --output outputs/terrain_lab/final.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view feature-graph --width 1280 --height 720 --output outputs/terrain_lab/feature-graph.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view watershed --width 1280 --height 720 --output outputs/terrain_lab/watershed.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view channel --width 1280 --height 720 --output outputs/terrain_lab/channel.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view divide --width 1280 --height 720 --output outputs/terrain_lab/divide.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view material --width 1280 --height 720 --output outputs/terrain_lab/material.png
./build/dev/projects/terrain_lab/terrain_lab --headless --debug-view flow-accumulation --width 1280 --height 720 --output outputs/terrain_lab/flow-accumulation.png
```

`outputs/` is ignored by Git; these captures are local review artifacts.

## Status

Current status: CPU field foundation plus a minimal standalone visual workbench.
The generator includes a deterministic four-basin watershed core with divide,
channel, and watershed-id fields. The latest pass softens hard watershed
ownership, derives stronger channels from initial flow accumulation, applies a
small slope-relaxation process, and breaks up material masks with semantic
noise. Channels drive valley incision, wetness, and deposition, while divide
fields keep high-ground structure explicit without dominating the final height.
The workbench has deterministic mesh extraction, field/debug shading, windowed
and headless render paths, PNG smoke coverage, and shader/debug-view sync tests.

Still out of scope for the current slice: live ImGui editing, runtime
regeneration, a true drainage graph with stream connectivity guarantees, tiled
or clipmap terrain rendering, foliage/proxy dressing, atmosphere-backed
lighting, and adapters into `planet`, `ocean`, or `procedural_terrain`.

# Terrain Lab

`terrain_lab` is the standalone terrain generation workbench for Cubey. It
exists because terrain is large enough to deserve its own project boundary
instead of living only as a shoreline helper, a planet subroutine, or a shader
detail stack.

The project starts docs-first. The immediate goal is to define the terrain
direction, then implement narrow biome and landform slices that can be tested,
rendered, and reused by other projects later.

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

## Status

Current status: docs only. No CMake target is registered yet.

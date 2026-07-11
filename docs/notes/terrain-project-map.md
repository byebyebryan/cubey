# Terrain Project Map

Date: 2026-07-10

This map records the terrain reboot boundaries. Older river, mountain-driver,
and landscape-evolution notes remain historical evidence rather than an active
queue.

## Active Projects

| Project | Role | Change policy |
| --- | --- | --- |
| `projects/terrain` | Terrain v1 source runtime, clipmap renderer, traversal, and review. | Active. Keep one parameterized source model and a small public contract. |
| `projects/terrain_ref` | TerrainEngine and curated ShaderToy visual controls. | Frozen. Maintenance and reproducibility fixes only. |
| `projects/terrain_hydrology_lab` | Previous CPU patch, drainage diagnostics, graph routing, and analytical landscape evolution. | Paused. Preserve build/tests; do not feed terrain v1. |
| `projects/terrain_workbench_legacy` | First river/mountain driver workbench. | Legacy. |
| `projects/procedural_terrain_legacy` | Earlier coast/island terrain project. | Legacy. |
| `projects/terrain_lab_legacy` | Earlier biome/noise lab. | Legacy. |

## V1 Spine

```text
shared coherent noise
    -> project-local parameterized source
    -> CPU point query + matching GLSL sample
    -> optional local weathering
    -> camera-centered clipmap
    -> procedural material + shared atmosphere
    -> diagnostic and scenic review
```

This is intentionally not the old source/process/product field spine. Terrain
v1 is a random-access heightfield runtime. Regional simulation products can be
added later through explicit adapters; they do not define this source contract.

## Source And Presets

`mountain`, `upland`, and `plains` are parameter sets over the same
macro/structure/detail evaluator. The TerrainEngine reference is the visual
anchor because it reached readable mountain shapes with a compact octave stack
and elevation power. Cubey keeps that hierarchy while using the shared noise
foundation and explicit physical scales.

The model must remain coherent at every stage. Do not add local ridgeline,
valley, coast, lake, or river masks to force a target composition. Additional
terrain categories come later only if they can be expressed as parameters or a
well-defined new source/operator class.

## Process Boundary

V1 local weathering is a bounded stateless modifier. It can sharpen or soften
resolved surface detail and publish its signed delta, but it cannot provide
drainage topology. D8, D-infinity, graph rivers, priority filling, stream-power
evolution, and particle hydrology belong in `terrain_hydrology_lab` or a future
hydrology reboot.

## Consumer Boundary

The standalone app is the only v1 integration target. Its project-local runtime
is structured for reuse, but glTF, fluid, ocean, and planet adapters are deferred
until the terrain contract and traversal evidence are stable. A second consumer
is the gate for considering promotion into `include/cubey` and `src/cubey`.

## Review Contract

Review always includes:

- neutral top views across three seeds;
- clean versus weathered height comparisons;
- oblique and near-surface presentation views;
- LOD diagnostics and moving-camera inspection;
- a side-by-side TerrainEngine reference control;
- bounded source statistics and capture metadata.

Material shading cannot be the only evidence. The height and slope views must
show the same macro hierarchy, and outputs must be grouped by checkpoint rather
than accumulated as an undifferentiated image dump.

## Deferred Work

- hydrology, rivers, lakes, wetlands, and coastlines;
- biome/climate/material products and foliage placement;
- terrain deformation, persistence, colliders, and offline baking;
- cross-project adapters;
- spherical planet mapping, floating origin, and planet-scale streaming;
- terrain cast shadows and final production material systems.

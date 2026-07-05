# TerrainEngine Reference Port Plan

Date: 2026-07-05

This note defines the first TerrainEngine reference lane for the terrain reboot.
The goal is not to absorb the whole `/home/bryan/code/ref/TerrainEngine-OpenGL`
application. It is to create a known-good visual midpoint inside
`projects/terrain` that we can compare against while the current river and
mountain process experiments remain available for diagnostics.

## Source

Reference repository:

- `/home/bryan/code/ref/TerrainEngine-OpenGL`
- License: MIT, copyright Federico Vaccaro
- Terrain owner files:
  - `DrawableObjects/Terrain.cpp`
  - `DrawableObjects/Terrain.h`
  - `shaders/terrain.tcs`
  - `shaders/terrain.tes`
  - `shaders/terrain.frag`

TerrainEngine is mainly a renderer-side terrain implementation. It draws a
moving grid of tessellated terrain tiles and samples height directly in GLSL.
The first port should therefore map its height and material recipe into Cubey's
CPU terrain product contract, not port the OpenGL draw stack.

## What To Port First

Add an isolated recipe named `terrain-engine-ref`.

The recipe should:

- sample the TerrainEngine height function using the same broad controls:
  `octaves = 13`, `freq = 0.01`, `gDispFactor = 20`, `power = 3`, persistence
  `0.5`, and the `mat2(0.8, -0.6, 0.6, 0.8)` octave rotation;
- preserve the reference character: low-frequency height field, cubic height
  shaping, finite-difference slope, and slope/elevation material separation;
- emit the existing terrain product fields so the preview and debug export
  systems work unchanged;
- keep river, gully, talus, and current mountain-driver process fields inactive
  for this recipe;
- use current `terrain_preview` mesh captures for review.

The first implementation can map TerrainEngine material output onto Cubey's
existing `material_soil`, `material_grass`, and `material_rock` fields. Sand is
represented by `material_soil`.

## What Not To Port Yet

Do not port these pieces in the first batch:

- TerrainEngine's app shell, windowing, camera, ImGui controls, or resource
  loader;
- OpenGL tessellation shader stages as runtime infrastructure;
- water, clouds, forest, atmosphere, stars, or fog from the reference app;
- TerrainEngine texture assets into Cubey shipping assets;
- a new terrain LOD or clipmap runtime.

Those are useful study material, but they are separate from the first reference
height/material lane.

## Acceptance

The implementation batch should leave us with:

- a deterministic `terrain-engine-ref` recipe;
- tests proving the recipe is isolated from current river carving and process
  diagnostics;
- scalar debug captures under `outputs/terrain/terrain-engine-ref`;
- at least one renderer-backed oblique preview and one surface preview;
- docs explaining how this reference differs from the current process-heavy
  mountain/rivers workbench.

If the reference lane looks better than the current mountain process from common
review angles, use it as a visual benchmark before adding more terrain process
complexity. If it looks worse, keep it as a compact baseline for what direct
shader-side sampling buys and does not buy.

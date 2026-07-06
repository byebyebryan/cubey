# TerrainEngine Reference Port Plan

Date: 2026-07-05

This note defines the first TerrainEngine reference lane for the terrain reboot.
The goal is not to absorb the whole `/home/bryan/code/ref/TerrainEngine-OpenGL`
application. It is to create a known-good visual midpoint inside
`projects/terrain_ref` that we can compare against while the legacy river and
mountain process experiments remain available for diagnostics in
`projects/terrain_workbench_legacy`.

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
The first port mapped its height and material recipe into Cubey's CPU terrain
product contract. The next port should add a renderer-backed runtime reference
mode that samples the same function in GLSL over a view-scale grid, without
turning TerrainEngine's OpenGL draw stack into Cubey runtime infrastructure.

## Capability Review

TerrainEngine is useful, but its terrain scope is narrower than the project name
suggests. The pieces below are the current read from
`/home/bryan/code/ref/TerrainEngine-OpenGL`.

### Terrain Shape

TerrainEngine has one procedural terrain model, not a biome or terrain-type
system. Shape comes from shader-side value-noise FBM with octave rotation and a
cubic height shaping step. GUI controls expose parameters such as octaves,
frequency, displacement, tessellation multiplier, grass coverage, and power, but
there is no registry of desert, alpine, plains, coast, or other biome recipes.

The useful lesson is the coherent source model: the same deterministic
world-coordinate height function drives tessellation, normals, material zoning,
and water intersection. That is why the result reads more coherent than a
sequence of independently pasted process masks.

### Tessellated Rendering

The tessellation path is likely the most important unported TerrainEngine idea:

- CPU geometry is a low-resolution tiled plane. `Terrain::tileW` is `1000`,
  `res` is `4`, and each tile is drawn as instanced patches.
- The vertex shader places the patch and adds the per-tile offset.
- The tessellation control shader computes camera distance to patch corners and
  chooses distance bands of roughly `60`, `30`, `10`, `2.5`, then `1`.
- The tessellation evaluation shader interpolates the tessellated vertex,
  samples the procedural height function in GLSL, and displaces the vertex.
- `Terrain::updateTilesPositions()` recenters the tile grid around the camera,
  giving the impression of an endless terrain plane.

This is not a full terrain runtime, paging system, or clipmap. It is still a
single procedural height function over repeated view-centered tiles. However, it
is a strong runtime direction for Cubey: keep CPU terrain products small and
debuggable, but sample terrain source/detail view-dependently for scene views
instead of baking a giant CPU mesh.

### Water

Water is a renderer feature, not a terrain/hydrology product. TerrainEngine
creates one horizontal water plane at a global height. Shorelines and apparent
lake shapes come from intersecting that plane with the procedural heightfield.

The water renderer is still useful as a reference:

- render a mirrored camera pass into a reflection FBO;
- render clipped terrain below the water plane into a refraction FBO;
- draw the water plane after terrain;
- distort reflection/refraction coordinates with DUDV/noise;
- use Fresnel, procedural normals, foam hints, fog, and depth-based alpha.

This should inform a future Cubey water presentation pass. It does not solve
lake generation, basin filling, per-lake water levels, river-fed water bodies,
or coast/ocean topology.

### Materials And Biomes

TerrainEngine has material zoning rather than biomes. The terrain shader blends
sand near water height, grass on flatter surfaces, and rock on steep surfaces.
There is snow texture support in the assets and shader, but the snow branch is
commented out in the terrain fragment shader.

For Cubey, this is a good compact material-rule baseline, not a biome system.
Actual biome work should still come from explicit recipe/source/process choices:
mountain, river, lake, coast, dune, plain, alpine, etc.

### Hydrology And Erosion

TerrainEngine does not implement hydraulic erosion. There is no rainfall,
drainage routing, flow accumulation, sediment transport, deposition, river
incision, or basin fill. Water does not carve the terrain; it is rendered on top
of the terrain.

This is an important correction to our earlier instinct. TerrainEngine looks
better than the current process-heavy mountain pass without hydro erosion, which
suggests our next terrain quality gains should first come from coherent source
functions, view-dependent rendering, material detail, and simpler diagnostic
processes. Hydro should remain a targeted process lane, not the default answer
to every terrain-shape problem.

### Grass, Trees, And Foliage

TerrainEngine does not ship procedural grass blades, trees, forests, bushes, or
foliage placement. It uses grass textures and a `grassCoverage` material control.
The README mentions procedural grass as future work, not implemented behavior.

So it is not a foliage reference. For Cubey, vegetation still needs its own
placement/rendering plan, likely driven by terrain fields such as slope,
wetness, material, elevation, and biome.

## Reuse Map

Borrow or keep:

- coherent world-coordinate shader height sampling;
- distance-adaptive tessellated terrain patches;
- view-centered tiled terrain rendering;
- finite-difference normals from the same height source;
- slope/elevation/water-height material rules;
- water reflection/refraction/depth-fade presentation.

Do not treat TerrainEngine as a reference for:

- biome architecture;
- hydraulic erosion;
- river/lake generation;
- terrain product field contracts;
- vegetation or forest rendering;
- large-world terrain streaming beyond simple recentered tiles.

## Completed First Port

Revision 34 added an isolated recipe named `terrain-engine-ref`.

The recipe:

- sample the TerrainEngine height function using the same broad controls:
  `octaves = 13`, `freq = 0.01`, `gDispFactor = 20`, `power = 3`, persistence
  `0.5`, and the `mat2(0.8, -0.6, 0.6, 0.8)` octave rotation;
- preserve the reference character: low-frequency height field, cubic height
  shaping, finite-difference slope, and slope/elevation material separation;
- emit the existing terrain product fields so the preview and debug export
  systems work unchanged;
- keep river, gully, talus, and current mountain-driver process fields inactive
  for this recipe;
- uses current `terrain_preview` mesh captures for CPU-product review.

The implementation maps TerrainEngine material output onto Cubey's existing
`material_soil`, `material_grass`, and `material_rock` fields. Sand is
represented by `material_soil`.

## Runtime Reference Step

Add a `terrain_ref` runtime mode for `terrain-engine-ref`.

The mode should:

- build a camera-scale clipmap mesh from Cubey's existing
  `clipmap_grid_2d` helper;
- keep vertex height sampling in GLSL using the same TerrainEngine-inspired
  function as the CPU recipe;
- pass deterministic seed components to the shader instead of baking a
  per-vertex heightfield;
- color terrain from the same water-height, slope, grass, soil, and rock rules
  used by the CPU recipe;
- optionally clamp sub-water vertices to the TerrainEngine reference waterline
  as a first water-intersection preview;
- keep the existing CPU-product preview as the default mode.

This is not a finished terrain runtime. It is a known-good midpoint for checking
whether coherent shader-side source sampling, view-scale grids, and simple
material rules give a better visual baseline than the process-heavy debug mesh.

## Runtime Reference Status

The first runtime-reference implementation adds:

- `projects/terrain_ref`, gated to the `terrain-engine-ref` recipe;
- a Cubey `clipmap_grid_2d` review mesh with vertex height displaced in GLSL;
- shared C++/GLSL TerrainEngine-inspired height and material rules;
- `--terrain-water-surface` / `--no-terrain-water-surface` review toggles for
  the simple waterline clamp.

Known limitations:

- the clipmap is currently a finite review patch, so captures can expose hard
  outer edges;
- the mesh is regenerated as one static review mesh rather than streamed or
  recentered around a moving camera;
- water is a flat intersection tint/clamp, not a separate reflective,
  refractive water renderer;
- no biome, hydrology, foliage, or terrain-product paging is implied by this
  runtime reference mode.

## What Not To Port Yet

Do not port these pieces in the runtime-reference batch:

- TerrainEngine's app shell, windowing, camera, ImGui controls, or resource
  loader;
- OpenGL tessellation shader stages as runtime infrastructure;
- reflection/refraction water FBOs, clouds, forest, atmosphere, stars, or fog
  from the reference app;
- TerrainEngine texture assets into Cubey shipping assets;
- a full streaming terrain product, planet-scale terrain paging, or hydrology.

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

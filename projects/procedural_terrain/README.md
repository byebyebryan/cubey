# Procedural Terrain

`procedural_terrain` is the standalone terrain and bathymetry data demo for
future ocean shoreline work. It keeps the first terrain contract explicit:
positive-up terrain elevation, positive-down water depth, shoreline signed
distance, and material masks for sand, rock, vegetation, and sediment. The
default final view adds a visual sea-surface mesh over the same analytical
fields so shoreline work is legible before the ocean renderer is integrated.
The terrain-ocean data boundary is documented in
[`terrain_ocean_contract.md`](terrain_ocean_contract.md).
The app now exports that boundary through the shared
`cubey::render::TerrainOceanFieldView` contract, and its static 2D clipmap
diagnostics use the same shared patch and triangle-count helpers as
`projects/ocean`.

Run a windowed view:

```sh
./build/dev/projects/procedural_terrain/procedural_terrain --width 1280 --height 720
```

The windowed view opens an ImGui control panel for debug view selection, water
visibility, camera reset, applied terrain rebuilds, and field/mesh diagnostics.
Terrain shape edits are staged until `Apply Terrain` so large grids do not
regenerate on every slider tick.

Run a deterministic headless capture:

```sh
./build/dev/projects/procedural_terrain/procedural_terrain --headless --width 1280 --height 720 --output /tmp/cubey-procedural-terrain.png
```

Terrain tuning is available from both the UI and CLI:

```sh
./build/dev/projects/procedural_terrain/procedural_terrain --headless --terrain-seed 12345 --terrain-land-extent 0.64 --terrain-relief 1.35 --terrain-ridges 0.85 --terrain-valleys 0.45 --output /tmp/cubey-terrain-tuned.png
./build/dev/projects/procedural_terrain/procedural_terrain --headless --no-terrain-water-surface --output /tmp/cubey-terrain-land.png
```

Useful debug views:

```sh
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view height
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view water-depth
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view shoreline
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view material
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view slope
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view landform
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view ridges
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view valleys
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view flow-accumulation
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view stream-power
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view macro-height
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view base-noise
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view detail-noise
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view feature-height
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view relax-delta
```

Underscore spellings remain accepted as compatibility aliases.

Use `--grid-width` and `--grid-height` for lower-cost checks or denser local
captures. The app supports grids up to `2049 x 2049`; the default is
`1025 x 1025` at `1.5 m` cells, keeping the same physical footprint as the
earlier `513 x 513` terrain. Grid presets in the UI preserve physical extent by
adjusting cell size. The current generator computes fields on the CPU, including
shoreline distance, ridge strength, flow accumulation, stream power, named
height contributions, and a bounded relax delta, then uploads CPU terrain and
water meshes. GPU texture-backed displacement, chunked LOD, CDLOD, heavier
erosion, and ocean integration are follow-up work.

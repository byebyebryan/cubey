# Procedural Terrain

`procedural_terrain` is the standalone terrain and bathymetry data demo for
future ocean shoreline work. It keeps the first terrain contract explicit:
positive-up terrain elevation, positive-down water depth, shoreline signed
distance, and material masks for sand, rock, vegetation, and sediment. The
default final view adds a visual sea-surface mesh over the same analytical
fields so shoreline work is legible before the ocean renderer is integrated.

Run a windowed view:

```sh
./build/dev/projects/procedural_terrain/procedural_terrain --width 1280 --height 720
```

The windowed view opens an ImGui control panel for debug view selection, water
visibility, camera reset, and applied terrain rebuilds. Terrain shape edits are
staged until `Apply Terrain` so large grids do not regenerate on every slider
tick.

Run a deterministic headless capture:

```sh
./build/dev/projects/procedural_terrain/procedural_terrain --headless --width 1280 --height 720 --output /tmp/cubey-procedural-terrain.png
```

Useful debug views:

```sh
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view height
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view water_depth
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view shoreline
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view material
./build/dev/projects/procedural_terrain/procedural_terrain --debug-view slope
```

Use `--grid-width` and `--grid-height` for lower-cost checks or denser local
captures. The first implementation generates fields on the CPU and uploads CPU
terrain and water meshes; GPU texture-backed displacement, chunked LOD, CDLOD,
erosion, and ocean integration are follow-up work.

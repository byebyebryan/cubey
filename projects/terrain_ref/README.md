# Terrain Ref

`projects/terrain_ref` is the active visual reference lane for terrain. It is
not the production terrain system and it does not inherit the old river/mountain
workbench pipeline. The first reference model is the TerrainEngine-inspired
shader-side height sampler and TerrainEngine-style material presentation over a
Cubey clipmap review mesh.

The current target is useful for renderer and source-model evaluation:

- coherent world-space height sampled in GLSL;
- slope/elevation material zoning with selected TerrainEngine terrain textures;
- rock normal detail and distance-faded material texture contribution;
- directional lighting and lightweight distance/altitude fog;
- optional flat waterline intersection;
- oblique and surface review cameras;
- fast headless captures for comparison.

Known limitations are intentional for this slice: the review mesh is finite,
water is still a tint/clamp rather than reflection/refraction, texture uploads
use single-mip sampled images, and there is no biome, hydrology, foliage,
streaming, or planet-scale paging.

## Commands

```sh
cmake --build --preset dev --target cubey_project_terrain_ref cubey_project_terrain_ref_tests
ctest --preset dev -R terrain_ref --output-on-failure

mkdir -p outputs/terrain_ref/terrain-engine
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-camera-preset oblique --terrain-water-surface --output outputs/terrain_ref/terrain-engine/oblique-water.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-camera-preset oblique --no-terrain-water-surface --output outputs/terrain_ref/terrain-engine/oblique-dry.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-camera-preset surface-low --terrain-water-surface --output outputs/terrain_ref/terrain-engine/surface-low-water.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-camera-preset surface-low --no-terrain-water-surface --output outputs/terrain_ref/terrain-engine/surface-low-dry.png
```

Next rendering work should handle the parts deliberately deferred here: a real
water pass with scene color/depth inputs, mipmapped or generated material detail,
tessellation/LOD parity, and a less finite-looking view-centered mesh.

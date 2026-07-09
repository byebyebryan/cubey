# Terrain Ref

`projects/terrain_ref` is the active visual reference lane for terrain. It is
not the production terrain system and it does not inherit the old river/mountain
workbench pipeline. It currently carries these reference recipes over a Cubey
clipmap review mesh:

- `terrain-engine-ref`: a TerrainEngine-inspired shader-side height sampler and
  procedural material presentation.
- `shadertoy-mountain`: a clean-room ShaderToy-style mountain heightfield
  sampler with a matching procedural material response.
- `shadertoy-alpine`: a clean-room alpine range reference with broad mountain
  mass, ridged crests, valley suppression, snow, rock, and meadow bands.
- `shadertoy-dunes`: a clean-room desert dune reference with wind-aligned
  rolling ridges and procedural sand/ripple material response.
- `shadertoy-lake-basin`: a clean-room lake-basin reference with surrounding
  hills, a warped basin depression, waterline intersection, and shoreline cues.
- `shadertoy-badlands`: a clean-room arid badlands reference with plateau mass,
  dry washes, eroded cuts, cliff exposure, and strata-like material cues.
- `shadertoy-coast-island`: a clean-room coast/island reference with noisy
  shoreline, beach shelf, inland hill buildup, local coastal cliffs, and fixed
  sea-level contact.
- `shadertoy-plains`: a clean-room low-relief grassland reference with rolling
  broad terrain, shallow swales, and wind/grass material variation.
- `shadertoy-gorge`: a clean-room dry gorge reference with a warped incision
  corridor, tributary cuts, arid floors, cliff exposure, and strata cues.
- `shadertoy-glacial-highland`: a clean-room icy highland reference with broad
  snow/ice fields, U-shaped valley hints, rock ribs, and talus/ice contrast.
- `shadertoy-crater-field`: a clean-room cratered terrain reference with
  overlapping depressions, raised rims, ejecta roughness, and barren regolith
  material.

The current target is useful for renderer and source-model evaluation:

- coherent world-space height sampled in GLSL;
- procedural slope/elevation material zoning;
- neutral height material mode for source-shape comparison;
- distance-faded procedural albedo and normal detail;
- directional lighting and lightweight distance/altitude fog;
- optional flat waterline intersection;
- oblique and surface review cameras;
- fast headless captures for comparison.

Known limitations are intentional for this slice: the review mesh is finite,
water is still a tint/clamp rather than reflection/refraction, material detail
is shader-generated rather than authored texture data, the ShaderToy-inspired
recipe samples a heightfield rather than porting a raymarch renderer, and there
is no biome, hydrology, foliage, streaming, or planet-scale paging. The
ShaderToy recipes are visual/source references, not production biome contracts.

## Commands

```sh
cmake --build --preset dev --target cubey_project_terrain_ref cubey_project_terrain_ref_tests
ctest --preset dev -R terrain_ref --output-on-failure

mkdir -p outputs/terrain_ref/terrain-engine outputs/terrain_ref/shadertoy-mountain outputs/terrain_ref/shape-compare
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-camera-preset oblique --terrain-water-surface --output outputs/terrain_ref/terrain-engine/oblique-water.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-camera-preset oblique --no-terrain-water-surface --output outputs/terrain_ref/terrain-engine/oblique-dry.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-camera-preset surface-low --terrain-water-surface --output outputs/terrain_ref/terrain-engine/surface-low-water.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-camera-preset surface-low --no-terrain-water-surface --output outputs/terrain_ref/terrain-engine/surface-low-dry.png

./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-recipe shadertoy-mountain --terrain-camera-preset oblique --terrain-water-surface --output outputs/terrain_ref/shadertoy-mountain/oblique-water.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-recipe shadertoy-mountain --terrain-camera-preset oblique --no-terrain-water-surface --output outputs/terrain_ref/shadertoy-mountain/oblique-dry.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-recipe shadertoy-mountain --terrain-camera-preset surface-low --terrain-water-surface --output outputs/terrain_ref/shadertoy-mountain/surface-low-water.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-recipe shadertoy-mountain --terrain-camera-preset surface-low --no-terrain-water-surface --output outputs/terrain_ref/shadertoy-mountain/surface-low-dry.png

./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-camera-preset oblique --terrain-preview-color height --no-terrain-water-surface --output outputs/terrain_ref/shape-compare/terrain-engine-oblique-height.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-camera-preset surface-low --terrain-preview-color height --no-terrain-water-surface --output outputs/terrain_ref/shape-compare/terrain-engine-surface-low-height.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-recipe shadertoy-mountain --terrain-camera-preset oblique --terrain-preview-color height --no-terrain-water-surface --output outputs/terrain_ref/shape-compare/shadertoy-mountain-oblique-height.png
./build/dev/projects/terrain_ref/terrain_ref --headless --width 1280 --height 720 --terrain-recipe shadertoy-mountain --terrain-camera-preset surface-low --terrain-preview-color height --no-terrain-water-surface --output outputs/terrain_ref/shape-compare/shadertoy-mountain-surface-low-height.png

projects/terrain_ref/capture_shadertoy_biome_refs.sh
```

Next rendering work should handle the parts deliberately deferred here: a
ShaderToy-style rendering reference pass, a real water pass with scene
color/depth inputs, stronger shared atmosphere/lighting, tessellation/LOD
parity, and a less finite-looking view-centered mesh.

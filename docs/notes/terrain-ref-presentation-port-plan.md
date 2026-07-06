# Terrain Ref Presentation Port Plan

Date: 2026-07-06

`projects/terrain_ref` now has a clean TerrainEngine-inspired height source.
The first presentation pass tried copied TerrainEngine material textures, but
that is not the right default for Cubey: the images are not strong enough to
justify a vendored asset path, and this repo generally benefits more from
procedural material and renderer pieces that can be reused across projects.

The TerrainEngine screenshots remain useful, but mostly as rendering-context
targets: slope/elevation zoning, directional light, fog, water integration, and
tessellated terrain presentation. ShaderToy terrain examples are especially
relevant for the next pass because they usually package terrain source,
material, lighting, fog, and water together instead of treating rendering as a
separate afterthought.

## Direction

This batch keeps the terrain surface self-contained and procedural. Use
`/home/bryan/code/ref/TerrainEngine-OpenGL` as a visual donor for:

- sand, grass, rock, and optional snow material bands;
- slope/elevation-based material selection;
- procedural normal detail on steep surfaces;
- stronger directional lighting and distance/altitude fog;
- repeatable oblique and surface captures for comparison.

Keep this Cubey-native:

- do not vendor TerrainEngine material images;
- generate material variation and normal detail in shaders;
- keep the TerrainEngine height function shader-side;
- keep the project self-contained and easy to replace with a ShaderToy-style
  terrain plus rendering reference.

## Explicit Deferrals

Do not implement full TerrainEngine water in this pass. Reflection/refraction
FBOs, DUDV distortion, normal-map water, and Fresnel blending need a separate
multi-pass scene-color/depth design. The current waterline cue can remain as a
terrain material clamp until that pass, or be replaced by a simpler ShaderToy
water model if that gives us a cleaner known-good reference.

Do not implement tessellation or streaming LOD in this pass. TerrainEngine's
distance-adaptive tessellation is relevant, but the first goal is making the
surface read correctly on the existing Cubey clipmap review mesh.

Do not port foliage, skybox, clouds, post processing, or GUI controls here.
Those are separate integration questions after the terrain surface baseline is
credible.

## Acceptance

The result should make `terrain_ref` captures useful for visual comparison:

- oblique captures should show textured material zones instead of one green
  field;
- dry captures should expose terrain form without water tint;
- surface captures should show procedural rock/grass/sand detail and stronger
  depth cues;
- the batch should pass `cubey_core_tests`, `terrain_ref_tests`, PNG smoke
  tests, and `git diff --check`.

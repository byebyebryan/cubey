# Terrain Ref Presentation Port Plan

Date: 2026-07-06

`projects/terrain_ref` now has a clean TerrainEngine-inspired height source, but
the first captures still read like a debug mesh: flat green material, simple
lighting, fog tint, and water as a color clamp. The next terrain reference pass
should port TerrainEngine's presentation cues before adding more source models.

## Direction

This batch ports the terrain surface presentation, not the whole OpenGL app.
Use `/home/bryan/code/ref/TerrainEngine-OpenGL` as a visual donor for:

- textured sand, grass, rock, and optional snow material bands;
- slope/elevation-based material selection;
- rock normal detail on steep surfaces;
- stronger directional lighting and distance/altitude fog;
- repeatable oblique and surface captures for comparison.

Keep this Cubey-native:

- decode selected image assets through `cubey::read_image_rgba8`;
- upload them as descriptor-backed sampled textures;
- keep the TerrainEngine height function shader-side;
- keep the project self-contained with copied MIT-covered terrain assets and
  provenance notes.

## Explicit Deferrals

Do not implement full TerrainEngine water in this pass. Reflection/refraction
FBOs, DUDV distortion, normal-map water, and Fresnel blending need a separate
multi-pass scene-color/depth design. The current waterline cue can remain as a
terrain material clamp until that pass.

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
- surface captures should show rock/grass/sand detail and stronger depth cues;
- the batch should pass `cubey_core_tests`, `terrain_ref_tests`, PNG smoke
  tests, and `git diff --check`.

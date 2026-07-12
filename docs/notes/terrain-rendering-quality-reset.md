# Terrain Rendering Quality Reset

Date: 2026-07-12

Status: completed renderer checkpoint. Terrain source generation remained
frozen through the native-resolution quality pass.

## Problem

The backdrop camera and foreground-clearance contract are working, but the
terrain still reads as a smooth diagnostic heightfield. Raising the camera hid
unsupported close detail without making the distant terrain convincing. Native
1920 x 1080 crops expose broad airbrushed color, rounded snow boundaries, weak
rock structure, flat green coverage, and little material response to lighting.

The previous rendering checkpoint also contains a correctness mismatch. Vertex
height includes local weathering, while geometric normals and heightfield
shadows sample base height. Weathered geometry therefore does not receive the
normal or shadow response its shape implies.

## Reference Read

Actual TerrainEngine screenshots derive much of their impact from textured
rock, strongly broken mountain silhouettes, water foregrounds, cloud-filled
skies, and directional lighting. Cubey's existing `terrain-engine-ref` control
is only a simplified height-preview port with water disabled. It is useful for
source vocabulary but is not a known-good presentation control.

Selected ShaderToy terrain examples demonstrate the rendering operators that
matter here: final-height normals, multiscale material breakup, slope- and
height-aware geology, irregular snow, bump normals, terrain shadows, and
bounded occlusion. Their raymarching and source code are not applicable
directly. Cubey will implement those concepts clean-room over its mesh clipmap
and shared procedural foundation.

The local ShaderToy mountain references are CC BY-NC-SA or more restrictive.
No code, constants, texture inputs, or source structure will be copied. Actual
reference screenshots may appear only as labeled review oracles with provenance
and no runtime dependency.

## Frozen Boundary

This batch does not change terrain source parameters, height formulas,
weathering behavior, CPU/GPU query parity, clipmap topology, camera contracts,
or collision. It adds no imported textures, visual displacement, canopy shell,
tree instances, water, or cloud integration.

The expected source-summary SHA-256 remains
`5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb`.

## Direction

1. Shade and shadow final weathered height rather than base height.
2. Replace the current broad-color-plus-fine-noise material with seeded 3D
   procedural geology at macro, meso, and local physical scales.
3. Use elevation, final normal, 96 m landform concavity, and filtered procedural
   fields to separate ground, soil, scree, rock, and snow.
4. Add bounded landform-aware ambient visibility so broad folds retain depth
   through sky irradiance and aerial perspective.
5. Keep vegetation coverage subordinate. A flat green mask cannot substitute
   for foliage geometry and must not carry the image.

## Preset Intent

- `mountain`: exposed rock, coherent strata and breakup, scree transitions,
  irregular snow, and clear mesostructure;
- `upland`: mixed soil, subdued rock exposure, and broad ground variation;
- `plains`: low-contrast ground structure with minimal rock and no accidental
  alpine treatment.

All presets share one material evaluator. Preset identity remains derivable
from resolved source parameters rather than a separate authored map.

## Acceptance

- Weathering changes final clay normals and heightfield shadows.
- Mountain faces show coherent structure at roughly 30-500 m scales without
  becoming procedural speckle.
- Snow, scree, and exposed rock follow terrain structure with irregular edges.
- Upland and plains remain visibly softer than mountains.
- Ambient visibility adds fold depth without black seams or painted trenches.
- Vegetation coverage no longer dominates surface contrast.
- Native-resolution stills and moving captures expose no LOD bands or material
  shimmer.
- Capture-body cost remains below 33.3 ms/frame.
- The terrain source hash remains byte-identical.

## Implemented Architecture

The mesh still samples the terrain v1 source directly. Final shading now adds
the weathering-delta derivative to the interpolated base gradient, avoiding
both stale base-height normals and visible clipmap triangle normals. The shadow
receiver uses final height; the two nearest horizon samples include local
weathering while distant samples stay on the macro source where the local
filter is below their useful footprint.

One seeded 3D material evaluator supplies all presets. It samples physical
fields at 680 m, 145 m, and 34 m, filters them by pixel footprint, and combines
them with resolved relief scale, normalized elevation, final normal, and 96 m
concavity. The result carries normalized ground, scree, rock, and snow weights,
material-specific roughness, warped strata, and meso/local detail normals.
Vegetation coverage remains available for backdrop composition, but its color
influence is capped at 0.16 and cannot replace the geologic surface.

Ambient visibility is a separate bounded lighting term. Concave or
horizon-facing landforms can reduce sky irradiance, but the result is clamped
to 0.62-1.0. It does not darken direct sun or modify atmospheric aerial
perspective.

## Review Evidence

`projects/terrain/capture_rendering_quality_review.sh` writes the canonical
ignored pack to `outputs/terrain/rendering-quality-reset/`. The pack includes:

- a three-preset, three-seed backdrop matrix;
- surface, clay, final-normal, shadow, material-weight, and ambient-visibility
  diagnostics;
- weathering-off versus local clay, normal, and shadow controls;
- standard/backdrop comparison, native 1920 x 1080 still and crops;
- standard and backdrop traversal videos plus a profiled capture;
- three explicitly labeled external TerrainEngine screenshot oracles and a
  SHA-256 provenance manifest.

The final source-summary SHA-256 is still
`5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb`.
The 60- and 300-frame 960 x 540 timing runs took 8.24 s and 9.84 s. Their
incremental difference is about 6.67 ms/frame including capture submission,
inside the 33.3 ms workbench budget. The profiled video reports a 0.752 ms p95
CPU submission span after warmup.

## Checkpoint Read

Mountain presentation is materially stronger than the previous smooth gray
heightfield: snow is reachable across observed mountain seeds, mesostructure
survives native resolution, and weathering affects the final normal and nearby
shadow receiver. Upland and plains deliberately remain softer.

This is not final terrain presentation. Lowland foreground remains too smooth
for a close scene, direct-shadow diagnostics still expose a hard visibility
transition at low sun, and there is no foliage geometry. The gap to the actual
TerrainEngine screenshots also includes source silhouettes, water, clouds, and
scene composition, not just material tuning. Those remain explicit follow-up
work rather than hidden inside this renderer checkpoint.

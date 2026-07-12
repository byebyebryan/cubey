# Terrain Rendering Quality Reset

Date: 2026-07-12

Status: implementation study. Terrain source generation remains frozen while
the standalone renderer is reopened for a native-resolution quality pass.

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

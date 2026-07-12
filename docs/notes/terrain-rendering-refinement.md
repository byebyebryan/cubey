# Terrain Rendering Refinement

Date: 2026-07-11

Status: approved rendering-first study. The terrain source is frozen until this
study produces a new visual review pack.

## Problem

Terrain v1 now provides a continuous random-access source, matching CPU and GPU
queries, a traversable clipmap, and stable LOD ownership. Its presentation is
not yet a trustworthy model-quality judge. The final terrain shader currently
uses one procedural palette, direct Lambert lighting, a strong constant ambient
term, and hand-tuned blue distance fog. It has no terrain self-shadowing, no
directional sky irradiance, and no true eye-level review camera.

Those omissions flatten broad relief and make it difficult to distinguish a
source-shape problem from a material or lighting problem. Integration into a
second scene should wait until terrain can be evaluated clearly by itself.

## Reference Read

The useful TerrainEngine cues are modest rather than architectural: distance
tessellation, height/slope material separation, sampled geometric normals, and
height-aware fog. Its renderer is still conventional diffuse lighting and
asset-backed textures, so it is a visual control rather than code to copy.

The selected ShaderToy mountain examples add the cues TerrainEngine lacks:

- heightfield horizon or shadow rays against the same terrain evaluator;
- distance-dependent normal and source sampling;
- low-sun shape studies;
- material breakup at multiple physical scales;
- atmosphere composition as surface transmittance plus in-scattering.

Cubey will adapt those ideas to its mesh/clipmap renderer. It will not port the
raymarched camera path or introduce external terrain textures.

## Frozen Boundary

This study must not change the terrain source parameters, height formulas,
weathering transform, source query contract, or CPU/GPU parity tolerance. It
also excludes hydrology, new presets, foliage, water, imported assets, and
second-consumer integration.

All new environment and material code remains under `projects/terrain`. Shared
atmosphere and forward-PBR shaders remain unchanged while the parallel lighting
worktree is active. A later integration pass can promote only the interfaces
that survive this study.

## Rendering Approach

1. Add a two-meter-clearance ground camera and neutral clay view.
2. Feed atmosphere frame data, diffuse-irradiance spherical harmonics, and the
   primary light into a terrain-local environment uniform block.
3. Apply the shared atmosphere integrator from the camera to each terrain
   fragment instead of using generic distance fog.
4. Estimate broad direct-light visibility from logarithmic heightfield horizon
   samples. This is terrain self-shadowing, not a general scene shadow map.
5. Build a procedural nonmetal material from physical elevation, source slope,
   footprint-filtered variation, and material-specific roughness.

The clay view uses only the source normal. Final presentation may add
footprint-filtered material relief, but it cannot alter geometry or diagnostics.

## Baseline And Budget

The closed v1 renderer was profiled headlessly on the local RTX 5070 Ti at
`960 x 540`, 60 frames, mountain seed `9012`, local weathering, and the surface
camera. The capture body completed in `455.51 ms`, or about `7.59 ms/frame`;
resource creation took `7.50 s` and is tracked separately.

The refinement target is an average capture-body cost below `33.3 ms/frame` at
the same resolution. This is an evaluation-renderer budget, not a production
performance contract.

## Acceptance

- Mountain mass, ridges, peaks, and valleys remain legible in neutral clay
  under three low-sun azimuths.
- Heightfield shadows stay continuous across moving LOD boundaries.
- Aerial perspective converges toward the rendered sky without a hard fog wall
  or loss of nearby contrast.
- Ground traversal maintains two meters of clearance and exposes the real
  difference between two-meter and one-meter near cells.
- Final materials add scale and surface separation without becoming a
  high-frequency noise field.
- The source summary remains byte-identical to the v1 checkpoint.


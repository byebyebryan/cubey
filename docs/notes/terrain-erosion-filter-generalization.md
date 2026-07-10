# Terrain Erosion Filter Generalization

Date: 2026-07-09

This pass determines whether the stateless slope-aware filter from
`shadertoy-erosion-filter` is a generally reusable terrain process or only a
mountain-reference effect. It is an evaluation pass, not a decision to apply
erosion to every biome.

## Contract

Existing `terrain_ref` recipes keep their current unfiltered surface by
default. An explicit preview surface selects the comparison:

- `--terrain-preview-surface pre-process` renders the original biome source;
- `--terrain-preview-surface post-erosion` applies the shared erosion filter;
- `--terrain-preview-color erosion` displays the signed displacement delta.

The reusable operator accepts a source height, source gradient, and activity
value. The original erosion-reference recipe retains its height gate. The
cross-biome comparison uses one source-independent baseline with full activity;
only the shared slope gate suppresses flat terrain. There is no per-biome
strength, wavelength, elevation, climate, or material tuning in this pass.

## Matrix

- `shadertoy-alpine`: expected positive control for steep mountain flanks.
- `shadertoy-badlands`: expected positive control for dry dissected terrain.
- `shadertoy-coast-island`: mixed control for inland slopes, beaches, shelves,
  and coastal cliffs.
- `shadertoy-plains`: negative control for low-relief terrain.
- `shadertoy-dunes`: negative control whose dominant process is wind rather
  than runoff.

Each row captures base material, filtered material, and signed erosion delta
from the same seed and camera. Water is disabled so submerged or flattened
geometry cannot hide process activity.

## Acceptance

- Default biome captures remain unchanged unless `post-erosion` is explicit.
- The operator works on an arbitrary source sample rather than rebuilding the
  source internally.
- Alpine and badlands gain coherent slope-following detail without losing
  their macro silhouette.
- Plains and dunes reveal whether slope gating is sufficient to keep an
  inappropriate process quiet.
- Coast/island reveals whether a future activity mask must distinguish inland
  runoff from beaches, shelves, and cliffs.
- CPU and GLSL implementations use the same scale, strength, octave, slope,
  and displacement rules.

## Decision Rule

Generalization means the operator can be shared as a process primitive. It does
not mean it should be enabled universally. If a biome needs different tuning or
masking, that belongs in an explicit process policy based on climate, material
resistance, slope, and feature scale. If the filter imposes the same etched
signature on unrelated sources, keep it as a narrow reference effect.

True hydraulic erosion, drainage continuity, sediment transport, and
production terrain integration remain out of scope.

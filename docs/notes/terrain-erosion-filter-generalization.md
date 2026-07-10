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
geometry cannot hide process activity. A second sheet repeats the comparison
with neutral height material. All biome erosion diagnostics use the same fixed
`-360` to `+360` meter scale; they are not normalized independently.

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

## Review Outcome

The operator generalizes mechanically but not as a universal visual pass.
Arbitrary biome sources can supply height and numerical gradient samples, the
filtered surfaces preserve their broad silhouettes, and default captures are
unchanged unless `post-erosion` is explicit. CPU tests cover deterministic
arbitrary-source filtering and zero-activity identity; the renderer smoke
covers the opt-in alpine path.

The fixed-scale visual matrix supports selective use:

- Alpine gains visible slope-following detail, but the baseline treats nearly
  every steep flank as active and produces a dense shared signature.
- Badlands also responds strongly. Its existing dissected source and the added
  filter compete at similar scales, so a future use needs a process scale and
  resistance policy rather than another unconditional pass.
- Coast/island keeps the flat shelf mostly quiet while affecting inland relief.
  A production policy would still need land, beach, cliff, and rainfall masks.
- Plains remains effectively unchanged. This validates the lower slope gate as
  a useful first rejection condition.
- Dunes activates on steeper dune faces and adds runoff-like cuts to a
  wind-shaped source. It is a clear negative case and should disable this
  process rather than merely reduce its strength.

The useful result is therefore the shared operator boundary, not shared biome
settings. Future terrain should select it through explicit process policy using
at least climate/moisture, material resistance, slope, and feature scale. Keep
the current baseline disabled by default and do not bake it into biome source
functions.

## Recommendation

Classify this filter as a **stateless mesoscale erosion-detail operator**. It is
not a biome generator, river generator, drainage model, or replacement for
regional hydrology. Its useful properties are deterministic world-coordinate
sampling, random access, bounded local displacement, and slope-following detail.

Production activity should be derived from coherent fields:

```text
activity = runoff * erodibility * slope_window * land_mask * scale_mask
```

- `runoff` comes from climate, rainfall, moisture, and exposure rather than the
  biome name alone;
- `erodibility` comes from material or geology fields;
- `slope_window` suppresses both flats and terrain too steep for this treatment;
- `land_mask` distinguishes inland relief from water, shelves, beaches, and
  other protected terrain classes;
- `scale_mask` relates the filter wavelength and strength to the source feature
  scale and target product resolution. LOD may omit unresolved octaves, but it
  must not change the retained world-space terrain truth.

These must be generated fields, not hand-authored lines, patches, or special
case shapes. A biome recipe may configure or combine the policy fields, but it
should not own a separate copy of the filter.

The recommended terrain composition is:

1. A macro source establishes mountain masses, ridges, basins, plains, dunes,
   coastlines, and other broad structure.
2. Regional hydrology establishes catchments, drainage continuity, rivers, and
   major incision where water processes apply.
3. This stateless filter adds optional mesoscale slope detail where the process
   policy is active.
4. Slope, curvature, material, wetness, vegetation, and rendering products are
   recomputed from the resulting terrain.

This is deliberately a hybrid model. Regional hydrology owns global coherence;
the stateless filter owns cheap local detail. Stretching either one into the
other role would repeat the earlier failure mode of using local noise as a
substitute for terrain structure.

No further per-biome tuning is recommended inside `terrain_ref`. Preserve the
operator and captures as reference evidence, and revisit it only through an
explicit process-policy experiment in the terrain reboot.

Review artifacts live under `outputs/terrain_ref/erosion-generalization/`:

- `cross-biome-contact-sheet.png` compares source material, filtered material,
  and fixed-scale erosion diagnostics;
- `cross-biome-height-contact-sheet.png` repeats the matrix with neutral height
  material;
- `capture-summary.txt` records the 1280x720, 513-grid, seed-9012 pack, which
  completed in 16 seconds on the review machine.

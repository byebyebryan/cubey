# Partially correlated marble regions

Status: planned; another implementation goal loop authorized on 2026-10-22.

## Revised target

The [previous pass](sdf-marble-minerals-plan.md) improved coherence but made
warmth strictly follow the vein masks. User review found that restriction
unnatural. A broader review of natural slabs and close-ups supports partial
correlation: color regions can share a flow with veins while having different
boundaries, widths and internal structure.

[Macchia Vecchia Original](https://www.antolini.com/en/c3/exclusive-collection/m1559/calacatta-macchia-vecchia-original)
is the primary pale/cream reference: wide irregular cream regions, narrow
dark lines, fine internal wisps and occasional flecks. Supplementary
[Cremo Fantasy](https://www.antolini.com/en/c6/natural-stone-collection/m2264/calacatta-cremo-fantasy)
and [Nuvolato](https://www.antolini.com/en/c6/natural-stone-collection/m2253/calacatta-nuvolato)
show directional bands and larger regions crossed by separate lines. These
are visual observations and an inferred procedural direction, not a measured
geological reconstruction or a proposal to adopt their other colors.

## Implementation boundary

- Begin with a flat native SDF slab using the guardian's material and fixed
  model coordinates. Capture the current binary before changing evaluation.
  Inspect unlit, shaded, close and distant views at variation 0 / 0.08 / 0.16.
- Give cream mineral regions their own seeded shape field, with the existing
  large-scale deformation providing a shared flow. Use anisotropic structure,
  fine boundary breakup and internal variation. Their support is independent
  of the narrow vein masks; selected veins can still carry warmer seams.
- Keep quiet pale regions alongside cream regions. Avoid broad uniform tint,
  flat camouflage islands, evenly scattered dots and tracing every gray line
  in cream. Use native swatches to choose scale/coverage/definition, then
  inspect the guardian and its unlit face material.
- Retain original pale/cream multipliers, amount range and saved 0.08 default,
  Balanced neutral marble at zero, all detail controls and scene parameters.
  Preserve limestone, roughness, bump, model coordinates, geometric tracing
  normals, geometry/history identities, persistence and material-only caching.
- Preserve the 80-byte pattern record. No extra control, serialized parameter,
  image map, appearance volume, subsurface scattering or lighting change is
  required. The existing smooth/closed vein topology remains a documented
  limitation; this is a color-distribution pass.
- CPU/GLSL share evaluation and analytic derivatives through the common
  deformation. Pixel footprints filter each spatial scale and region edge.

## Checkpoints

1. Commit this plan and a reproducible native swatch capture driver. Record
   swatch sources, commands, applied configs, source status and exact binary /
   shader hashes for the current renderer before editing material code.
2. Iterate a bounded set of swatch trials against the reference observations.
   Check that cream can occur between veins and that narrow veins cross cream
   and pale areas. Review the selected candidate on the guardian. Revise the
   overly restrictive spatial test; keep finite/bounded cream-only color,
   roughness/gradient and filtering contracts. Commit after focused CPU and
   native foundation checks pass, without relaxing parity tolerances.
3. Capture the selected swatch and guardian at equal amounts/settings, retain
   the older receipts, compare exact neutral images, and qualify turntables.
   Run 1080p tint-off/on profiles without competing owned GPU work.
4. Publish swatch-first comparison evidence, reference links, matching guardian
   views, source identities and limits. Verify served files and available
   browser interactions. Update evidence/docs and commit milestones locally.
   Do not push.

## Acceptance and limits

- Native inspection establishes whether there is a useful material trial;
  user artistic acceptance remains separate.
- Controlled spatial tests require both warm and pale samples away from the
  narrow main/secondary vein paths. Warm and gray-only core samples may still
  occur. Tests assert the revised spatial contract, not a preferred coverage
  percentage or a fit to the photograph.
- Retain the 0.0002 procedural color and 0.00002 geometry/gradient/normal
  CPU/GPU bounds, plain/limestone/neutral regression, deterministic seeds,
  shortcut/full equality, persistence, ownership, short histories and
  one-build appearance edits. Exercise all detail choices at maximum amount.
- Review multiple camera distances and motion for filtering. Preserve the
  exact accepted neutral images across revisions and record cold construction
  separately from steady GPU shading.
- The full guardian history and unchanged desktop UI need not be replayed for
  this color-only edit. Earlier receipts retain their source scope.

Final results will be recorded in the
[procedural material evidence](sdf-procedural-materials-evidence.md).

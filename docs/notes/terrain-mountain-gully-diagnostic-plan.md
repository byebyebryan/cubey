# Terrain Mountain Gully Diagnostic Plan

Date: 2026-06-30

Revision 23 made rivers affect `height_m` and left the mountain stress recipe as
the next shape-quality target. The current mountain fields expose broad support,
peak anchors, ridge skeletons, and uplift, but perspective captures can still
read like noisy generated relief rather than terrain shaped by process.

## Decision

Add a clean-room gully / erosion diagnostic over
`temperate-mountain-range-stress`.

This is a field diagnostic, not hydraulic erosion and not a height mutation. It
should use the existing terrain product spine:

```text
height + derivatives + relief + mountain support -> process fields -> review views
```

The first pass should emit:

- `erosion_delta_m`
- `gully_mask`
- `crease_proxy`
- `post_erosion_height_m`

`height_m` must remain unchanged in this batch. `post_erosion_height_m` is only a
review target for before/after comparisons.

## Implementation Boundary

- Keep the helper terrain-local in `projects/terrain/terrain_process_fields`.
- Do not copy ShaderToy formulas or shader code.
- Do not call the result hydraulic erosion.
- Do not feed the diagnostic into rivers, materials, wetness, vegetation, or
  final mesh height yet.
- Keep non-mountain recipes stable by emitting inactive diagnostic fields.
- Bump the generator revision because the public product fields and debug views
  change.

## Acceptance

- Scalar exports include the four new diagnostic fields and views.
- The mountain stress recipe has nonzero, finite, bounded diagnostic deltas.
- Default river recipes keep the diagnostic inactive.
- `post_erosion_height_m` equals `height_m - erosion_delta_m`.
- Tests prove `height_m` is unchanged by the diagnostic pass.
- Perspective captures can be compared against `height.png`,
  `post-erosion-height.png`, `gully-mask.png`, and `erosion-delta.png` before
  deciding whether any erosion-like pass should become height-affecting.

## Outcome

Implemented in revision 24.

- Added `erosion_delta_m`, `gully_mask`, `crease_proxy`, and
  `post_erosion_height_m` to the terrain product and scalar review set.
- Kept the pass diagnostic-only: `height_m` remains unchanged, and default river
  recipes emit inactive gully fields with `post_erosion_height_m == height_m`.
- The regenerated 513 mountain stress manifest reports 47 fields, 41 scalar
  views, and bounded diagnostic ranges: `erosion_delta_m.max = 78.0`,
  `gully_mask.max = 1.0`, and `crease_proxy.max = 1.0`.
- Refreshed local review captures under `outputs/terrain/mountain-range-stress`
  and `outputs/terrain/mountain-range-stress-1025`.

Remaining question: whether the diagnostic improves mountain readability enough
to justify a later height-affecting pass. That decision should be made from
`height.png`, `post-erosion-height.png`, `erosion-delta.png`, `gully-mask.png`,
`mountain-perspective.png`, and `mountain-profile.png`, not from `final.png`
alone.

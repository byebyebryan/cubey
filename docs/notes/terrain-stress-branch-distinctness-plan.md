# Terrain Stress Branch Distinctness Plan

Date: 2026-06-27

Revision 13 promotes major stress paths into `river_trunk`, but visual review
shows a new hierarchy failure: many promoted branches run beside the main trunk
instead of opening into distinct drainage areas. The field classification is
better than the revision 12 mainstem-plus-tributaries split, but the candidate
selection still rewards nearby alternate routes through the same corridor.

## Decision

Keep this pass stress-only. The default `temperate-mountain-river` recipe should
remain stable.

Promoted stress trunks must add distinct visible drainage territory. A branch is
eligible for `river_trunk` only when it passes the existing stream-order or
discharge hierarchy gate and also contributes enough visible samples away from
the already promoted trunk skeleton. Nearby, parallel alternatives should be
left as tributaries only if they still add some visible feeder structure;
otherwise they should be skipped as redundant diagnostic clutter.

## Implementation Boundary

- Do not add breach routing, erosion, lakes, or a new hydrology solver.
- Do not render raw graph edges.
- Keep the revision 13 softer/wider stress trunk band and existing straight-run
  guards.
- Add a promoted-trunk skeleton used only for stress branch distinctness.
- Apply the distinctness gate to connected support paths, order-seed paths, and
  high-scoring corridor branch candidates.

## Acceptance

- `outputs/terrain/stress-river-network/river-trunk.png` should no longer show
  several promoted trunks packed into one narrow parallel corridor.
- Promoted trunk branches should visibly reach different parts of the review
  patch or different local sub-basins.
- `outputs/terrain/stress-river-network/tributaries.png` may keep smaller
  feeders, but tributaries should not regain the role of primary hierarchy.
- Tests should keep the revision 13 trunk-share and straight-run checks and add
  a corridor-density regression that catches one small window carrying too much
  of the stress trunk field.

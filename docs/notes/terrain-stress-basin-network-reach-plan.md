# Terrain Stress Basin Network Reach Plan

Date: 2026-06-28

Revision 14 fixed the obvious near-parallel promoted-trunk failure by rejecting
branches that do not add distinct visible drainage territory. Visual review now
shows the opposite failure: the stress recipe reads as one trunk with short
fingers rather than a broad river network. The current
`river-trunk.png` also exposes a left-edge discontinuity in the promoted trunk
handoff.

## Decision

Keep this pass stress-only. The default `temperate-mountain-river` product
should remain stable except for shared continuity fixes.

The stress recipe should act as a broad connected river-network diagnostic. It
should select a visible basin tree from the existing routing and stream-order
fields, then render that tree through the same de-gridded channel pipeline used
by the default product. Stress coverage should come from connected upstream
branches and sub-trunks, not from authored masks, quadrant targets, or direct
raw graph-edge rendering.

## Implementation Boundary

- Do not introduce erosion, lakes, breach routing, or a new solver in this pass.
- Do not render D8 graph edges directly into `river_mask` or `river_trunk`.
- Keep the revision 14 distinctness check as an anti-parallel guard, but do not
  let it be the primary network builder.
- Allow accepted stress branches to attach to previously accepted branches, not
  only to the original trunk, so the network can spread through the visible
  basin.
- Add acceptance checks for reach, component continuity, and trunk continuity so
  future tuning cannot regress back to one trunk plus short fingers.

## Acceptance

- `outputs/terrain/stress-river-network/river-trunk.png` should show a connected
  trunk hierarchy without the current left-side break.
- `outputs/terrain/stress-river-network/river-mask.png` should read as one
  broad basin tree reaching a meaningful portion of the map.
- Stress branches should include several long arms that reach different visible
  regions; short local fingers are not enough for this diagnostic recipe.
- Existing straight-run and endpoint-clutter regressions should continue to
  guard against returning to hard D8-looking strokes.

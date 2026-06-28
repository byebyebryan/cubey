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
  mainstem without the current left-side break.
- `outputs/terrain/stress-river-network/river-mask.png` should read as one
  broad basin tree reaching a meaningful portion of the map.
- Stress branches should include several long arms that reach different visible
  regions; short local fingers are not enough for this diagnostic recipe.
- Existing straight-run and endpoint-clutter regressions should continue to
  guard against returning to hard D8-looking strokes.

## Outcome

Implemented in revision 15.

- Added stress-only reach regressions for high-strength coverage, largest
  component dominance, crop-edge reach, coarse review footprint, and trunk
  continuity.
- Added a connected basin-growth pass that gathers edge-biased stream-order
  candidates, traces them downstream into the active network, and accepts only
  paths that add visible samples and coarse footprint.
- Kept extra stress trunk promotion disabled for now. Broad diagnostic reach is
  carried by attached tributaries, while `river_trunk` remains a continuous
  mainstem.
- Restored the longer accumulation trunk candidate and widened/softened the
  stress mainstem so the trunk remains visible without fragmenting into
  disconnected side branches.

The revision 15 stress output is broader and no longer has the left-side trunk
break, but it still exposes straight-ish tributary strokes. That is now a source
model limitation rather than only a coverage bug; the next river pass should
focus on more organic tributary routing or erosion/breach-informed path
selection.

Revision 16 follows up on that limitation by adding rendered trunk-connectivity
gating, direction checks, tighter straight-run caps, and extra pre-curving for
stress support paths. It improves the obvious artificial strokes, but does not
claim the broader multi-edge river-network driver is solved.

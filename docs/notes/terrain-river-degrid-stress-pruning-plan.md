# Terrain River Degrid And Stress Pruning Plan

Date: 2026-06-27

Revision 10 improved coverage, channel width variation, and routing continuity,
but the visible river still reads too close to an eight-neighbor graph in places:
long straight or diagonal runs reappear when traced grid paths become rendered
centerlines. The stress recipe also accepts too many low-order connected support
paths, so it reads as a hairy watershed sketch instead of a larger river system.

## Decision

D8/downstream graph paths can remain as topology and connectivity scaffolding,
but visible channel geometry should be de-gridded before rasterization. Selected
grid paths should be converted into sub-cell centerlines that are smoothed and
nudged along the continuous D-Infinity flow direction. Endpoints and confluences
must stay pinned so the visible product remains connected.

The stress recipe should favor hierarchy over maximum coverage: fewer accepted
support paths, stronger stream-order gates, and wider confluence spacing. It
should still reveal artifacts over a larger river system, but it should not
paint every low-order finger that can technically reach the active basin.

## Implementation Boundary

- Do not add erosion, breach routing, lakes, or a new hydrology solver.
- Keep `routing_fill_delta` and the revision 10 routing repair behavior.
- Keep D8 graph traversal where it selects connectivity, but stop using raw D8
  graph paths as visible centerlines.
- Keep stress as a diagnostic recipe, not the default composition target.

## Acceptance

- Default `river-mask.png` and `river-trunk.png` no longer show obvious long
  straight or 45-degree high-strength runs.
- Stress keeps a connected dominant network but has fewer short hairy branches.
- Branch-density and grid-run tests catch regressions directly.
- Updated captures and docs call out remaining hydrology limitations honestly.

## Outcome

Implemented in revision 11.

- Grid-selected trunk, tributary, and connected-support paths now pass through a
  degrid stage before rasterization: resampling, smoothing, flow-direction
  nudging, and uphill-move constraints over the repaired routing surface.
- Channel rasterization now uses denser resampling and a stronger smoothing /
  relax pass so the high-strength core follows the de-gridded centerline rather
  than the raw D8 graph.
- The stress recipe now uses stronger bounded lateral meander, higher
  support-order gates, fewer painted support paths, and wider confluence spacing
  to reduce low-order branch clutter while preserving a connected review
  network.
- Terrain tests now cover stress endpoint density and long high-strength
  straight/diagonal runs in addition to the existing coverage and continuity
  checks.

One tuning lesson is worth preserving: a hard visual-shape rejection for any
long grid-aligned support path starved seed `1234` and left only a tiny visible
stress network. The kept approach is to prune low-value support expansion and
de-grid rendered centerlines, not to reject every long straight source path
before hierarchy selection.

Remaining limitation: stress captures can still show straight-ish support
strokes and parallel branches. That points to the next source/model problem:
replace painted support expansion with a more principled basin and tributary
hierarchy rather than adding more render-space masking.

## Revision 12 Follow-Up

The first revision 11 prune pass improved branch clutter but regressed active
coverage and network read. Revision 12 recovers part of that loss by:

- increasing the default recipe's branch and order-seed budget;
- allowing order-seed paths to trace farther upstream before accumulation
  cutoff;
- broadening the stress selected support hierarchy and overlap gate; and
- adding path-level spacing between accepted connected-support paths so coverage
  can increase without immediately returning to dense parallel support bundles.

This is still a compromise. It is better than the sparse revision 11 output, but
the proper next fix is still a stronger basin/tributary hierarchy rather than
more stress-recipe tuning.

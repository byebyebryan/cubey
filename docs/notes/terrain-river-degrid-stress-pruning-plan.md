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

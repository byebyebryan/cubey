# Terrain Stress Promotion Organic Cleanup Plan

Date: 2026-06-28

Revision 15 made the stress river network broader, but visual review showed two
remaining failures:

- promoted trunk branches could still become disconnected or read as short
  straight stubs;
- some tributaries still exposed D8-looking vertical or diagonal source paths.

## Decision

Keep this pass stress-only and driver-driven. Do not add authored branch masks
or force a branch to a specific map edge. A candidate branch should be judged by
the rendered field it produces and by whether the source path is directionally
distinct from the existing trunk.

The stress recipe should prefer a clean connected mainstem with attached
tributaries over a broader but obviously artificial river network.

## Implementation Boundary

- Do not replace routing, add erosion, or add breach/lake handling in this pass.
- Do not render raw graph edges directly.
- Keep branch promotion provisional: a promoted branch must pass a rendered
  trunk-component check before it mutates the product.
- If a promotion fails, keep the path as a tributary candidate rather than
  dropping all coverage from that source.

## Outcome

Implemented in revision 16.

- Added a rendered trunk connectivity guard. Promotion now paints into a
  temporary trunk field first and accepts only if the high-strength trunk
  remains dominated by one connected component.
- Applied that guard to basin-network, connected-support, stream-order seed, and
  corridor branch promotion paths.
- Added direction and straight-run gates before stress support paths can promote
  into `river_trunk`.
- Added an extra pre-curving pass for connected-support, order-seed, basin, and
  corridor tributary paths before normal channel rasterization.
- Tightened stress source caps so long grid-aligned support paths are less
  likely to become visible D8-looking tributary strokes.
- Updated review regressions to match the current slice: connected trunk,
  multi-region trunk footprint, multi-edge river reach, broad-but-not-flooded
  river footprint, and tributary straight-run guards.

This is cleaner than revision 15, but it is still not a mature river-network
driver. The stress output currently favors a clean connected mainstem and
attached branches. A future hydrology pass should improve basin reach and
organic branching from better source drivers rather than relaxing the straight
run filters until artifacts return.

# Terrain Stress Trunk Hierarchy Plan

Date: 2026-06-27

The revision 12 stress recipe recovers more river coverage, but its hierarchy is
still wrong: one main corridor is painted as `river_trunk`, while most of the
visible network expansion is painted as `tributaries`. That makes major branches
read as thin support strokes even when they are high-order drainage paths.

## Decision

Keep the next pass stress-only. Do not change the default recipe composition
yet.

Stress should promote major connected-support and order-seed paths into
`river_trunk` when they have enough visible length and either high stream order
or high discharge. Lower-order attached paths should remain tributaries. This
keeps the de-gridded centerline renderer and the single selected basin, but
makes the stress product read as a branching trunk skeleton with tributary
feeders rather than a mainstem plus many tributary strokes.

## Implementation Boundary

- Do not add a new hydrology solver, erosion pass, lake model, or multi-basin
  corridor renderer.
- Keep the revision 11/12 degrid, support spacing, and stress coverage recovery
  behavior.
- Change the target field for qualified stress paths; do not introduce raw graph
  edge rendering.
- Keep the default recipe stable unless shared helper code is needed.

## Acceptance

- `outputs/terrain/stress-river-network/river-trunk.png` shows a branching trunk
  skeleton, not only one heavy main corridor.
- `outputs/terrain/stress-river-network/tributaries.png` reads as feeder
  structure rather than the primary network carrier.
- Stress `river-mask.png` keeps the connected network footprint from revision
  12 without returning to dense parallel support fans.
- Tests assert that stress trunk coverage is meaningful and that tributaries do
  not dominate the high-strength network.

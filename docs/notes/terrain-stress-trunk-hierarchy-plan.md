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

## Outcome

Implemented in revision 13.

- Added a stress-only path hierarchy classifier based on visible sample length,
  stream order, and normalized accumulation/discharge.
- Promoted qualified connected-support paths, order-seed paths, and a capped
  number of high-scoring branch candidates into `river_trunk`; lower-order
  attached paths remain in `tributaries`.
- Split trunk strength and channel softening into settings so the stress recipe
  can use a broader but softer trunk band. This keeps review-threshold trunk
  coverage meaningful without reintroducing long high-strength axis-aligned or
  diagonal runs.
- Added regression checks for stress trunk share, tributary dominance, and the
  existing straight-run guards.

This remains a hierarchy/classification pass over the current routing products,
not a replacement hydrology model. The next river-quality work should focus on
better basin/tributary generation rather than adding more hand-tuned promoted
branch caps.

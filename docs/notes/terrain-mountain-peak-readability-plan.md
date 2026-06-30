# Terrain Mountain Peak Readability Plan

Revision 21 separated the mountain stress recipe into inspectable source
fields, but the rendered terrain still reads as uniformly rough highland. The
peak-first driver exists in diagnostics; the final height composition and
`mountain-relief.png` review image do not yet make the broad buildup into high
peaks obvious.

Status: implemented in revision 22.

## Direction

Keep this pass scoped to `temperate-mountain-range-stress`. Default river
recipes should keep their current visual behavior and continue emitting inactive
mountain stress-only diagnostics.

Revision 22 should make the existing `mountain-relief.png` image the primary
readability view instead of adding another debug view. It should show lowland,
foothill, highland, ridge, and peak progression more clearly than revision 21.

## Implementation Batch

1. Rebalance mountain stress height composition so peak uplift is a dominant
   summit contribution rather than a subtle accent.
2. Keep broad mountain envelope responsible for lowland, foothill, and highland
   mass.
3. Make ridge influence create broader shoulders leading into peak anchors.
4. Gate residual detail so it supports mountain/ridge/peak structure instead of
   competing across the whole patch.
5. Retune `mountain-relief.png` around elevation hierarchy: use a clearer height
   ramp, lower hillshade contrast, reduce high-frequency local-relief darkening,
   and keep ridge/peak tint subtle.
6. Refresh mountain stress captures and docs.

## Review Criteria

- `mountain-relief.png` should show elevation buildup before fine texture.
- `height.png` should make peak regions visibly higher than general mountain
  support.
- `mountain-envelope.png` should still read as broad mass rather than a disk or
  quadrant mask.
- `mountain-ridge-influence.png` should explain ridge shoulders leading toward
  peaks.
- `peak-uplift.png` should be strong enough to explain the highest terrain.

## Outcome

Revision 22 keeps the revision 21 field contract but changes how the mountain
stress recipe composes height. The generic base-elevation tilt is replaced with
an envelope-driven mountain base, peak uplift is strengthened into a visible
summit contribution, ridge influence is widened into broader shoulders, and
residual detail is damped unless mountain/ridge/peak support is present.

`mountain-relief.png` now uses a dedicated elevation-first ramp with softer
hillshade and subtler source-field tinting. The refreshed mountain stress
captures are available at:

- `outputs/terrain/mountain-range-stress`
- `outputs/terrain/mountain-range-stress-1025`

The pass improves the broad buildup into high peaks, but it is still not an
erosion, talus, snow, glacial, or world-scale mountain-range solution.

## Deferred

- New debug view names or product fields.
- Erosion-evolved ridge cleanup.
- Talus, snow/ice, glacial carving, and alpine material polish.
- World-scale mountain range continuity.

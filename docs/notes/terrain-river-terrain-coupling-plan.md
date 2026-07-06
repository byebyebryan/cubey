# Terrain River Terrain Coupling Plan

The renderer-backed preview exposed a product-model issue in the rebooted
terrain generator: rivers are visible mostly because `river_mask` and wetness
tint the mesh, while the mesh height still comes from the pre-river source
height. In perspective, that reads as painted blue texture rather than terrain
that has been shaped by drainage.

This pass corrects that coupling before another mountain-generator pass. The
terrain product should preserve the source height as a diagnostic field, apply
river valley and channel incision, and publish `height_m` as the final carved
height consumed by preview meshes and downstream users.

## Goals

- Keep the existing river-network drivers, but make them terrain-form drivers.
- Add diagnostic preview color modes so geometry can be reviewed without
  material or river tint.
- Emit explicit incision fields for channel and valley review.
- Recompute slope, relief, materials, and vegetation from final carved height.
- Refresh river captures so current and stress recipes can be reviewed in both
  material and height-only perspective.

## Non-goals

- This is not the full mountain-driver rewrite. The mountain stress recipe is
  still expected to look artificial until a separate hierarchy/source-shape pass.
- This does not add render-only water geometry. The fix belongs in the terrain
  product first.
- This does not replace the river-network source. It makes the current source
  honest in the height product before deeper hydrology work.

## Acceptance

- Height-only perspective preview shows visible low channels or valleys along
  active rivers.
- Material perspective preview aligns water and wetness with carved terrain.
- Stress river still reads as one connected network rather than disconnected
  painted fragments.
- Product tests prove active river samples sit lower than nearby shoulders on
  average.

## Implemented Checkpoint

Revision 23 implements this correction. `height_m` is now the carved terrain
surface, while `pre_process_height_m` preserves the source height for review.
`channel_incision` and `valley_incision` expose the carving masks used to lower
the final surface, and slope/material/vegetation fields are recomputed from the
carved height.

Refreshed review outputs:

- `outputs/terrain/current-river-network`
- `outputs/terrain/stress-river-network`

Each directory includes the 37-view scalar set plus:

- `river-perspective.png`
- `river-profile.png`
- `river-height-perspective.png`
- `river-channel-perspective.png`

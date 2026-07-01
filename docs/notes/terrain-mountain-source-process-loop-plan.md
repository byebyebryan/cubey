# Terrain Mountain Source Process Loop Plan

Date: 2026-07-01

Revision 29 made the remaining mountain problem clearer. The bounded thermal
talus diagnostic reduces some over-steep shoulders in
`mountain-post-erosion-perspective.png`, but the strongest visual failures are
still upstream of the process pass:

- some summits read as rounded bulges instead of peaks rising out of a massif;
- some crest bands remain too narrow or too straight;
- some shoulders still look stepped because source support changes too abruptly;
- talus and gully diagnostics expose the same localized source artifacts rather
  than solving them.

The revision 30 target is therefore not "more erosion". It is a tighter
source/process loop: make the mountain source profile more coherent before
local detail, then add a compact review image that compares the important
source and process stages side by side.

## Revision 30 Target

- keep `height_m` as the product surface;
- keep `post_erosion_height_m` as a diagnostic review surface;
- add one compact mountain process comparison debug export;
- revise the mountain stress source profile so broad mass, shoulders, ridges,
  and summit rise build into each other more gradually;
- avoid adding hand-authored lines, masks, or one-off local features;
- keep the pass scoped to `temperate-mountain-range-stress`.

The comparison export should make future review less subjective. It should show
the source/product/process relationship in one image rather than requiring a
manual shuffle through separate scalar PNGs:

```text
profile/source height | final height | post erosion
slope instability     | thermal delta | talus deposition
```

The source-profile change should favor a broad massif and shoulder buildup,
with ridges acting as widened structure inside that mass and summits emerging
from the combined mass/ridge field. Thin fins, isolated blobs, and hard shelves
are all failures.

## Review Order

For revision 30, inspect:

1. `outputs/terrain/mountain-range-stress/mountain-process-review.png`
2. `outputs/terrain/mountain-range-stress/mountain-perspective.png`
3. `outputs/terrain/mountain-range-stress/mountain-post-erosion-perspective.png`
4. `outputs/terrain/mountain-range-stress/mountain-profile.png`
5. `outputs/terrain/mountain-range-stress/mountain-profile-height.png`
6. `outputs/terrain/mountain-range-stress/mountain-mass.png`
7. `outputs/terrain/mountain-range-stress/mountain-shoulder.png`
8. `outputs/terrain/mountain-range-stress/slope-instability.png`

Expected success is incremental: a more cohesive pre-process mountain shape and
an easier review workflow. If the new source profile merely hides artifacts by
flattening the range, revert the tuning and keep only the comparison export.

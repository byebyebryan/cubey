# Terrain Reboot Current Captures

This note records the current terrain reboot capture set after the revision 2
temperate mountain river product pass.

## Capture Command

```sh
./build/dev/projects/terrain/terrain --headless --grid-size 513 --terrain-debug-view all --terrain-output-dir outputs/terrain/current
```

The review images are `513x513` PNGs under `outputs/`, which is intentionally
ignored by git. This replaced the earlier tiny local output set so field
structure, channel continuity, and material response are easier to inspect.

## What To Inspect

- `final.png`: debug composition of height, material masks, slope shade, and
  active river/wetness response.
- `drainage-potential.png`: scalar routing surface before flow routing. This
  should remain smooth even when later river products expose routing artifacts.
- `flow-direction.png`: flow receiver directions or continuous flow-angle debug
  data for diagnosing grid artifacts.
- `flow-accumulation.png`: routed catchment field. This should show regional
  organization, not many broken local fragments.
- `sink-mask.png`: visible crop outlets and true terminal routing cells, useful
  for spotting where the larger hidden routing domain leaves the review patch.
- `river-trunk.png`: soft active main-channel product field extracted from the
  visible-crossing routed catchment, resampled, relaxed over drainage potential,
  and rasterized as channel segments.
- `tributaries.png`: conservative branch field feeding the trunk.
- `river-mask.png`: combined active river product used by channel width,
  valley width, wetness, deposition, material, and final debug rendering.
- `height.png`, `slope.png`, and `ridge-uplift.png`: current mountain/base
  terrain sources that still need a stronger mountain-driver pass.

## Current Limitations

The active river no longer depends on an authored center line, and the visible
trunk/mask now use a padded hidden routing domain instead of treating the review
patch as the whole watershed. Main trunks are selected from traced candidates
that better cross the visible crop, then the path is resampled, constrained by
drainage potential, relaxed, and rasterized as a soft channel curve.

The current revision still exposes a routing-grid artifact: the
`drainage-potential` field is smooth, but D8 flow accumulation quantizes water
movement into horizontal, vertical, and 45-degree receiver runs. Smoothing the
rendered channel helps but cannot fully remove a lattice-shaped drainage graph.
The next river-quality pass should replace D8 as the active river driver with a
D-Infinity-style continuous flow angle, fractional accumulation, and continuous
streamline extraction. Depression fill/breach routing remains the follow-up
hydrology correction before expanding to lakes, canyons, or broader biome
recipes.

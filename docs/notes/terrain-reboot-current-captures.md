# Terrain Reboot Current Captures

This note records the current terrain reboot capture set after the first
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
- `drainage-potential.png`: scalar routing surface before D8 flow routing.
- `flow-direction.png`: D8 receiver directions for diagnosing grid artifacts.
- `flow-accumulation.png`: routed catchment field. This should show regional
  organization, not many broken local fragments.
- `sink-mask.png`: terminal routing cells, useful for spotting local basin
  fragmentation.
- `river-trunk.png`: soft active main-channel product field extracted from the
  strongest routed catchment, smoothed, and rasterized as channel segments.
- `tributaries.png`: conservative branch field feeding the trunk.
- `river-mask.png`: combined active river product used by channel width,
  valley width, wetness, deposition, material, and final debug rendering.
- `height.png`, `slope.png`, and `ridge-uplift.png`: current mountain/base
  terrain sources that still need a stronger mountain-driver pass.

## Current Limitations

The active river no longer depends on an authored center line, and the visible
trunk/mask now use smoothed channel-curve rasterization instead of direct
one-cell D8 path painting. The route network underneath is still D8 over a
scalar drainage potential, so branch placement, sinks, and some large-scale
bends are still less natural than a real hydrology pass. The next river-quality
work should evaluate depression fill/breach routing before expanding to lakes,
canyons, or broader biome recipes.

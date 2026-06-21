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
- `flow-accumulation.png`: routed catchment field. This should show regional
  organization, not many broken local fragments.
- `river-trunk.png`: soft active main-channel product field extracted from the
  strongest routed catchment.
- `tributaries.png`: conservative branch field feeding the trunk.
- `river-mask.png`: combined active river product used by channel width,
  valley width, wetness, deposition, material, and final debug rendering.
- `height.png`, `slope.png`, and `ridge-uplift.png`: current mountain/base
  terrain sources that still need a stronger mountain-driver pass.

## Current Limitations

The active river no longer depends on an authored center line, but it still uses
D8 routing over a scalar drainage potential. Close inspection shows perfectly
straight reaches, sharp turns, angular segments, and branch selection that is
less natural than a real hydrology pass. The next river-quality work should
evaluate depression fill/breach routing and vectorized or smoothed channel paths
before expanding to lakes, canyons, or broader biome recipes.

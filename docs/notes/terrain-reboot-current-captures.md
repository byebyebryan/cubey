# Terrain Reboot Current Captures

This note records the current terrain reboot capture set after the revision 4
temperate mountain river product pass.

## Capture Command

```sh
./build/dev/projects/terrain/terrain --headless --grid-size 513 --terrain-debug-view all --terrain-output-dir outputs/terrain/current
./build/dev/projects/terrain/terrain --headless --grid-size 513 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network
```

The review images are `513x513` PNGs under `outputs/`, which is intentionally
ignored by git. This replaced the earlier tiny local output set so field
structure, channel continuity, and material response are easier to inspect.
`outputs/terrain/current` is the default product review. The optional
`outputs/terrain/stress-river-network` set uses the diagnostic stress recipe to
paint a larger channel graph across the patch. Each review set currently writes
19 PNGs.

## What To Inspect

- `final.png`: debug composition of height, material masks, slope shade, and
  active river/wetness response.
- `drainage-potential.png`: scalar routing surface before flow routing. This
  should remain smooth even when later river products expose routing artifacts.
- `filled-drainage-potential.png`: routing surface after the current
  Priority-Flood-style epsilon fill. Compare with raw drainage to see what the
  hydrology correction changed.
- `depression-depth.png`: amount of fill applied to each visible cell.
- `flow-direction.png`: continuous flow-angle debug data for diagnosing local
  sinks and direction-field artifacts.
- `flow-accumulation.png`: D-Infinity-style fractional routed catchment field.
  This should show regional organization without the obvious horizontal,
  vertical, and 45-degree D8 lattice.
- `channel-graph.png`: selected source/confluence/outlet graph raster before it
  is interpreted as trunk versus tributary product fields.
- `sink-mask.png`: visible crop outlets and true terminal routing cells, useful
  for spotting where the larger hidden routing domain leaves the review patch.
- `river-trunk.png`: soft active main-channel product field selected from
  the channel graph, resampled, procedurally offset, lightly relaxed over the
  filled routing surface, and rasterized as channel segments.
- `tributaries.png`: lower-order selected channel graph edges.
- `river-mask.png`: combined active river product used by channel width,
  valley width, wetness, deposition, material, and final debug rendering.
- `height.png`, `slope.png`, and `ridge-uplift.png`: current mountain/base
  terrain sources that still need a stronger mountain-driver pass.

For `outputs/terrain/stress-river-network`, look for failures that the smaller
default network may hide: repeated parallel channels, schematic branch fans,
disconnected-looking tributaries, local-sink dead ends, and too-straight trunk
segments. The stress recipe intentionally covers more of the patch and should
not be treated as the desired default composition.

## Current Limitations

The active river no longer depends on an authored center line, and the visible
trunk/mask now use a padded hidden routing domain instead of treating the review
patch as the whole watershed. Revision `4` fills local depressions on the hidden
routing surface, routes accumulation with continuous D-Infinity-style flow
angles and fractional receivers, then extracts an explicit channel graph from
the filled accumulation and dominant downstream receivers. Graph edges are
selected by outlet component, visible length, crop continuity, accumulation, and
parallel-path pruning, then resampled, constrained by the filled drainage
surface, offset, and rasterized as soft channel curves.

Remaining limitations are now concentrated in graph quality and hydrology rather
than only flow accumulation. The current epsilon fill is useful but not a breach
or erosion model, selected edges can still expose straight receiver runs or
sharp turns, and stress captures can still read as schematic when coverage is
pushed high. The next river-quality pass should compare breach routing, add
order/discharge-aware channel-width variation, and improve basin-aware graph
pruning before treating rivers as a solved driver.

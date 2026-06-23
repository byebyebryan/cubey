# Terrain Reboot Current Captures

This note records the current terrain reboot capture set after the revision 5
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
paint more trunks and tributaries across the patch.

## What To Inspect

- `final.png`: debug composition of height, material masks, slope shade, and
  active river/wetness response.
- `drainage-potential.png`: scalar routing surface before flow routing. This
  should remain smooth even when later river products expose routing artifacts.
- `flow-direction.png`: continuous flow-angle debug data for diagnosing local
  sinks and direction-field artifacts.
- `flow-accumulation.png`: D-Infinity-style fractional routed catchment field.
  This should show regional organization without the obvious horizontal,
  vertical, and 45-degree D8 lattice.
- `sink-mask.png`: visible crop outlets and true terminal routing cells, useful
  for spotting where the larger hidden routing domain leaves the review patch.
- `river-trunk.png`: soft active main-channel product field selected from
  fractional accumulation candidates, kept connected by the current topology
  fallback, resampled, relaxed over drainage potential, and rasterized as
  channel segments.
- `tributaries.png`: conservative branch field feeding the trunk.
- `river-mask.png`: combined active river product used by channel width,
  valley width, wetness, deposition, material, and final debug rendering.
- `height.png`, `slope.png`, and `ridge-uplift.png`: current mountain/base
  terrain sources that still need a stronger mountain-driver pass.

For `outputs/terrain/stress-river-network`, look for failures that the smaller
default network may hide: repeated parallel channels, schematic branch fans,
disconnected-looking tributaries, local-sink dead ends, and too-straight trunk
segments. The stress recipe intentionally covers more of the patch and should
not be treated as the desired default composition.

The current capture set intentionally keeps the revision 3 connected
trunk/tributary baseline after the reverted revision 4 graph-routing attempt.
Revision 5 adds stream-order-seeded paths only when they reconnect to the active
network. The rejected revision 4 attempt made the visible product worse by
rendering selected graph edges directly, producing disconnected snippets and
hard straight or diagonal runs. See
`docs/notes/terrain-river-graph-routing-attempt.md` for the retained learnings.

## Current Limitations

The active river no longer depends on an authored center line, and the visible
trunk/mask now use a padded hidden routing domain instead of treating the review
patch as the whole watershed. Revision `5` routes accumulation with continuous
D-Infinity-style flow angles and fractional receivers. Active channel extraction
is still a hybrid: the revision 3 candidate-scored trunk/tributary network keeps
the visible product connected, while stream-order-seeded additions borrow the
better-looking source shape from the coherent `stream_order` diagnostic without
rendering the whole hierarchy. Added order paths must reconnect to the active
network, then are resampled, constrained by drainage potential, relaxed, and
rasterized as soft channel curves.

Remaining limitations are now concentrated in network extraction and hydrology
rather than only flow accumulation. Continuous streamlines can still terminate on
unresolved local sinks, and the topology fallback can still make tributary
placement feel schematic. Stream-order seeding can improve branch shape, but it
is still a supplement rather than a full network extractor. The next
river-quality pass should evaluate depression fill or breach routing, then
replace the fallback graph with an explicit connected network extraction over
the fractional accumulation field.

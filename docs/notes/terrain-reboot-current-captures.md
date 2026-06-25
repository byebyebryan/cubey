# Terrain Reboot Current Captures

This note records the current terrain reboot capture set after the revision 9
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
apply a stronger basin-grade routing profile and paint extra connected support
paths across the patch.

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
- `river-trunk.png`: soft active main-channel product field traced from a
  connected stream-order support corridor, resampled, relaxed over drainage
  potential, and rasterized as channel segments.
- `tributaries.png`: conservative connected branch field feeding the trunk.
- `river-mask.png`: combined active river product used by channel width,
  valley width, wetness, deposition, material, and final debug rendering.
- `channel-width.png`: channel-width product derived from active river strength
  and discharge. Use this to check that trunk/tributary width is not uniform.
- `height.png`, `slope.png`, and `ridge-uplift.png`: current mountain/base
  terrain sources that still need a stronger mountain-driver pass.

For `outputs/terrain/stress-river-network`, look for failures that the smaller
default network may hide: repeated parallel channels, schematic branch fans,
disconnected-looking tributaries, local-sink dead ends, and too-straight trunk
segments. The stress recipe intentionally covers more of the patch and should
not be treated as the desired default composition.

The current capture set intentionally keeps the lesson from the reverted
revision 4 graph-routing attempt without rendering graph edges directly.
Revision 8 selects connected support corridors from the coherent `stream_order`
diagnostic, traces and smooths active product paths from that source, and
promoted selected support cells as softer channel coverage. Revision 9 keeps the
connected support lesson but changes the stress recipe to paint connected
support paths through the normal smoothed tributary pipeline instead of painting
raw support cells or multiple unrelated corridors. The
rejected revision 4 attempt made the visible product worse by rendering selected
graph edges directly, producing disconnected snippets and hard straight or
diagonal runs. See
`docs/notes/terrain-river-graph-routing-attempt.md` for the retained learnings.

## Current Limitations

The active river no longer depends on an authored center line, and the visible
trunk/mask now use a padded hidden routing domain instead of treating the review
patch as the whole watershed. Revision `9` routes accumulation with continuous
D-Infinity-style flow angles and fractional receivers, selects active channels
from connected `stream_order` support, accepts extra branches only when they
terminate at an existing active channel, and gives the stress recipe a
basin-convergent routing source plus extra painted connected support paths. The
stress recipe remains intentionally better for artifact hunting than composition
review.

Remaining limitations are now concentrated in hydrology and corridor scoring
rather than only flow accumulation. Continuous streamlines can still terminate on
unresolved local sinks, stress captures can still expose straight reaches and
parallel branches, and the source can still look too schematic without real
depression fill or breach routing. The next river-quality pass should evaluate
depression fill or breach routing, then improve trunk continuity and default
composition scoring.

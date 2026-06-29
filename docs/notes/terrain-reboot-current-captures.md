# Terrain Reboot Current Captures

This note records the current terrain reboot capture set after the revision 17
graph-first stress river pass.

## Capture Command

```sh
./build/dev/projects/terrain/terrain --headless --grid-size 513 --terrain-debug-view all --terrain-output-dir outputs/terrain/current-river-network
./build/dev/projects/terrain/terrain --headless --grid-size 513 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network
./build/dev/projects/terrain/terrain --headless --grid-size 1025 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network-1025
```

The primary review images are `513x513` PNGs under `outputs/`, with a `1025x1025`
stress capture for larger network inspection. `outputs/` is intentionally
ignored by git. This replaced the earlier tiny local output set so field
structure, channel continuity, and material response are easier to inspect.
`outputs/terrain/current-river-network` is the default product review. The optional
`outputs/terrain/stress-river-network` set uses the diagnostic stress recipe to
stress-test broader river-network shape and coverage. Revision 12
restores some active network coverage after the first degrid/pruning pass while
spacing accepted support paths to avoid near-duplicate branch bundles. Revision
13 keeps the same routing source but promotes major stress support/order-seed
and high-scoring branch paths into `river_trunk`, with a softer/wider stress
trunk band to avoid long high-strength straight runs. Revision 14 keeps the
stress-only hierarchy but rejects promoted candidates that mostly run beside the
existing trunk skeleton without adding distinct visible drainage area. Revision
15 keeps the stress trunk as a continuous mainstem and grows broader connected
basin-tree tributaries from edge-biased stream-order candidates. Revision 16
keeps promotion available only when the rendered trunk remains connected, then
tightens and pre-curves stress support paths so the worst straight tributary
fingers are filtered before export.
Revision 17 replaces the stress recipe's visible source topology with a
deterministic jittered river graph over the padded hidden domain. The 1025
stress capture is included as a larger artifact-hunting view because the graph
network reads differently at thumbnail and high resolution.

For the next hierarchy pass, keep the field contract unchanged but interpret
`river_trunk` as major channels: the mainstem plus selected high-discharge or
high-order tributaries. `tributaries` should hold smaller attached branches.
The graph fields stay diagnostic source views. This revision 17 graph is still
patch-local over the padded hidden domain; later world-scale terrain work should
move topology planning into deterministic world-coordinate basin graph data and
let local products rasterize only the tile plus halo.

## What To Inspect

- `final.png`: debug composition of height, material masks, slope shade, and
  active river/wetness response.
- `drainage-potential.png`: scalar routing surface before flow routing. This
  is the repaired routing surface and should remain smooth even when later river
  products expose routing artifacts.
- `routing-fill-delta.png`: raw-to-repaired routing delta from the priority-flood
  epsilon fill. This should stay sparse and bounded; broad bright regions mean
  the repair is flattening too much of the routing domain.
- `flow-direction.png`: continuous flow-angle debug data for diagnosing local
  sinks and direction-field artifacts.
- `flow-accumulation.png`: D-Infinity-style fractional routed catchment field.
  This should show regional organization without the obvious horizontal,
  vertical, and 45-degree D8 lattice.
- `river-graph-plan.png`: revision 17 stress source topology before channel
  painting. This should show one connected drainage graph with an obvious trunk
  and attached branches, independent of rendered channel thickness.
- `river-graph-discharge.png`: graph discharge/order diagnostic used by the
  stress recipe for channel and valley width. It should brighten downstream and
  at confluences instead of showing uniform branch widths.
- `sink-mask.png`: visible crop outlets and true terminal routing cells, useful
  for spotting where the larger hidden routing domain leaves the review patch.
- `river-trunk.png`: soft active major-channel product field. In the stress set,
  this should read as the mainstem plus selected major tributaries from the
  graph plan, not every branch and not only a single line.
- `tributaries.png`: conservative connected branch field feeding the trunk,
  with the stress recipe using accepted graph paths to carry broad diagnostic
  reach while keeping those branches attached to the active network.
- `river-mask.png`: combined active river product used by channel width,
  valley width, wetness, deposition, material, and final debug rendering.
- `channel-width.png`: channel-width product derived from active river strength
  and discharge. Use this to check that trunk/tributary width is not uniform.
- `height.png`, `slope.png`, and `ridge-uplift.png`: current mountain/base
  terrain sources that still need a stronger mountain-driver pass.

For `outputs/terrain/stress-river-network`, look for failures that the smaller
default network may hide: repeated parallel channels, schematic branch fans,
disconnected-looking tributaries, local-sink dead ends, too-straight trunk
segments, and major drainage paths accidentally left in `tributaries`. The
stress recipe intentionally covers more of the patch and should not be treated
as the desired default composition. Revision 14 intentionally prunes some stress
trunk coverage to reduce the obvious parallel-corridor failure. Revision 15
allows tributaries to carry more of the broad stress footprint, so use
`river-mask.png` and `final.png` for reach review and `river-trunk.png` for
mainstem continuity. Revision 16 keeps that split but rejects rendered trunk
branches that would create disconnected high-strength components. The current
stress capture should read as a clean connected mainstem with attached
tributaries and visible width variation; the 1025 stress image is the best
current stress read.

The current capture set intentionally keeps the lesson from the reverted
revision 4 graph-routing attempt without rendering graph edges directly.
Revision 11 routes over the priority-flood-repaired hidden drainage surface,
publishes `routing_fill_delta`, keeps the default product on traced
trunk/tributary channels instead of raw support-cell painting, and converts
selected grid paths into flow-guided sub-cell centerlines before rasterization.
Prior stress revisions painted connected support paths through the normal
channel pipeline instead of painting raw support cells or multiple unrelated
corridors, then spaced support confluences and reduced low-order branch
clutter. Revision 12 loosens default tributary/order-seed selection and lets
order-seed paths trace farther upstream, then restores more stress support
coverage while rejecting support paths that run too near previously accepted
support geometry. Revision 13 promotes major visible stress paths into
`river_trunk` using visible length plus stream-order/discharge metrics, then
widens and softens the stress trunk band so the hierarchy is visible without
failing high-strength straight-run guards. Revision 14 adds a promoted-trunk
skeleton distinctness check so later trunk promotions must add visible area away
from the current skeleton; redundant near-parallel paths are skipped instead of
being promoted. Revision 15 adds stress reach and continuity regressions, keeps
extra stress trunk promotion disabled, restores the longer accumulation-trunk
candidate, and paints connected basin-tree paths into `tributaries` until the
visible review footprint broadens. Revision 16 promotes stress trunk branches
only after a temporary rendered-field connectivity check passes, adds direction
and straight-run gates for promoted candidates, and pre-curves stress support
paths before rasterization. Revision 17 returns to graph-first topology, but
keeps the lesson from the rejected revision 4 attempt: graph edges are a source
plan and diagnostic, not a product mask. Accepted graph paths still go through
the channel rasterization pipeline, and merged tributary segments are painted
once so shared downstream paths do not become parallel offset copies. The
rejected revision 4 attempt made the visible product worse by rendering selected
graph edges directly, producing disconnected snippets and hard straight or
diagonal runs. See
`docs/notes/terrain-river-graph-routing-attempt.md` for the retained learnings.

## Current Limitations

The active river no longer depends on an authored center line, and the visible
trunk/mask now use a padded hidden routing domain instead of treating the review
patch as the whole watershed. Revision `17` routes accumulation with continuous
D-Infinity-style flow angles and fractional receivers over the repaired routing
surface, selects active channels from connected `stream_order` support, accepts
extra branches only when they visibly terminate at an existing active channel,
and converts visible paths to de-gridded centerlines. The default recipe still
follows that route. The stress recipe now uses a deterministic graph-first
topology source and graph discharge for channel width, but remains intentionally
better for artifact hunting than composition review.

Remaining limitations are now concentrated in the river source model. The
revision 17 graph is still a jittered-grid/k-nearest graph, not a Poisson-disc
plus Delaunay river graph, evolved hydraulic network, lake/breach router, or
erosion model. Some branch joins still read angular, and the graph can still
produce short spurs or sparse local reach in parts of the map. The next
river-quality pass should improve graph topology construction before adding
more high-level terrain features.

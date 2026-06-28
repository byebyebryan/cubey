# Terrain Reboot Current Captures

This note records the current terrain reboot capture set after the revision 15
stress basin-network reach pass.

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
paths across the patch through the same de-gridded channel pipeline. Revision 12
restores some active network coverage after the first degrid/pruning pass while
spacing accepted support paths to avoid near-duplicate branch bundles. Revision
13 keeps the same routing source but promotes major stress support/order-seed
and high-scoring branch paths into `river_trunk`, with a softer/wider stress
trunk band to avoid long high-strength straight runs. Revision 14 keeps the
stress-only hierarchy but rejects promoted candidates that mostly run beside the
existing trunk skeleton without adding distinct visible drainage area. Revision
15 keeps the stress trunk as a continuous mainstem and grows broader connected
basin-tree tributaries from edge-biased stream-order candidates.

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
- `sink-mask.png`: visible crop outlets and true terminal routing cells, useful
  for spotting where the larger hidden routing domain leaves the review patch.
- `river-trunk.png`: soft active main-channel product field traced from a
  connected stream-order support corridor, converted to a sub-cell flow-guided
  centerline, relaxed over drainage potential, and rasterized as channel
  segments. In the stress set, this should now read as one continuous mainstem
  rather than disconnected promoted branch fragments.
- `tributaries.png`: conservative connected branch field feeding the trunk,
  with the stress recipe using basin-tree growth to carry broad diagnostic
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
mainstem continuity.

The current capture set intentionally keeps the lesson from the reverted
revision 4 graph-routing attempt without rendering graph edges directly.
Revision 11 routes over the priority-flood-repaired hidden drainage surface,
publishes `routing_fill_delta`, keeps the default product on traced
trunk/tributary channels instead of raw support-cell painting, and converts
selected grid paths into flow-guided sub-cell centerlines before rasterization.
The stress recipe still paints connected support paths through the normal
channel pipeline instead of painting raw support cells or multiple unrelated
corridors, but it now spaces support confluences and reduces low-order branch
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
visible review footprint broadens. The
rejected revision 4 attempt made the visible product worse by rendering selected
graph edges directly, producing disconnected snippets and hard straight or
diagonal runs. See
`docs/notes/terrain-river-graph-routing-attempt.md` for the retained learnings.

## Current Limitations

The active river no longer depends on an authored center line, and the visible
trunk/mask now use a padded hidden routing domain instead of treating the review
patch as the whole watershed. Revision `15` routes accumulation with continuous
D-Infinity-style flow angles and fractional receivers over the repaired routing
surface, selects active channels from connected `stream_order` support, accepts
extra branches only when they visibly terminate at an existing active channel,
converts visible paths to de-gridded centerlines, and gives the stress recipe a
basin-convergent routing source plus extra but spaced connected support paths.
The stress recipe now keeps its trunk continuous and uses connected tributary
basin growth for broader diagnostic reach, but remains intentionally better for
artifact hunting than composition review.

Remaining limitations are now concentrated in hydrology, basin hierarchy, and
corridor scoring rather than only flow accumulation. Stress captures can still
expose straight-ish tributary strokes and side clusters, and the source can
still look too schematic without breach routing, process erosion, or better
basin selection. The next river-quality pass should evaluate breach routing and
process erosion references, then improve organic tributary curvature, stress
performance, and default composition scoring.

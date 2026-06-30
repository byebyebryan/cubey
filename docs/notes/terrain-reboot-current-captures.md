# Terrain Reboot Current Captures

This note records the current terrain reboot capture set after the revision 23
river terrain-coupling pass.

## Capture Command

```sh
./build/dev/projects/terrain/terrain --headless --grid-size 513 --terrain-debug-view all --terrain-output-dir outputs/terrain/current-river-network
./build/dev/projects/terrain/terrain --headless --grid-size 513 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network
./build/dev/projects/terrain/terrain --headless --grid-size 1025 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network-1025
./build/dev/projects/terrain/terrain --headless --grid-size 513 --recipe temperate-mountain-range-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/mountain-range-stress
./build/dev/projects/terrain/terrain --headless --grid-size 1025 --recipe temperate-mountain-range-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/mountain-range-stress-1025
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river --terrain-camera-preset oblique --terrain-preview-color material --output outputs/terrain/current-river-network/river-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river --terrain-camera-preset profile --terrain-preview-color material --output outputs/terrain/current-river-network/river-profile.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river --terrain-camera-preset oblique --terrain-preview-color height --output outputs/terrain/current-river-network/river-height-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river --terrain-camera-preset oblique --terrain-preview-color channel --output outputs/terrain/current-river-network/river-channel-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river-stress --terrain-camera-preset oblique --terrain-preview-color material --output outputs/terrain/stress-river-network/river-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river-stress --terrain-camera-preset profile --terrain-preview-color material --output outputs/terrain/stress-river-network/river-profile.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river-stress --terrain-camera-preset oblique --terrain-preview-color height --output outputs/terrain/stress-river-network/river-height-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river-stress --terrain-camera-preset oblique --terrain-preview-color channel --output outputs/terrain/stress-river-network/river-channel-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --output outputs/terrain/mountain-range-stress/mountain-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset profile --output outputs/terrain/mountain-range-stress/mountain-profile.png
```

The primary review images are `513x513` PNGs under `outputs/`, with `1025x1025`
stress captures for larger river-network and mountain-driver inspection. The
revision 23 scalar review set emits 37 PNG views per capture. Current and
stress river directories hold 41 PNGs after material/profile, height-only, and
channel diagnostic perspective captures are generated. The 513 mountain stress
directory now also includes `mountain-perspective.png` and
`mountain-profile.png` from the renderer-backed preview app, so it holds 39 PNGs
after the full mountain review command sequence. The 1025 mountain stress set
remains scalar-only unless a matching preview capture is explicitly generated.
`outputs/` is intentionally ignored by git. This replaced the earlier tiny
local output set so field structure, channel continuity, and material response
are easier to inspect.
`outputs/terrain/current-river-network` is the default product review. The optional
`outputs/terrain/stress-river-network` set uses the diagnostic stress recipe to
stress-test broader river-network shape and coverage. The optional
`outputs/terrain/mountain-range-stress` set uses the isolated mountain source
profile to inspect range support, ridge structure, peak candidates, and peak
uplift without retuning the river graph stress recipe. Its primary rendered
inspection image is `mountain-relief.png`; `final.png` remains the normal
river/material product composition. Revision 12
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
Revision 18 keeps the field contract unchanged but interprets `river_trunk` as
major channels: the mainstem plus selected high-discharge or high-order graph
tributaries. `tributaries` holds smaller attached branches. The graph fields
stay diagnostic source views. This graph is still patch-local over the padded
hidden domain; later world-scale terrain work should move topology planning into
deterministic world-coordinate basin graph data and let local products rasterize
only the tile plus halo.
Revision 19 expands the product contract with explicit mountain fields:
`mountain_support`, `ridge_support`, `peak_support`, `mountain_uplift`, and
`peak_uplift`. The existing river recipes emit those fields while keeping broad
mountain and peak uplift disabled; the new `temperate-mountain-range-stress`
recipe opts into them for mountain-driver review.
Revision 20 adds `mountain_range_spine`, `mountain_ridge_hierarchy`, and
`mountain_peak_candidates` as source diagnostics for the mountain stress recipe.
The default river recipes keep range spine and peak candidates at zero.
Revision 21 adds `mountain_envelope`, `mountain_peak_anchors`,
`mountain_peak_prominence`, `mountain_ridge_skeleton`, and
`mountain_ridge_influence`. The mountain stress recipe now derives support,
ridge hierarchy, and peak support from that envelope/anchor/skeleton source
path instead of treating layered ridged noise as the primary form driver. The
default river recipes keep these new stress-only source diagnostics inactive.
Revision 22 keeps the same field contract but makes the mountain stress recipe
visibly build into high peaks. The stress recipe now uses an envelope-driven
base elevation instead of the generic regional tilt, gives peak uplift a larger
height role, broadens ridge influence, gates residual detail against mountain
structure, and retunes `mountain-relief.png` to prioritize elevation hierarchy
over high-contrast shadow texture.
Revision 23 expands the product contract with `pre_process_height_m`,
`channel_incision`, and `valley_incision`, then publishes `height_m` as the
river-carved final surface. Slope, local relief, material masks, wetness,
deposition, and vegetation potential are recomputed against that final height,
so active rivers are terrain-form drivers instead of only color overlays.

## What To Inspect

- `final.png`: debug composition of height, material masks, slope shade, and
  active river/wetness response.
- `mountain-relief.png`: mountain-specific rendered review image. It uses an
  elevation-first ramp with softer hillshade and subtle ridge/peak tinting,
  without river, wetness, vegetation, or material overlays.
- `mountain-perspective.png`: renderer-backed oblique mesh capture from
  `terrain_preview`. Use this to judge whether broad support, basins, valleys,
  ridges, and high peaks are readable in 3D.
- `mountain-profile.png`: renderer-backed low side view from `terrain_preview`.
  Use this to check height contrast and to expose the current sharp-peak
  character that flat scalar PNGs can hide.
- `river-perspective.png`: renderer-backed material view for current or stress
  river recipes. It is useful for reviewing the normal water/material read, but
  it should be paired with height/channel modes so water color does not hide
  product-shape failures.
- `river-height-perspective.png`: renderer-backed height-only river view. Use
  this to check whether channels are visible in geometry without river tint.
- `river-channel-perspective.png`: renderer-backed channel diagnostic view. The
  gold bands show where channel and valley incision were applied to the mesh.
- `pre-process-height.png`: source surface before river carving. Compare with
  `height.png` to see what the river process changed.
- `channel-incision.png` and `valley-incision.png`: scalar incision fields that
  should align with active river masks while spreading enough to read as carved
  beds and valley shoulders rather than single-pixel painted lines.
- `mountain-envelope.png`: smooth macro mountain support. It should show broad
  uplift regions before ridges, peaks, or residual detail are applied.
- `mountain-peak-anchors.png`: sparse deterministic summit anchors selected
  from the envelope and summit score. It should not read as a noisy full-field
  mask.
- `mountain-peak-prominence.png`: peak dominance grown from anchors. It should
  explain where peak uplift comes from, even though revision 22 still leaves it
  mostly radial before later anisotropic shaping.
- `mountain-ridge-skeleton.png`: generated primary/secondary ridge source. It
  is a structural debug view, not a final surface overlay.
- `mountain-ridge-influence.png`: widened shoulder field derived from the ridge
  skeleton. It should connect peak anchors into readable ridge mass before
  support and uplift fields consume it.
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
- `river-graph-plan.png`: stress source topology before channel
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
- `mountain-range-spine.png`: macro range-band source. It should read as
  coherent range organization from noise, not as an authored line, circle, or
  quadrant mask.
- `mountain-support.png`: broad range mask. It should read as regional mountain
  mass, not as isolated authored circles or quadrant blocks.
- `mountain-ridge-hierarchy.png`: ranked primary/secondary ridge source inside
  the range support. It should be more organized than full-map ridged noise, but
  it is still expected to look busy before erosion exists.
- `ridge-support.png`: ridged structure gated by mountain support. It should
  show coherent ridge webs inside broad support, not full-map noise.
- `mountain-peak-candidates.png`: sparse summit candidates attached to high
  range and ridge hierarchy before final peak support shaping.
- `peak-support.png`: localized summit accents derived from the coherent ridge
  fields. It should stay sparse and attached to ridge regions.
- `mountain-uplift.png`, `ridge-uplift.png`, and `peak-uplift.png`: height
  contributions from the support fields. In revision 22, `peak-uplift.png`
  should be strong enough to explain the highest terrain.
- `height.png` and `slope.png`: combined terrain shape and derivative response.
  In revision 23, `height.png` is the final carved height product. In the
  mountain stress recipe, pair these scalar views with
  `mountain-relief.png` before judging whether the driver creates recognizable
  range mass before biome or glacial polish.

For `outputs/terrain/stress-river-network`, look for failures that the smaller
default network may hide: repeated parallel channels, schematic branch fans,
disconnected-looking tributaries, local-sink dead ends, too-straight trunk
segments, and major drainage paths accidentally left in `tributaries`. The
stress recipe intentionally covers more of the patch and should not be treated
as the desired default composition. Revision 14 intentionally prunes some stress
trunk coverage to reduce the obvious parallel-corridor failure. Revision 15
allows tributaries to carry more of the broad stress footprint, so use
`river-mask.png` and `final.png` for reach review and `river-trunk.png` for
major-channel hierarchy. Revision 16 keeps that split but rejects rendered trunk
branches that would create disconnected high-strength components. Revision 18
should read as a clean connected major-channel network with smaller attached
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
revision 18 hierarchy pass promotes selected graph paths into `river_trunk`
based on graph discharge, stream order, visible length, and novelty against the
current trunk skeleton, then leaves the remaining graph paths in `tributaries`.
Revision 19 keeps the river graph path unchanged and adds mountain source fields
as product diagnostics instead of hiding them inside `ridge_uplift`. Revision 20
adds range spine, ridge hierarchy, and peak-candidate diagnostics before
turning the stress recipe into an alpine biome. Revision 21 adds a peak-first
mountain skeleton source path and derives the existing mountain support/ridge
fields from it, so the source hierarchy can be reviewed before erosion or
material polish. Revision 22 changes the stress height composition and relief
review image so the generated range reads more clearly as broad support
building into high peaks. Revision 23 changes the height product contract: the
river network now carves channel and valley incision into the final surface.
The scalar source/final height pair plus renderer height/channel modes are now
the required way to check that rivers are not merely blue material overlays.
The rejected revision 4 attempt made the visible product worse by rendering selected
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
topology source, graph discharge for channel width, and major-tributary
promotion for `river_trunk`, but remains intentionally better for artifact
hunting than composition review.

Remaining limitations are now concentrated in the river source model. The
revision 18 graph is still a jittered-grid/k-nearest graph, not a Poisson-disc
plus Delaunay river graph, evolved hydraulic network, lake/breach router, or
erosion model. Some branch joins still read angular, and the graph can still
produce short spurs or sparse local reach in parts of the map. The 1025 stress
capture also shows the patch-local limitation: some promoted major channels are
cut by the visible crop without the larger basin context that would make their
upstream/downstream role clearer. The next river-quality pass should improve
graph topology construction before adding more high-level terrain features.

The revision 23 river incision is a deterministic field-propagation pass over
the active river product, not erosion. It gives the mesh visible channels and
valley shoulders, but it does not yet solve bed-profile monotonicity, bank
shape, sediment transport, lakes, floodplains, or terrace formation.

The current mountain driver is still an early diagnostic source profile, not
a polished alpine biome. It now makes peak hierarchy more visible in
`height.png`, `mountain-relief.png`, `mountain-perspective.png`, and
`mountain-profile.png`, but peak prominence remains mostly radial and the
skeleton is generated rather than erosion-evolved. The perspective preview is a
local heightfield mesh consumer; it does not yet include clipmaps, tiled world
continuity, water surfaces, foliage, or planet integration. It does not yet
model tectonic plates, erosion time, talus, snow/ice, glacial valley carving,
or a world-scale range graph. The next mountain-quality pass should focus on
anisotropic peak shaping, erosion-aware ridge cleanup, and alpine
material/valley contrast before turning these fields into final biome
compositions.

# Terrain

`projects/terrain` is the rebooted terrain workbench. It starts as a local
CPU/reference terrain product generator, not as a direct continuation of
`terrain_lab_legacy`, not as a coastal/ocean demo, and not as a planet renderer.

The first goal is a deterministic product contract that downstream systems can
inspect and eventually consume:

- coherent source fields;
- terrain feature and process fields;
- material and vegetation-potential hints;
- summaries and debug exports.

The current first slice is a temperate mountain river catchment over a local
kilometer-scale grid. A renderer-backed preview app now consumes the product for
perspective review, but final terrain rendering, ocean integration, planet
streaming, foliage rendering, and physically complete erosion remain deferred
until the product fields are credible.

The current generator revision is `26`. It emits source fields, height/slope
analysis, static drainage, routing diagnostics, smoothed active river trunk and
tributary fields, incision/process diagnostics, wetness/deposition, material
masks, and vegetation potential.
The drainage pass now repairs local routing pits with a bounded priority-flood
epsilon fill. River topology still uses D8 graph traversal where that is useful
for connectivity, but selected channel paths are converted to sub-cell
centerlines, nudged by continuous flow direction, and rasterized with bounded
meander before they become product masks. Revision 12 restores more active
network coverage by allowing more default tributary/order-seed candidates and by
spacing stress support paths against already accepted support geometry, not only
against confluence points. Revision 13 keeps that routing model but promotes
major stress support, order-seed, and high-scoring branch paths into
`river_trunk`, then tunes the stress trunk band to stay broad at review
thresholds without recreating long high-strength straight runs. Revision 14
keeps the stress-only hierarchy but requires promoted branches to add distinct
visible area away from the current trunk skeleton, pruning nearby parallel
alternatives that read as repeated routes through the same corridor. This
remains process-informed rather than a full hydraulic simulation. Revision 15
keeps the stress trunk as a continuous mainstem, restores the longer
accumulation trunk candidate, and grows a connected basin-tree tributary network
from edge-biased stream-order candidates so the stress review exposes broader
river reach without rendering unrelated graph edges. Revision 16 makes stress
trunk promotion provisional at render time: candidates are accepted only if the
painted trunk remains dominated by one connected component. It also tightens
grid-aligned support caps and pre-curves connected support/order-seed/corridor
branches before rasterization to reduce obvious D8-looking tributary strokes.
Revision 17 pivots the stress recipe away from raster-routing-derived topology:
stress topology now comes from a deterministic jittered river graph over the
padded hidden domain, computes graph discharge/order, and then rasterizes
accepted graph paths through the existing channel pipeline. D-Infinity
accumulation remains a diagnostic and validation field, not the stress recipe's
visible source of truth.
Revision 18 keeps the public field contract stable but changes stress hierarchy:
`river_trunk` now means major channels, so selected high-discharge/high-order
graph tributaries are painted with the mainstem while smaller attached branches
remain in `tributaries`.
Revision 19 adds explicit mountain source products: broad mountain support,
ridge support, peak support, mountain uplift, and peak uplift. The new
`temperate-mountain-range-stress` recipe is an isolated mountain-driver review
slice; existing river recipes emit the fields but keep broad mountain and peak
uplift disabled for stability. Use `mountain-relief.png` as the primary rendered
review image for that recipe; `final.png` remains the river/material debug
composition.
Revision 20 adds a mountain hierarchy layer for that stress recipe:
`mountain_range_spine`, `mountain_ridge_hierarchy`, and
`mountain_peak_candidates`. The default river recipes keep range spine and peak
candidate fields disabled while preserving their existing ridge source.
Revision 21 pivots the stress recipe to a peak-first mountain skeleton:
`mountain_envelope`, `mountain_peak_anchors`, `mountain_peak_prominence`,
`mountain_ridge_skeleton`, and `mountain_ridge_influence`. The older mountain
support, ridge hierarchy, and peak-candidate fields are now derived from that
envelope/anchor/skeleton source path in the mountain stress recipe. Default
river recipes still emit inactive values for the new stress-only diagnostics.
Revision 22 keeps that field contract but makes the mountain stress recipe read
more clearly as terrain building into high peaks. It replaces the generic
base-elevation tilt with an envelope-driven mountain base, strengthens peak
uplift, broadens ridge shoulders, gates residual detail against the mountain
source fields, and retunes `mountain-relief.png` around elevation hierarchy.
Revision 23 makes active rivers modify the terrain height product. It preserves
`pre_process_height_m`, applies channel and valley incision driven by the river
fields, publishes the carved result as `height_m`, and recomputes slope, local
relief, material masks, and vegetation potential from that carved height.
Renderer-backed previews now support material, height, river, and channel color
modes so reviewers can separate geometry from river tint.
Revision 24 adds a clean-room mountain gully diagnostic for
`temperate-mountain-range-stress`: `erosion_delta_m`, `gully_mask`,
`crease_proxy`, and `post_erosion_height_m`. These fields are review-only; they
do not modify `height_m`, materials, rivers, wetness, or vegetation.
Revision 25 adds explicit mountain macro fields for the same stress recipe:
`mountain_mass`, `mountain_shoulder`, and `mountain_summit_core`. The mountain
source now separates broad highland mass, foothill/shoulder buildup, and sparse
summit cores before local detail is applied. `terrain_preview` can also render
`height`, `post-erosion`, or `pre-process` surfaces so diagnostic surfaces can
be compared without changing the product contract.
Revision 26 changes the mountain stress recipe from additive feature stacking
to a coherent height profile. `mountain_profile_height_m` is now the source
height for that recipe, ridge/peak/uplift fields are diagnostics and bounded
attribution, and visible ridges come from smooth connection influence instead
of 8-neighbor raster paths. This removes the worst flat shoulder shelves,
needle peaks, and jagged ridge strokes from the revision 25 preview.

The current foundation pass keeps terrain process math in terrain-local helpers:
spread, relief-clamped lowering, height lowering, and the diagnostic gully pass.
The scalar review export writes `manifest.json` with recipe, grid, summary,
field stats, view names, and output filenames.

See [Terrain reboot direction](../../docs/architecture/terrain-reboot.md) for
the current design checkpoint. The staged lane map is captured in
[Terrain project map](../../docs/notes/terrain-project-map.md): source drivers,
process operators, product fields, review consumers, and integration adapters.
The ShaderToy terrain/hydro extraction boundary is captured in
[Terrain ShaderToy operator extraction](../../docs/notes/terrain-shadertoy-operator-extraction.md):
use those refs for clean-room process diagnostics and visual vocabulary, not
river topology or shader ports.

## Commands

```sh
cmake --build --preset dev --target cubey_project_terrain cubey_project_terrain_preview cubey_project_terrain_tests
ctest --preset dev -R terrain --output-on-failure

./build/dev/projects/terrain/terrain
./build/dev/projects/terrain/terrain --headless --terrain-debug-view final --output outputs/terrain/current/final.png
./build/dev/projects/terrain/terrain --headless --terrain-debug-view flow-accumulation --grid-size 129 --output outputs/terrain/current/flow-accumulation.png
./build/dev/projects/terrain/terrain --headless --grid-size 513 --terrain-debug-view all --terrain-output-dir outputs/terrain/current-river-network
./build/dev/projects/terrain/terrain --headless --grid-size 513 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network
./build/dev/projects/terrain/terrain --headless --grid-size 1025 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network-1025
./build/dev/projects/terrain/terrain --headless --grid-size 513 --recipe temperate-mountain-range-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/mountain-range-stress
./build/dev/projects/terrain/terrain --headless --grid-size 1025 --recipe temperate-mountain-range-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/mountain-range-stress-1025
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river --terrain-camera-preset oblique --terrain-preview-color material --output outputs/terrain/current-river-network/river-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river --terrain-camera-preset oblique --terrain-preview-color height --output outputs/terrain/current-river-network/river-height-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river --terrain-camera-preset oblique --terrain-preview-color channel --output outputs/terrain/current-river-network/river-channel-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river-stress --terrain-camera-preset oblique --terrain-preview-color material --output outputs/terrain/stress-river-network/river-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river-stress --terrain-camera-preset oblique --terrain-preview-color channel --output outputs/terrain/stress-river-network/river-channel-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface height --output outputs/terrain/mountain-range-stress/mountain-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset profile --terrain-preview-surface height --output outputs/terrain/mountain-range-stress/mountain-profile.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface post-erosion --terrain-preview-color height --output outputs/terrain/mountain-range-stress/mountain-post-erosion-perspective.png
./build/dev/projects/terrain/terrain_preview --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface height --terrain-preview-color height --output outputs/terrain/mountain-range-stress/mountain-height-perspective.png
```

## Current Review Outputs

Use `--terrain-debug-view all --terrain-output-dir outputs/terrain/current-river-network`
for the standard review set. The current local review images are generated at
`513x513`, large enough to inspect the field structure rather than just a tiny
thumbnail. `outputs/` is ignored by git, so this directory is a disposable local
review artifact.

The scalar review set includes 45 PNG views plus `manifest.json`:

- `final.png`
- `mountain-relief.png`
- `height.png`
- `pre-process-height.png`
- `mountain-profile-height.png`
- `slope.png`
- `erosion-delta.png`
- `gully-mask.png`
- `crease-proxy.png`
- `post-erosion-height.png`
- `mountain-envelope.png`
- `mountain-peak-anchors.png`
- `mountain-peak-prominence.png`
- `mountain-ridge-skeleton.png`
- `mountain-ridge-influence.png`
- `mountain-range-spine.png`
- `mountain-mass.png`
- `mountain-shoulder.png`
- `mountain-summit-core.png`
- `mountain-support.png`
- `mountain-ridge-hierarchy.png`
- `ridge-support.png`
- `mountain-peak-candidates.png`
- `peak-support.png`
- `mountain-uplift.png`
- `ridge-uplift.png`
- `peak-uplift.png`
- `drainage-potential.png`
- `routing-fill-delta.png`
- `flow-direction.png`
- `flow-accumulation.png`
- `stream-order.png`
- `river-graph-plan.png`
- `river-graph-discharge.png`
- `river-mask.png`
- `river-trunk.png`
- `tributaries.png`
- `sink-mask.png`
- `channel-width.png`
- `channel-incision.png`
- `valley-incision.png`
- `wetness.png`
- `deposition.png`
- `material.png`
- `vegetation.png`

`terrain_preview` is a separate renderer-backed consumer for perspective
review. It turns the selected `TerrainRegionProduct` height field into a lit
mesh through the normal Vulkan windowed/headless app path. For the mountain
stress recipe, `mountain-perspective.png` is the primary 3D read for peak,
basin, and valley hierarchy, while `mountain-profile.png` is a lower side view
for checking whether peak height and valley contrast are plausible.
`mountain-post-erosion-perspective.png` renders the diagnostic
`post_erosion_height_m` surface with height color so gully detail can be
compared against the actual `height_m` product. The current
`outputs/terrain/mountain-range-stress` directory holds 49 PNGs after the
scalar set plus oblique, profile, post-erosion, and height-colored perspective
captures are generated. `manifest.json` is not a rendered view; use it to check
the recipe, seed, generator revision, grid size, field ranges, and content hash
for a scalar capture directory.

The optional `temperate-mountain-river-stress` recipe keeps the same source
terrain and routing diagnostics but uses a graph-first visible river source for
review stress testing. Earlier revisions used a stronger basin-grade routing
profile, selected one connected support basin, spaced accepted support
confluences, and painted extra support paths through the same de-gridded channel
pipeline. Revision 12 also
spaces support paths against previously accepted support paths so
`outputs/terrain/stress-river-network` exposes more of the patch to
river-network artifacts without rendering unrelated watershed clusters.
Revision 13 adds a stress-only hierarchy pass so major branches read as a
branching trunk skeleton while smaller attached paths remain tributaries.
Revision 14 rejects near-parallel promoted branches that do not add distinct
visible drainage area, so the stress trunk review is sparser but less clustered
around one corridor. Revision 15 shifts broad stress reach into connected
basin-tree tributaries while keeping `river_trunk` as the continuous mainstem;
this exposes more network coverage without turning side branches into
disconnected trunk fragments. Revision 16 keeps that hierarchy but rejects
rendered trunk promotions that would fragment the trunk and filters the worst
straight support paths before they become visible tributaries.
Revision 17 replaces the stress recipe's visible topology source with a
deterministic graph-first drainage tree, then paints accepted graph paths
through the same smoothing, lateral-offset, relaxation, and width pipeline. It
also exports `river-graph-plan.png` and `river-graph-discharge.png` so the
source topology can be reviewed independently from the rendered river mask.
Revision 18 promotes selected major graph tributaries into `river_trunk` while
leaving smaller graph paths in `tributaries`.

The optional `temperate-mountain-range-stress` recipe uses the same product
contract to review mountain shape independently from the river stress graph. It
adds broad mountain mass from `mountain-support.png`, sharper ridge structure
from `ridge-support.png`, localized summit accents from `peak-support.png`, and
routes over a softened combination of those uplifts without including fine
detail. The intended first-pass visual review is `mountain-relief.png`, which
hillshades height and tints the mountain source fields without river, wetness,
vegetation, or material overlays. `final.png` is still useful as a product
composition check, but it is not the right image for judging mountain form.
Revision 20 makes the source hierarchy inspectable: `mountain-range-spine.png`
shows broad range organization, `mountain-ridge-hierarchy.png` shows ranked
primary/secondary ridge structure, and `mountain-peak-candidates.png` shows
sparse summit candidates before they become peak support.
Revision 21 changes the primary review order for this recipe. Inspect
`mountain-relief.png` first, then compare `mountain-envelope.png`,
`mountain-peak-anchors.png`, `mountain-peak-prominence.png`,
`mountain-ridge-skeleton.png`, and `mountain-ridge-influence.png`. The raw
skeleton view is a source diagnostic; the relief view intentionally does not
draw raw graph strokes over the surface.
Revision 22 keeps `mountain-relief.png` as the primary visual review image, but
it now uses a clearer elevation ramp and softer hillshade. For hierarchy review,
inspect `mountain-relief.png`, `height.png`, `mountain-envelope.png`,
`mountain-ridge-influence.png`, `mountain-peak-prominence.png`, and
`peak-uplift.png` together. For height readability, inspect
`mountain-perspective.png` and `mountain-profile.png` after the scalar views;
these are renderer-backed mesh captures, not CPU software perspective exports.
Revision 24 adds diagnostic erosion-shaped fields for the same recipe. Inspect
`erosion-delta.png`, `gully-mask.png`, `crease-proxy.png`, and
`post-erosion-height.png` next to `height.png` and `mountain-perspective.png`.
Revision 25 adds `mountain-mass.png`, `mountain-shoulder.png`, and
`mountain-summit-core.png`. Inspect those before judging summit detail: the
intended read is broad highland mass, shoulder buildup, then sparse summit core.
Revision 26 adds `mountain-profile-height.png` and makes that field the
coherent source height for the mountain stress recipe. Inspect
`mountain-profile-height.png`, `pre-process-height.png`, and
`mountain-perspective.png` together: pre-process height should differ from the
profile mostly by bounded detail, not by independent ridge/peak uplift stacks.
The regenerated 513 and 1025 mountain review sets report generator revision 26,
51 fields, and 45 scalar views. The 513 manifest reports
`height_m.span = 1895.587`, `mountain_profile_height_m.span = 1767.296`,
`mountain_mass.mean = 0.4200`, `mountain_shoulder.mean = 0.3854`, and
`mountain_summit_core.mean = 0.0420`.
Both stress recipes are diagnostic recipes, not the default product target.

The active river fields come from a coherent low-frequency drainage potential
plus routed flow accumulation over a padded hidden routing domain. Revision `17`
routes over a priority-flood-repaired drainage surface and exposes the
raw-to-repaired delta as `routing_fill_delta`. Continuous D-Infinity-style flow
angles and fractional accumulation remain the diagnostic catchment path, while
`stream_order` selects connected support for active channels. The default recipe
now avoids raw support-cell painting and keeps a traced trunk plus attached
branch network. Additional branches are accepted only when they visibly reach an
existing active channel, which avoids independent local strokes and straight
snap connectors. Before rasterization, grid-selected paths are resampled into
sub-cell centerlines, smoothed, constrained by the continuous flow field, and
given discharge/stream-order width and strength variation. Revisions 14 through
16 tried to improve the stress recipe by pruning near-parallel support paths,
gating rendered trunk connectivity, and pre-curving support branches. Revision
17 keeps the default path but switches stress river masks to the graph-first
source model, using graph discharge to drive stress channel and valley widths.
Revision 18 adds graph-major-channel promotion to that stress path. Some
tributary joins can still read too angular because this is not yet a
Delaunay/Poisson river graph, hydraulic erosion pass, lake/breach routing model,
or tiled world-coordinate basin graph. See
[Terrain routing repair plan](../../docs/notes/terrain-routing-repair-plan.md).
Revision 23 makes those active river fields terrain-form drivers by carving
broad valley incision and narrower channel incision into `height_m`. Use
`pre-process-height.png` to inspect the source surface, `height.png` to inspect
the carved product, and `channel-incision.png` / `valley-incision.png` plus
`river-channel-perspective.png` to verify where the network actually cuts the
mesh. `river-height-perspective.png` is intentionally color-neutral; if a river
only appears in `river-perspective.png`, the product is back to a texture-overlay
failure.

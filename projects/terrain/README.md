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
kilometer-scale grid. Rendering, ocean integration, planet streaming, foliage
rendering, and physically complete erosion are deferred until the product fields
are credible.

The current generator revision is `16`. It emits source fields, height/slope
analysis, static drainage, routing diagnostics, smoothed active river trunk and
tributary fields, wetness/deposition, material masks, and vegetation potential.
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

See [Terrain reboot direction](../../docs/architecture/terrain-reboot.md) for
the current design checkpoint.

## Commands

```sh
cmake --build --preset dev --target cubey_project_terrain cubey_project_terrain_tests
ctest --preset dev -R terrain --output-on-failure

./build/dev/projects/terrain/terrain
./build/dev/projects/terrain/terrain --headless --terrain-debug-view final --output outputs/terrain/current/final.png
./build/dev/projects/terrain/terrain --headless --terrain-debug-view flow-accumulation --grid-size 129 --output outputs/terrain/current/flow-accumulation.png
./build/dev/projects/terrain/terrain --headless --grid-size 513 --terrain-debug-view all --terrain-output-dir outputs/terrain/current
./build/dev/projects/terrain/terrain --headless --grid-size 513 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network
```

## Current Review Outputs

Use `--terrain-debug-view all --terrain-output-dir outputs/terrain/current` for
the standard review set. The current local review images are generated at
`513x513`, large enough to inspect the field structure rather than just a tiny
thumbnail. `outputs/` is ignored by git, so this directory is a disposable local
review artifact.

The review set includes:

- `final.png`
- `height.png`
- `slope.png`
- `ridge-uplift.png`
- `drainage-potential.png`
- `routing-fill-delta.png`
- `flow-direction.png`
- `flow-accumulation.png`
- `stream-order.png`
- `river-mask.png`
- `river-trunk.png`
- `tributaries.png`
- `sink-mask.png`
- `channel-width.png`
- `wetness.png`
- `deposition.png`
- `material.png`
- `vegetation.png`

The optional `temperate-mountain-river-stress` recipe keeps the same source
terrain and routing diagnostics but expands active channel extraction for review
stress testing. It uses a stronger basin-grade routing profile, selects one
connected support basin, spaces accepted support confluences, and paints extra
support paths through the same de-gridded channel pipeline. Revision 12 also
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
Treat it as a diagnostic recipe, not the default product target.

The active river fields come from a coherent low-frequency drainage potential
plus routed flow accumulation over a padded hidden routing domain. Revision `16`
routes over a priority-flood-repaired drainage surface and exposes the
raw-to-repaired delta as `routing_fill_delta`. Continuous D-Infinity-style flow
angles and fractional accumulation remain the diagnostic catchment path, while
`stream_order` selects connected support for active channels. The default recipe
now avoids raw support-cell painting and keeps a traced trunk plus attached
branch network. Additional branches are accepted only when they visibly reach an
existing active channel, which avoids independent local strokes and straight
snap connectors. Before rasterization, grid-selected paths are resampled into
sub-cell centerlines, smoothed, constrained by the continuous flow field, and
given discharge/stream-order width and strength variation. The stress recipe
still applies a procedural basin convergence, but now prunes low-value support
branches more aggressively, spaces accepted confluences, and rejects support
paths that run too near previously accepted support paths to reduce parallel
branch fans without starving the network. Revision 14 promoted only distinct
major branches into `river_trunk` and skipped redundant near-parallel paths
against the already promoted trunk skeleton. Revision 15 disables extra stress
trunk promotion again, adds reach and continuity gates, and uses a connected
basin-growth pass to paint long attached tributaries until the visible patch has
a broader review footprint. Revision 16 adds rendered trunk-component gating,
directional promotion checks, and additional pre-curving for stress support
paths. Some tributary runs can still read too schematic because the source
hierarchy remains static routing rather than evolved hydrology. See
[Terrain routing repair plan](../../docs/notes/terrain-routing-repair-plan.md).

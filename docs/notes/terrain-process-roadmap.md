# Terrain Process Roadmap

Date: 2026-06-30

This note captures the current terrain reboot reset point after the river and
mountain driver experiments. The project has useful product plumbing now, but
the next work should move away from per-image tuning and toward reusable terrain
process fields.

## Current Position

`projects/terrain` is the active reboot workbench. It is allowed to replace its
early contracts as long as the data-first direction remains intact:

```text
coherent source fields -> terrain process fields -> product fields -> consumers
```

The current implementation already emits named scalar fields, summaries,
headless PNG review sets, and renderer-backed preview captures. It also has
separate diagnostic recipes for the normal temperate mountain river slice, a
stress river network, and an isolated mountain range.

Those are still diagnostic recipes, not finished biome definitions. Rivers,
mountains, materials, and vegetation should become outputs of shared drivers and
processes, then recipes can combine them into biome slices.

## Lessons To Preserve

- Hand-authored feature masks do not scale. Single lines, disks, centered
  canyons, quadrants, or fixture-like watersheds create obvious artificial
  shapes and do not explain continuation outside the local patch.
- Coherent source fields should drive macro shape. Detail noise is useful only
  after the broad elevation, uplift, drainage, and support fields already read
  correctly.
- The visible PNG is not enough. Every major visual result needs source,
  process, product, and consumer diagnostics so we can see where the artifact
  entered.
- The renderer preview is now useful because it exposes height problems that
  flat scalar images hide. It should remain a consumer of product fields, not
  the terrain source of truth.

## Current Gaps

| Area | Current state | Needed direction |
| --- | --- | --- |
| Process helpers | River incision has local spread/clamp logic inside the generator. | Move reusable terrain-local field operations into a small helper module before broadening erosion, deposition, talus, snow, sand, or wetness work. |
| River carving | Rivers now lower `height_m`, but channel depth still reads weak in 3D and water/material tint can obscure geometry. | Keep incision fields explicit, use manifests to compare field ranges, then tune against height-only and channel preview modes. |
| Mountain form | The stress recipe has envelope, peak, skeleton, and uplift fields, but it can still read noisy or artificial in perspective. | Treat mountains as a hierarchy problem: broad mass, peak anchors, ridge connection, shoulder influence, then local detail. |
| Scale | Rivers and mountains are still patch-local with a padded halo. | Later world/tile work should generate deterministic world-coordinate basin and range sources, then rasterize local products plus halo. |
| Capture evidence | PNG directories are useful but easy to lose track of. | Write machine-readable capture manifests with config, fields, ranges, hashes, and emitted view names. |

## Near-Term Order

1. Add terrain-local process field helpers for spread, clamping, and simple
   lowering/composition.
2. Route river incision through those helpers without changing the public
   product contract.
3. Add capture manifests for scalar review directories so PNGs have field range
   and hash context.
4. Use the new evidence to tune river incision and mountain hierarchy in small
   passes.
5. Only then add another terrain process or water body. Lakes, coast, dunes,
   snow, talus, and foliage eligibility should build on these product/process
   pieces instead of restarting from authored shapes.

## Foundation Boundary

Keep these helpers in `projects/terrain` for now. They are domain-specific
terrain process operations, not yet general procedural foundation APIs. Promote
them into `cubey::procedural` only after more than one consumer needs the same
contract and the names are no longer terrain-specific.

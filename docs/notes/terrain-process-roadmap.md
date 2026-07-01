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
headless PNG review sets, scalar capture manifests, and renderer-backed preview
captures. It also has separate diagnostic recipes for the normal temperate
mountain river slice, a stress river network, and an isolated mountain range.

Those are still diagnostic recipes, not finished biome definitions. Rivers,
mountains, materials, and vegetation should become outputs of shared drivers and
processes, then recipes can combine them into biome slices.

The broader project lane map is captured in
[Terrain project map](terrain-project-map.md). Use that note when choosing
whether a batch belongs to source drivers, process operators, product fields,
review consumers, or integration adapters.

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
| Process helpers | Terrain-local spread, relief-clamped lowering, and height-lowering helpers now exist in `terrain_process_fields`. | Keep new process math routed through helper APIs before broadening erosion, deposition, talus, snow, sand, or wetness work. |
| River carving | Rivers now lower `height_m`, but channel depth still reads weak in 3D and water/material tint can obscure geometry. | Keep incision fields explicit, use manifests to compare field ranges, then tune against height-only and channel preview modes. |
| Mountain form | The stress recipe has envelope, peak, skeleton, and uplift fields, but it can still read noisy or artificial in perspective. | Treat mountains as a hierarchy problem: broad mass, peak anchors, ridge connection, shoulder influence, then local detail. |
| Scale | Rivers and mountains are still patch-local with a padded halo. | Later world/tile work should generate deterministic world-coordinate basin and range sources, then rasterize local products plus halo. |
| Capture evidence | Scalar review directories now write `manifest.json` beside the PNGs. | Use manifest ranges and hashes when comparing captures instead of relying only on manual image inspection. |

## ShaderToy Operator Extraction

The ShaderToy terrain/hydro references are useful, but only when routed through
Cubey's field pipeline. Treat them as compact process-operator and visual
vocabulary references, not as renderer or river-topology donors.

Near-term borrow targets:

- clean-room gully/erosion diagnostics over `height_m`, slope/derivatives, local
  relief, and optional mountain support, producing fields such as
  `erosion_delta_m`, `gully_mask`, and `crease_proxy`;
- later shallow-water/lake relaxation diagnostics over `height_m + water_depth_m`
  with explicit outflow, boundary, and mass-change fields;
- shoreline and water-contact visual vocabulary for future terrain/ocean handoff
  work, such as water-depth tint, wet sand, foam bands, and seabed visibility.

Do not use ShaderToy-authored river curves as river topology. River network
source work should stay tied to the graph/hydrology references, while ShaderToy
informs how the fields are reviewed and composed visually.

The detailed extraction and guardrails are in
[Terrain ShaderToy operator extraction](terrain-shadertoy-operator-extraction.md).

## Near-Term Order

1. Preserve the current river and mountain evidence: scalar exports, manifests,
   renderer-backed previews, and deterministic stress recipes.
2. Use the new process helpers and manifests to tune river incision and mountain
   hierarchy in small passes.
3. Start the next process-helper batch with a clean-room gully/erosion
   diagnostic for the mountain stress recipe before opening lakes or shoreline
   work.
4. Use that diagnostic evidence to improve the mountain source hierarchy.
5. Return to river topology with graph/hydrology references after mountain
   process/source quality is clearer.
6. Only then add another terrain process or water body. Lakes, coast, dunes,
   snow, talus, and foliage eligibility should build on these product/process
   pieces instead of restarting from authored shapes.

## Foundation Boundary

Keep these helpers in `projects/terrain` for now. They are domain-specific
terrain process operations, not yet general procedural foundation APIs. Promote
them into `cubey::procedural` only after more than one consumer needs the same
contract and the names are no longer terrain-specific.

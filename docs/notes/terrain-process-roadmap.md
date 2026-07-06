# Terrain Process Roadmap

Date: 2026-06-30

This note captures the current terrain reboot reset point after the river and
mountain driver experiments. The project has useful product plumbing now, but
the next work should move away from per-image tuning and toward reusable terrain
process fields.

## Current Position

`projects/terrain_workbench_legacy` preserves this roadmap and the old reboot
workbench evidence. New visual terrain rendering work should happen in
`projects/terrain_ref` unless this note is intentionally revived for product
field work. The data-first direction remains:

```text
coherent source fields -> terrain process fields -> product fields -> consumers
```

The current implementation already emits named scalar fields, summaries,
headless PNG review sets, scalar capture manifests, and renderer-backed preview
captures. Revision 25 adds selectable preview surfaces so `height_m`,
`post_erosion_height_m`, and `pre_process_height_m` can be compared in the same
mesh consumer. Revision 26 adds `mountain_profile_height_m` so the mountain
stress recipe can be reviewed as one coherent height profile. Revision 27 adds
`mountain_saddle_gate`, curved ridge influence, and elongated summit support.
It also has
separate diagnostic recipes for the normal temperate mountain river slice, a
stress river network, and an isolated mountain range.

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
| Process helpers | Terrain-local spread, relief-clamped lowering, height-lowering, and diagnostic gully helpers now exist in `terrain_process_fields`. | Keep new process math routed through helper APIs before broadening erosion, deposition, talus, snow, sand, or wetness work. |
| River carving | Rivers now lower `height_m`, but channel depth still reads weak in 3D and water/material tint can obscure geometry. | Keep incision fields explicit, use manifests to compare field ranges, then tune against height-only and channel preview modes. |
| Mountain form | The stress recipe now uses a coherent profile height with curved ridge influence, elongated summit support, and saddle suppression, but perspective captures still read synthetic because crests/summits are source-shaped rather than process-eroded. | Treat mountains as a process hierarchy: broad mass, peak anchors, curved crest fields, saddle suppression, erosion/talus/strata cleanup, then local detail. |
| Scale | Rivers and mountains are still patch-local with a padded halo. | Later world/tile work should generate deterministic world-coordinate basin and range sources, then rasterize local products plus halo. |
| Capture evidence | Scalar review directories now write `manifest.json` beside the PNGs. | Use manifest ranges and hashes when comparing captures instead of relying only on manual image inspection. |

## ShaderToy Operator Extraction

The ShaderToy terrain/hydro references are useful, but only when routed through
Cubey's field pipeline. Treat them as compact process-operator and visual
vocabulary references, not as renderer or river-topology donors.

Near-term borrow targets:

- the revision 24 clean-room gully/erosion diagnostic over `height_m`,
  slope/derivatives, local relief, and mountain support, producing
  `erosion_delta_m`, `gully_mask`, `crease_proxy`, and
  `post_erosion_height_m`;
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
2. Use the new process helpers, manifests, and preview surface selector to tune
   river incision and mountain hierarchy in small passes.
3. Review the revision 27 coherent mountain profile before deciding whether the
   revision 24 clean-room gully/erosion diagnostic or a new erosion-aware crest
   cleanup should affect final height.
4. Use that evidence to improve ridge/summit process shaping without returning
   to pasted layers.
5. Return to river topology with graph/hydrology references after mountain
   process/source quality is clearer.
6. Only then add another terrain process or water body. Lakes, coast, dunes,
   snow, talus, and foliage eligibility should build on these product/process
   pieces instead of restarting from authored shapes.

## Foundation Boundary

Keep these helpers in `projects/terrain_workbench_legacy` for now. They are domain-specific
terrain process operations, not yet general procedural foundation APIs. Promote
them into `cubey::procedural` only after more than one consumer needs the same
contract and the names are no longer terrain-specific.

# CSG junction and presentation polish

Date: 2026-11-04. Status: complete locally; native art candidates.

The [matched visual report](http://starship:8787/reports/b7e544bd392b94abae79cdc829049050ab6b39fc59bef887d7df2c48926f88b5/)
follows the [forms-first checkpoint](sdf-csg-form-refinement.md) at `2f87ec88`.
The user accepted that direction and asked for a wider scan of joins and
presentation, using the observatory landing, mill waterfront and falling
courtyard water as examples. This pass inspects all four, including reverse,
overhead and close views; it keeps the simpler forms and fixes relationships.

## Changes

- Observatory: the wider supported landing overlaps the circular balcony at
  exactly its height. The balcony rail has a matching opening and an end post;
  it no longer blocks the approach from the stair. The section carries the
  same repair inside its architectural assembly.
- Mill: both banks share a consistent coping height, with gaps at the bridge
  entries. Thinner deck planks meet their two stringers. Two retained grain
  sacks stand in the dry yard; the third is removed. Awning posts stand on the
  coping. A small wheel race connects to the canal and the lowered paddles
  reach its water. Cart wheels now seat on their paving with a small parking
  rest; barrels are moved clear of the delivery path.
- Courtyard: remove all four falling-water strands and retain the still basin.
  Expand the plain paving to a coherent plaza and seat the bench/pots on it.
  Open the upper doorway through the overlapping wing and tower, with interior
  clearance. Match the landing to the upper floor, make stair rises uniform,
  connect balusters to rails and seat the fountain finial on its bowl.
- Robot: inspected from all four general angles, with its independent joint
  audit repeated. No geometry change was justified; it is byte-identical to
  the previous refinement.

The original mill sacks were read as buckets because of their simple tapered
forms and ties. Their placement was the defect addressed here; no new tiny
decorative detail was added to distinguish them. The new bridge supports and
wheel race explain the mechanism. Water remains static CSG geometry.

The new leaf counts are robot 75, mill 179, observatory 277, courtyard 180 and
observatory section 278. More leaves are justified where they repair support
or a shared cut; fewer leaves are not an independent acceptance target. No
performance improvement is claimed.

## Reproduction

The current candidates and presets remain in
`projects/sdf/scenes/csg-showcase-2026-11/` and
`projects/sdf/presets/csg-showcase-2026-11/`. Presets are unchanged, explicitly
choosing Balanced/SS2 for presentation; the application default remains Cached.

```sh
build/release/projects/sdf/sdf --config projects/sdf/presets/csg-showcase-2026-11/observatory.json
```

The [polish generator](../../projects/sdf/tools/polish_csg_showcase.py) derives
the previous candidate with the retained forms-first generator, verifies it
against the frozen `before/` tree, then applies the repairs. The earlier source
JSONs, evidence and immutable report are preserved. Restore the report's
bundled `before/` tree to the following study path when reproducing elsewhere.

```sh
python3 -B projects/sdf/tools/polish_csg_showcase.py --before-dir outputs/sdf/csg-junction-polish-20261104/before --manifest outputs/sdf/my-fresh-polish/changes.json --capture-dir outputs/sdf/my-fresh-polish/captures --extra-views
python3 -B projects/sdf/tools/check_csg_joins.py --output outputs/sdf/my-fresh-polish/joins.json
python3 -B projects/sdf/tools/qualify_csg_refinement.py --output outputs/sdf/my-fresh-polish/qualification
```

Use fresh output directories. The report includes frozen before/after JSONs,
all forty native frames and capture inputs, native logs, history qualification,
the bounded join audit and exact authoring/report sources.

## Validation and limits

Forty validation-enabled native 1440 by 1080 frames compare matched before/after
views: four angles of the robot, five angles of each architectural scene and
the observatory section. The native executable and all 220 compiled shaders
retain their prior fingerprints. No renderer, shader, camera or lighting
change is used to disguise a geometric join.

The independent [saved-geometry audit](../../projects/sdf/tools/check_csg_joins.py)
passes 170 checks. It composes nested transforms from JSON and evaluates
selected serialized boolean masses. It checks surface continuity, access
clearance, support contacts, bank/deck heights, wheel immersion, dry prop
placement, stair rises and the tower passage through both walls. It catches
eleven access failures in the previous observatory. The robot's 66 joint checks
and the older observatory's 63 base/tripod checks per variant also pass.
These are bounded assertions, not a general collision or walkability solver.

The existing native field/history oracles pass all 169 prefix/seek states for
the final five JSONs, including empty reset, backward seek and final restore.
Every surviving leaf remains named and covered once by the saved history;
new positive/negative leaves keep operation polarity. The section shares all
surviving primitives with the intact observatory and adds exactly its cutter.

The new audit first exposed insufficient depth behind the courtyard doorway;
adding the shared interior cut resolved it. Intermediate source-validation
also caught an incorrectly supplied extra subtraction tool before any native
capture accepted it. The final cut uses the required binary subtraction with
a union of tools. Intermediate artifacts remain outside the final packet.

Report JavaScript passes 68 control/asset assertions. The connected browser
adapter still fails with `Cannot read properties of undefined (reading
'timeoutMs')`; browser layout and physical interaction acceptance remain
unverified. HTTP byte verification and native visual inspection are distinct.
Scoped CTest, SDF CLI recipes, deterministic generation and preservation are
recorded in the [evidence packet](../evidence/sdf-csg-junction-polish-2026-11/README.md).

Raw iterations are under `outputs/sdf/csg-junction-polish-20261104/`. The final
native evidence, frozen inputs and delivered report are protected by its
retention manifest. No cleanup or deduplication was performed. Guardian stays
retired; the separate horse work remains untouched. Public media, push and
deployment are unchanged.

Follow-up: the observatory base trim now overlaps through all four corners in
both the intact and section scenes. The [small native before/after report](http://starship:8787/reports/2df2e6008127e27d18d33e428f3778ff0076e95a8328b5baf7b6e8e2e732d20e/)
and [verification receipt](../evidence/sdf-observatory-trim-2026-11.json) retain
the three new captures. Histories, materials and all other primitives are unchanged.

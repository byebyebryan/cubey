# Terrain Directional Backdrop Study

Date: 2026-07-16

Status: directional composition rejected; the later radial v2 follow-up is
accepted as the macro baseline in
[`terrain-radial-backdrop-macro-baseline.md`](terrain-radial-backdrop-macro-baseline.md).

## Decision

The cached terrain product is a fixed-focus backdrop, not a general traversable
world. Its composition does not need mountains around all 360 degrees. Replace
the circular inner cut and inverted radial-valley idea with a directional
study: place the scene on quiet low terrain beside one or more distant mountain
arcs, while allowing other directions to remain open.

Compare two bounded approaches. The placement-only lane leaves the procedural
source unchanged and searches translated windows for a suitable low-side
focus. The shaped lane uses the same focus and mountain direction, then applies
a broad, warped, one-sided relief transition. The shaped result is useful only
if placement alone cannot provide a robust composition without exposing a
straight uplift strip or authored boundary.

This study does not promote `mountains-hierarchy-v2`, add source v4, or alter
the accepted production backdrop defaults.

## Directional Placement Contract

Evaluate 24 panorama sectors around each candidate focus. Local terrain must
remain quiet around the subject, while distant relief is evaluated at roughly
2, 6, 9, and 12 km. A useful placement has:

- low local relief and slope around the focus;
- at least one contiguous distant mountain arc;
- at least one contiguous open or low horizon arc;
- roughly 4-14 mountain sectors rather than the current 14-sector minimum;
- a gradual rise in mountain-facing sectors rather than an isolated far wall.

The mountain direction comes from the strongest contiguous prominence arc. It
is measured from the source and must not be a fixed authored heading.

## Directional Shaping Contract

The optional shaping lane uses signed distance along the selected mountain
direction, not distance from the focus. Its clean-room composition is:

- a quiet floor from the wrapped source sampled with a 6 km footprint and 8
  percent retained relief;
- intermediate structure from a 1.5 km footprint;
- broad relief restored from 2.5-8 km toward the mountain direction;
- full detail restored from 5-10 km in that direction;
- a center-anchored, two-octave procedural warp with a 14 km period and 1.25 km
  amplitude.

Terrain behind and beside the focus remains low. The warp prevents the rise
front from becoming a straight or diagonal line. No radial attenuation,
circular basin, ridge path, or hand-authored terrain feature is allowed.

## Continuous Center

The study renders terrain continuously beneath the focus. Add an opt-in center
mesh that joins the existing polar sectors with exact shared samples. The
validation sphere rests on the conditioned or naturally selected floor. The
existing cutout topology and hashes remain the production default.

## Fixed Comparison

For `mountains-hierarchy-v2` seeds `0`, `9012`, and `12345`, compare:

1. the current 6 km hard-cut control;
2. continuous geometry at the current focus;
3. continuous geometry with directional placement only;
4. directional placement plus one-sided shaping.

Add placement-only and shaped v2.1 lanes for seed `9012` as a detail control.
Capture six unrestricted yaw angles plus the 50 m / 0 degree, 100 m / 8 degree,
and 250 m / 30 degree orbit envelope. Review top height, slope, placement arcs,
shaping gates, clay, and final presentation before performance.

## Acceptance And Stop Condition

Accept a lane only when it has no circular hole, concentric basin, straight
uplift boundary, center/sector seam, or subject intersection. Mountains may
occupy only part of the horizon, but they must build gradually and remain
credible through every view where they are visible. Placement-only wins when
both lanes pass. Shaping is retained only when it materially improves seed
robustness without exposing its directional gate.

The terrain-surface pass must remain below 1 ms p95 at 2560 x 1440 over at least
120 post-warmup samples. The batch ends with a documented verdict. Translation,
streaming, close terrain, vegetation, water, hydrology, and production
promotion remain separate work.

## Completed Review

The fixed pack is under
`outputs/terrain/directional-backdrop-study-v1/`. It contains all four hierarchy
lanes for seeds `0`, `9012`, and `12345`, six unrestricted yaw samples per lane,
matching surface and clay matrices, the v2.1 control, orbit-envelope checks,
source/gate diagnostics, presentation captures, and GPU profiles.

The continuous-center topology passes its mechanical purpose. No center hole,
sector seam, or detached ownership edge is visible, and the validation sphere
rests on the sampled floor. That result is retained as an opt-in study path;
the production cutout remains unchanged.

Placement alone fails. The bounded hierarchy search selects `23`, `24`, and
`23` mountain sectors for the three seeds, with no open sectors and a failed
placement contract in every case. Moving the focus within the unchanged source
therefore does not produce the low-side composition this study needs.

One-sided shaping meets the numeric panorama contract with `9-10` mountain
sectors and `11-14` open sectors, but fails visual review. Its broad and detail
gates remain legible as a directional band in top diagnostics. In clay, several
mountain-facing headings become a near-frame-filling wall while other headings
lose useful mountain silhouette entirely. The same behavior appears in the
v2.1 control, so this is a composition problem rather than a hierarchy-v2-only
artifact. The `250 m / 30 degree` envelope also reads as terrain-filled rather
than as a reusable backdrop view.

At the study's explicit full render stride, the 2560 x 1440 terrain pass misses
the performance gate: the hard-cut control measures `1.540 ms` p95 and shaped
continuous measures `5.496 ms` p95 over `146` post-warmup samples each. This is
not a regression in production defaults, which retain their coarser render
stride and cutout topology, but it prevents promoting the study configuration.

## Verdict

Reject both directional lanes for production. Placement-only cannot find the
required composition in the tested source, and the shaping fallback replaces a
circular artifact with an exposed directional one while adding substantial
geometry cost. Retain the planner, relief wrapper, continuous topology, report,
and capture app as isolated evidence and reusable diagnostics. Do not add
another shaping iteration in this batch; the accepted cached hard-cut backdrop
remains the runtime default.

## Expanded Follow-up

After reviewing the surface pack interactively, add one explicitly isolated
`expanded-shaped` lane. It tests whether the rejected result was dominated by
viewpoint and transition scale rather than by directional composition itself:

- expose a `100-1000 m` focus altitude and default to `500 m` above the local
  filtered floor;
- expand the debug orbit from `100 m` to `1 km`, defaulting to `400 m`;
- extend the study-only terrain radius from `16.384 km` to `32.768 km` without
  changing mesh topology or production extent;
- restore broad structure over `6-18 km` and source detail over `10-26 km`;
- scale the anchored domain warp to a `28 km` period and `2.5 km` amplitude.

This follow-up does not overturn the completed verdict by implementation alone.
It requires a new expanded-domain source/gate report and surface-first visual
pack before either the composition or its cost can be judged.

### Expanded Review

The complete follow-up pack is under
`outputs/terrain/directional-backdrop-expanded-v1/`. It includes `1024 x 1024`
base, floor, shaped-height, slope, placement, and gate fields over a
`65.536 km` square for all three hierarchy seeds and the v2.1 control. Surface
orbits isolate seed, `100/500/1000 m` focus height, and `100/400/1000 m` orbit
radius, with clay retained only as a silhouette check.

The expanded scale materially improves scene composition. Mountains remain in
the far field, the foreground no longer reads as the bottom of a tight valley,
and the full orbit-radius range remains continuous without a center or sector
seam. Changing focus altitude within `100-1000 m` mainly changes clearance and
atmosphere; it has much less visual effect than orbit distance and terrain
transition scale. A `500 m` default remains a reasonable midpoint for this
study.

The source maps prevent interpreting that improvement as a solved terrain
model. Broad and detail gates are non-circular but remain clearly visible as a
warped directional band. Shaped slope concentrates at that band, and several
surface headings still expose a smooth uplift shelf before source mountains
resume. Other headings are intentionally open but can become nearly empty.
The broader transition hides the artifact better; it does not remove the
authored composition boundary.

At 2560 x 1440, the full-stride terrain pass measures `4.451 ms` p95 over `146`
post-warmup samples and fails the `<1 ms` production gate. Retain
`expanded-shaped` as a useful study/debug composition and do not promote it to
the cached backdrop default.

## Expanded Radial Comparison

The directional result leaves several headings empty and exposes its one-sided
uplift shelf. A companion `expanded-radial` lane tests the opposite composition
without changing the mountain source, renderer, terrain extent, or camera:

- retain the source-derived `8 km` filtered floor and `2.5 km` structure level;
- restore broad structure from `6-24 km` radial distance;
- restore source detail from `12-29 km` radial distance;
- keep the `32.768 km` outer radius, `500 m` focus height, `400 m` default
  orbit, and unrestricted yaw;
- use an unwarped radial distance so this remains a controlled circular test.

The comparison pack is under
`outputs/terrain/radial-backdrop-expanded-v1/`. It contains `1024 x 1024`
source, floor, shaped-height, slope, and gate fields for seeds `0`, `9012`, and
`12345`; six-heading surface rows for all three seeds; a matched directional
control; and a clay silhouette row.

The wide band removes the visible hard cutoff in scene views. It also restores
mountain occupancy in every tested heading, avoiding the directional lane's
empty sectors and exposed one-sided shelf. Variation still comes from the
wrapped procedural source, so the horizon does not become a geometrically
uniform wall.

The diagnostic maps make the compromise explicit. Both gates remain perfect
circles, shaped slope contains a circular low-relief region, and the central
foreground is visibly quieter and flatter than the restored source. The
surface orbit hides that transition at this scale, but the composition now
implies mountains in every direction. Several seed/headings also crop very tall
source peaks, which is a source/framing issue rather than a gate discontinuity.

Keep `expanded-radial` as the stronger unrestricted-yaw comparison lane, not as
a production source contract. It demonstrates that a large transition band is
viable for the backdrop framing problem, while confirming that circular
composition and terrain-source quality remain separate decisions.

### Narrow-Core Follow-up

The initial radial parameters leave a `6 km` quiet radius, which is much larger
than the `1 km` maximum camera orbit. Preserve that evidence under
`outputs/terrain/radial-backdrop-expanded-v1/`, then retune the same isolated
lane without changing its source or transition function:

- reduce the filtered floor footprint from `8 km` to `6 km`;
- start broad restoration at `1 km` and complete it at `24 km`;
- restore detail over `5-30 km`;
- retain smootherstep, exact radial distance, and the existing camera envelope.

The resulting pack is under
`outputs/terrain/radial-backdrop-expanded-v2/`. The scene-view difference is
appropriately subtle: low-frequency terrain begins returning earlier, but the
23 km smootherstep band keeps relief near the stage weak. All tested headings
remain continuous and clear, with slightly fuller terrain before the far
horizon and no visible circular shelf.

Top diagnostics show the intended larger change. The fully quiet core is much
smaller, gate gradients span more of the domain, and shaped slope transitions
more gradually into the source. The center remains deliberately calm and the
radial signature remains measurable. The implementation therefore stays
study-only, while v2 is accepted as the macro-composition target. Prefer v2
over v1 for subsequent radial backdrop evaluation.

The radial result closes this composition study. Subsequent work follows the
cached-integration and detail boundaries in
[`terrain-radial-backdrop-macro-baseline.md`](terrain-radial-backdrop-macro-baseline.md)
rather than adding another transition variant here.

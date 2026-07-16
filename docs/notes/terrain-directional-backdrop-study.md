# Terrain Directional Backdrop Study

Date: 2026-07-16

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

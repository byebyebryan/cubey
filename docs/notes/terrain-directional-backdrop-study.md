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

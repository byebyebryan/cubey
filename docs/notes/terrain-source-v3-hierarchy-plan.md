# Terrain Source V3 Far-Field Hierarchy

Date: 2026-07-13

Status: implementation plan. Source v1 remains the default, source v2 remains
the previous mountain-quality control, and source v3 is an opt-in mountain-only
candidate.

## Problem

Source v2 improves geometric bandwidth without changing the mountain model. It
extends the existing detail band from four to eight octaves, raises its gain and
ridge contribution, and then sends the result through the same nonlinear
elevation power as the broad source. Review shows that this preserves peak
placement but adds narrow ribs, point peaks, and similarly sized corrugation
across high terrain.

The next source must establish scale hierarchy before adding more bandwidth.
The product target is a convincing backdrop at 3.2-6.4 km. The 1.6 km
midground and closer views remain diagnostics for finding source defects; they
do not establish a walkable-surface requirement.

## Source Contract

Source v3 remains deterministic, random-access, world-coordinate terrain with
matching CPU and GLSL evaluation. It has no authored anchors, paths, masks,
finite patch composition, hydrology, or erosion.

The clean base height is the sum of named contributions:

```text
base height = massif height
            + valley delta
            + ridge delta
            + meso delta
```

- `range_support` comes from a three-octave 24 km signed field after a broad
  two-component 32 km domain warp capped at 2 km.
- `massif_height_m` comes from a four-octave 12 km signed field. Nonlinear
  elevation shaping applies only to this broad profile.
- `valley_delta_m` comes from the negative lobe of the same massif field. It is
  not an independent channel or line and is capped at 35% of massif height and
  900 m.
- `ridge_delta_m` applies one broadened ridge transform after accumulating a
  three-octave 6 km field. It is gated by range and highland support and capped
  at 18% of massif height and 600 m. Ridge extraction is never applied to each
  octave independently.
- `meso_delta_m` is signed four-octave detail from 1.2 km through 150 m. It is
  suppressed near summit silhouettes and capped at 4% of massif height and
  160 m.

Each octave uses the existing footprint filter. Unresolved bands approach a
stable neutral value rather than changing macro height. Geometry stops at
approximately 150 m; generated material and normal paths own finer scales.

## Diagnostics

Source v3 exposes range, massif, valley, ridge, and meso views. The source report
records range coverage and component RMS/max statistics in addition to final
height and slope. Existing v1/v2 reports, debug behavior, and content hashes
remain unchanged.

The fixed review set contains:

- v2/v3 far-field surface and clay views for seeds 0, 9012, and 12345;
- clean v3 component views for all three seeds;
- world-center top and oblique source-shape comparisons;
- v3 midground clay diagnostics for seeds 9012 and 12345;
- source reports, camera plans, timing, and measured acceptance metadata.

## Acceptance

Mechanical guardrails for each review seed are:

- final relief between 1.8 km and 4.5 km;
- mean slope below 0.60;
- range-support coverage between 15% and 85%;
- ridge RMS below 18% of massif RMS;
- meso RMS below 5% of massif RMS;
- CPU/GLSL parity at footprints from 0 through 512 m;
- the quality/layered 960 x 540 profile below 33.3 ms per frame.

Visual acceptance remains authoritative. Far-field clay must show broad
connected mass, readable valleys, gradual buildup, varied but non-pointy
summits, and no repeated thin fins or corrugated silhouettes. If ridge contours
remain visible, reduce or broaden the one ridge control; do not add ridged
octaves. Local weathering is disabled for source-shape diagnostics and enabled
only for final presentation views.

## Exclusions

This batch does not change the default source, quality renderer, layered
materials, camera contracts, hydrology, erosion, water, foliage, upland, or
plains. Promotion of v3 is a later decision based on the completed review pack.

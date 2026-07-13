# Terrain Source V3 Far-Field Hierarchy

Date: 2026-07-13

Status: implemented and measured opt-in checkpoint. Source v1 remains the
default, source v2 remains the previous mountain-quality control, and source v3
is an opt-in mountain-only candidate.

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
- `massif_height_m` comes from a six-octave 8 km signed field. Nonlinear
  elevation shaping applies only to this broad profile.
- `valley_delta_m` comes from the negative lobe of the same massif field. It is
  not an independent channel or line and is capped at 65% of local massif
  height and 600 m.
- `ridge_delta_m` applies one broadened ridge transform after accumulating a
  five-octave 6 km field. It is gated by range and highland support and capped
  at 14% of massif height and 450 m. Ridge extraction is never applied to each
  octave independently.
- `meso_delta_m` is signed four-octave detail from 1.2 km through 150 m. It is
  suppressed near summit silhouettes and capped at 5% of massif height and
  140 m.

Each octave rotates away from the preceding gradient-noise lattice and uses the
existing footprint filter. Unresolved bands approach a stable neutral value
rather than changing macro height. Geometry stops at approximately 150 m;
generated material and normal paths own finer scales.

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
- valley RMS between 1% and 10% of massif RMS;
- ridge RMS below 18% of massif RMS;
- meso RMS below 5% of massif RMS;
- CPU/GLSL parity at footprints from 0 through 512 m;
- the quality/layered 960 x 540 profile below 33.3 ms per frame.

Visual acceptance remains authoritative. Far-field clay must show broad
connected mass, readable valleys, gradual buildup, varied but non-pointy
summits, and no repeated thin fins or corrugated silhouettes. If ridge contours
remain visible, reduce or broaden the one ridge control; do not add ridged
octaves.

## Runtime Boundary

Source v3 is materially more expensive than v1/v2 when repeatedly evaluated by
the direct-source renderer. Its quality path therefore uses three exponentially
spaced terrain-shadow probes and skips the separate five-sample landform
concavity estimate. V1 and v2 retain their exact 16-probe and concavity paths.
Layered fragment-normal recovery is skipped whenever its existing distance fade
is zero for every source version.

V3 presentation and timing captures use weathering off. The source's meso band
provides bounded local form; applying the existing local weathering kernel adds
eight complete neighbor evaluations and exceeds the frame budget. A cached or
otherwise amortized weathering process remains later work rather than part of
this direct random-access candidate.

V3 backdrop planning evaluates headings with the camera's actual horizontal
forward convention and permits up to 18 degrees of upward pitch when needed to
keep the selected massif in frame. V1/v2 keep the legacy heading convention and
12-degree pitch limit so their established captures and contracts remain
unchanged.

## Exclusions

This batch does not change the default source, v1/v2 rendering, layered
materials, legacy camera contracts, hydrology, erosion, water, foliage, upland,
or plains. Promotion of v3 is a later decision based on the completed review
pack.

## Measured Checkpoint

Generate the authoritative pack with:

```sh
projects/terrain/capture_source_v3_review.sh
```

It replaces `outputs/terrain/source-v3-hierarchy/` with matched v2/v3
far-field views, v3 component and source-shape diagnostics, midground stress
views, reports, and a timing profile. The accepted run measured:

| Seed | Relief | Mean slope | Valley / massif RMS | Ridge / massif RMS | Meso / massif RMS |
| ---: | ---: | ---: | ---: | ---: | ---: |
| `0` | `3384.62 m` | `0.3813` | `0.0334` | `0.0726` | `0.0078` |
| `9012` | `3006.55 m` | `0.2880` | `0.0515` | `0.0714` | `0.0078` |
| `12345` | `3690.35 m` | `0.4877` | `0.0295` | `0.0791` | `0.0084` |

The six v3 camera plans remain within the 2-of-15 near-frame occupancy limit.
The 960 x 540 quality/layered profile measured `31.3749 ms`, below the
`33.3 ms` gate. V1/v2 report hashes remain
`5687ba3d63ec477a813cd0fefd5b268affc128f84bfce01224d049fff34e9edb`
and `c9b1f9b94d7f2d14f8f301df59c29651207c279b43f31339815e552421b2b456`.

The far-field read is directionally successful: v3 replaces v2's uniformly
corrugated field and thin-fin emphasis with connected mountain bodies and
broader buildup. It is not a promotion candidate yet. Midground diagnostics
still expose overly rounded slopes, broad parallel shoulder bands, and limited
localized sharp form. Those are source-model findings for a later pass; they
must not be hidden with closer camera placement or indiscriminate detail noise.

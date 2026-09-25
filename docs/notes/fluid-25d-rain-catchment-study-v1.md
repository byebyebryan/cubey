# Fluid 2.5D rain catchment study V1 (2026-09-24)

Status: bounded diagnostic study; **no terrain fixture, rain product, or
single-outlet scene promotion**. The inherited worktree was dirty before this
pass. This study does not commit or alter the immutable terrain source or the
default water presentation.

## Question and evidence boundary

Can uniform rainfall excess on an unmodified Terrain Diffusion map produce a
legible dynamic story of runoff collecting into channels and leaving a natural
outlet? Keep terrain routing analysis, renderer legibility, and measured water
evolution separate. A D8 catchment is a site-screening hypothesis, not a water
simulation or proof of a river. Every RainPulse crop presently rains on the
whole rectangle and opens all outward perimeter faces; it has no infiltration,
evaporation, upstream boundary inflow, or terrain-carved channel.

The previous fixed rain probe used 12 and 60 mm/h for 600 s, then 600 s of
drainage, on a 256×128 native-30 m lowland crop. The 60 mm/h tier was a
visibility stress test, not a calibrated watershed storm. It produced coherent
top-down runoff but no clear one-outlet event. This pass kept that protocol
fixed while checking a better catchment and a render-only thin-water view.

## Gate 1: immutable-terrain site screen

Pinned `rolling-lowland-1/heightfield.json` elevation payload SHA-256:
`fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2`.
The selected conditional crop is `(x304,z837,256×128)` at 30 m/cell, or
7.68×3.84 km; its transformed crop SHA-256 is
`7d7ace75441f3e931c3631f72570ecb655012d3a9b1c1f46853dec6bf88b4e1d`.
The candidate north-edge outlet is world cell `(546,837)`, local `(242,0)`.

In the pinned, connected D8 terrain read, that outlet has a 16.5222 km²
full-map upstream mask. The crop contains 15.2127 km² (92.1%) of the mask;
1.3095 km² remains outside and enters through the south and east edges. The
full mask spans 194×166 samples, so no 256×128 crop can contain all of it. A
512×256 crop could contain the mask, but it also includes a competing north
outlet with 51.543 km² of crop-local accumulation. The proposed outlet is the
lowest raw-elevation cell on this crop perimeter (75.576 m), with a raw
downhill step beyond it and zero local fill. That makes it a plausible
headwater pilot, **not** a self-contained single-outlet watershed. There are
many other perimeter exits and the simulator opens every outward face.

The earlier broad/narrow main-route crops were worse for this purpose: their
crop-internal D8 accumulation captured only 6.31% and 2.35% of their
respective full upstream areas. Those figures use strict in-crop routing and
are not directly interchangeable with the 92.1% mask-intersection statistic.

## Gate 2: isolate presentation from physics

`--fluid25d-terrain-thin-water-composite` is opt-in, terrain-case Composite
only. It tapers the water overlay from invisible below 2 mm to the previous
opacity at 5 cm; it does not change solver state, the WaterDepth diagnostic,
or the default presentation. The frozen A/B runner uses the earlier
`(1216,1664,256×128)` rain crop at exact frames 300 and 600. On both frames,
baseline and thin-water **entire profile metrics files are SHA-identical**,
including solver status and conservation. Visual review shows that the default
thin sheet mutes the terrain, while the opt-in view retains brown relief and
reveals the deeper blue drainage network. This is a legibility result, not a
site or hydrology promotion.

Reproduce with:

```sh
uv run projects/fluid/fluid_25d/terrain_rain_thin_water_ab_v1.py \
  --output-dir outputs/fluid/terrain-rain-thin-water-ab-v1-<new-run>
```

Verified result: `outputs/fluid/terrain-rain-thin-water-ab-v1-20260924-post-boundary/result.json`.
The corresponding `review-contact-sheet.png` is the visual A/B.

## Gate 3: fixed rain pilot and actual boundary ledger

The candidate crop started dry. It received uniform 60 mm/h rainfall excess
for 600 s and then 600 s without rain, using the opt-in finite-volume solver,
2 s fixed outer steps, eight substeps, open outward perimeter faces, and the
thin-water Composite. No forcing, terrain, boundary, or renderer tuning was
done after seeing the site. Exact f300/f600 Composite and WaterDepth captures
are in `outputs/fluid/terrain-rain-catchment-pilot-v1-20260924-verified/` alongside
commands, profile CSVs, checksums, a contact sheet, and `result.json`.

The existing cumulative per-cell GPU ledger already holds boundary outflow.
This pass adds a **read-only** rain-profile attribution into 16 equal bins on
each non-corner edge; corners remain separate because one corner cell can have
two open faces. The sum of bins plus corners matches the existing all-boundary
ledger within CSV rounding. The intended outlet is north bin 15, local
x=240..254: a 450 m non-corner edge segment within the nominal 480 m bin,
containing `(242,0)`. The segment is much
wider than the outlet cell, so its share is an upper bound on a narrower
outlet-specific reading, not an exact point discharge.

| Exact time | Rain source | Max depth | Active-flow cells | All-edge outflow | Intended north bin | Bin share |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 600 s | 294,907 m³ | 0.143 m | 2,722 / 32,768 | 5,967 m³ | 123 m³ | 2.05% |
| 1,200 s | 294,907 m³ | 0.733 m | 2,924 / 32,768 | 18,084 m³ | 482 m³ | 2.67% |

At 1,200 s the finite-volume status flags are zero and the conservation
residual is 8.269 m³, 0.0028% of cumulative source. The non-edge outflow
ledger is zero. The four edge totals are north 9,268 m³, south 3,637 m³,
west 2,739 m³, east 2,315 m³, with 125 m³ attributed to corner cells. Water
does accumulate after rain ends and the top-down diagnostic shows a branching
network. The oblique Composite remains hard to read as one event, and the
measured boundary loss is distributed, not concentrated at the proposed
outlet. Even the 480 m segment collects only 2.67% of boundary outflow.

Reproduce with:

```sh
uv run projects/fluid/fluid_25d/terrain_rain_catchment_pilot_v1.py \
  --output-dir outputs/fluid/terrain-rain-catchment-pilot-v1-<new-run>
```

The runner refuses to overwrite an existing evidence directory, verifies the
terrain payload/crop identity, checks status and conservation, compares exact
Composite/WaterDepth solver metrics, and closes the boundary attribution.

## Verdict and next decision

The rain model is useful now for **distributed hillslope runoff and channel
convergence**, especially in WaterDepth. It does **not** yet support the
stronger story "rain gathers into one natural river outlet" on this crop and
20-minute window. The site-screening catchment is only partly contained, the
rectangle receives rain outside that basin, all sides drain, and the measured
outlet share is small. The lowland map spans kilometres while the flow visible
over minutes is local; a longer run or a smaller coherent basin needs a
separate fixed protocol, not ad hoc tuning of this result. A video would show
local network growth but should not be captioned as a single-outlet river.

For the next pass, choose the intended story *before* changing mechanics:

1. For a truthful distributed-runoff demo, retain all-edge rainfall, label
   it as local convergence, add a stable top-down/oblique time comparison, and
   use the edge-ledger distribution as part of the explanation.
2. For a one-outlet watershed demo, screen for an outlet with a substantially
   contained *rainfall domain*, choose a physically appropriate duration,
   then predeclare an outlet-share gate. A basin-shaped rain mask or different
   boundary handling would be a new hydrology protocol and must be compared
   explicitly with this unchanged rectangular-rain control.
3. For pond/lake formation, screen a depression and measure retained volume,
   water-surface rise, and spill threshold rather than requiring a river exit.

Do not add infiltration, dye, particles, rain sprites, or default changes
until the chosen physical story passes its own measurement and visual gates.

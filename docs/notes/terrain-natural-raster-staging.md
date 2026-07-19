# Terrain Natural Raster Staging

Date: 2026-07-19

Status: complete and promoted. Bounded directional staging with a continuous
center is the basis of the opt-in `raster-v1` product. The `500 m` focus remains
the default and Terrain Diffusion remains only an offline producer. The
original consumer-owned-center verdict is retained below as decision history,
but it is superseded for this isolated backdrop: a visible circular cutout is
not an acceptable final composition.

## Problem

The pinned `2048 x 2048` Terrain Diffusion fields are substantially more
coherent than the internal controls in top height and slope diagnostics, but
the common backdrop renderer reduces them to low rolling horizons. The source
is not the immediate failure.

The external study currently uses the detached panoramic stage planner. That
planner raises the target until the lower frame remains terrain-free to
`3.2 km` at every tested yaw, radius, and elevation, then asks for at least 14
of 24 sectors to contain relief between `3.2-6.6 km`. On the three maintained
raster fields it selected targets about `2.75-3.25 km` above the local terrain;
only seed `9012` passed the radial contract. This height explains why strong
source morphology collapses toward the horizon.

The existing directional placement evaluator gives a better fit for natural
terrain without changing source height. A bounded probe over the same fields
found passing placements for all three seeds:

| Seed | Focus x/z (m) | Local relief (m) | p95 slope | Mountain/open sectors | Largest mountain/open arc |
| --- | --- | ---: | ---: | --- | --- |
| 0 | `8500 / -2500` | 29.75 | 0.081 | `4 / 13` | `4 / 10` |
| 9012 | `4500 / -8000` | 78.16 | 0.178 | `8 / 16` | `8 / 16` |
| 12345 | `7750 / -7000` | 42.38 | 0.049 | `8 / 8` | `8 / 4` |

These values were orientation evidence rather than hand-picked coordinates.
The implemented planner reproduced them deterministically.

## Candidate Contract

Keep the generated source and renderer fixed. Search a `24 km` square at
`4 km` coarse spacing, followed by the existing `1 km` and `250 m` refinement
passes. Retain the established directional placement rules:

- quiet local terrain within `1 km`, at most `120 m` relief and `0.25` p95
  slope;
- terrain samples at `2`, `6`, `9`, and `12 km`;
- 24 panorama sectors with 4-14 mountain sectors;
- at least three contiguous mountain sectors and four contiguous open sectors;
- gradual rise in at least half of the mountain sectors.

Map the selected natural focus to the consumer origin without reshaping the
field. Put the foreground focus `500 m` above the selected center height. Keep
the existing hard-cut comparison orbit at `50/100/250 m`, `0/8/30` degrees,
and unrestricted yaw. The focused-stage clearance correction may raise the
target only enough to retain `10 m` camera clearance; kilometre-scale raising
is a failed result.

The visible terrain remains `3.2-16.384 km`. Every selected focus must retain
the complete `16.384 km` disk plus one native `30 m` gradient sample inside the
finite raster.

## Center A/B

Compare two candidate ownership modes against the existing strict cutout:

1. `consumer-owned` keeps the current `3.2 km` far-field cutout;
2. `continuous` fills the center from the unchanged raster field.

The `500 m` focus keeps the continuous floor below the foreground scene while
testing whether removal of the annular edge improves composition. Prefer the
continuous lane only when it does not expose close-range source resolution,
intersect the foreground subject, or introduce a visible center seam. No
height mask, radial attenuation, flattened basin, or directional uplift is
allowed.

## Resolution And Coverage Boundary

The source stays at its native `30 m` spacing. At `2048 x 2048`, one field
covers about `61.44 km` per side and is sufficient for one bounded
`16.384 km`-radius backdrop stage. This batch does not claim that `30 m`
supports close terrain.

A `4096 x 4096` field at the same spacing would buy coverage rather than
detail. A real `15 m` source or a separately justified detail layer is needed
before a higher-resolution bake can answer fidelity questions. Do not change
resolution until the staging comparison establishes that the natural source
can survive presentation.

## Continuous Refinement Contract

The continuous lane exposed a topology-allocation problem rather than a reason
to restore the cutout. At high density, its center currently spends 32 radial
intervals between `0-300 m`, then only 64 logarithmic intervals between
`300-3200 m`. Spacing therefore grows from about `9.4 m` under the subject to
about `116 m` in the visible foreground, even though the raster source has a
uniform `30 m` sample spacing. The broad foreground shoulders in seed `12345`
are consistent with that mismatch.

Add a study-only uniform-center policy. It distributes the same 96 center
intervals over `0-3200 m`, giving about `33.3 m` radial spacing, then joins the
unchanged logarithmic outer annulus. This must not increase source samples,
vertices, indices, render triangles, or the number of draw meshes. Existing
procedural terrain products retain the split linear/logarithmic policy unless
they explicitly request the candidate.

Review three continuous lanes over the same source fields and unrestricted
yaw:

1. current split linear/logarithmic sampling at a `500 m` focus;
2. uniform center sampling at a `500 m` focus;
3. uniform center sampling at a `750 m` focus.

The first two isolate geometry allocation. The third tests whether additional
vertical separation makes the unchanged foreground less dominant without
collapsing the mountain backdrop toward the horizon. `500 m` remains the
default in this batch; the camera comparison is evidence only.

No source filtering, resampling, masks, radial attenuation, flattened basin,
material tuning, or higher framebuffer resolution belongs in this pass. If
uniform radial sampling still exposes polar structure, the next experiment is
a local Cartesian inner patch, not another heightfield edit.

## Evidence And Stop Condition

Capture strict cutout, directional cutout, and directional continuous lanes
through identical geometry, material, lighting, and camera settings. Review
source-space placement first, clay silhouettes second, and surface shading
last.

Keep Terrain Diffusion as reference-only when neither directional lane restores
recognizable mountain morphology. If both pass, prefer continuous only under
the center criteria above. Product promotion, runtime asset formats, source
generation, close detail, and performance optimization remain separate work.

## Result

The maintained pack is
`outputs/terrain/terrain-diffusion-stage-v1/`. Its contract gate passed all
three unchanged fields:

| Seed | Strict focus height | Strict contract | Natural focus height | Minimum camera clearance | Natural contract |
| --- | ---: | --- | ---: | ---: | --- |
| 0 | 2752.74 m | no | 500 m | 490.76 m | yes |
| 9012 | 3252.92 m | yes | 500 m | 486.76 m | yes |
| 12345 | 3244.04 m | no | 500 m | 493.51 m | yes |

Every centered search and selected `16.414 km` support disk remains inside the
finite raster. Natural planning took about `42-45 ms` per field in this review;
it is a setup operation, not frame work. The source stayed at `2048 x 2048`,
`30 m`, with no filtering, mask, resampling, or height shaping.

The first visual pass also exposed a coordinate-contract bug: source-space
direction yaw and orbit-camera yaw have opposite signs. The stage previously
placed the camera on the wrong side for most measured mountain directions.
The shared stage conversion and its tests now make the showcase view face the
selected relief arc. This correction applies equally to the strict and natural
lanes; it does not change terrain morphology.

Clay and surface evidence establish three practical findings:

- Lower natural staging restores recognizable mountain scale across all three
  seeds while preserving broad open directions.
- The cutout lane exposes the absent inner `3.2 km` in the isolated sphere
  study. That region is intentionally owned by the eventual foreground
  consumer, not by the backdrop.
- The continuous lane removes the annular gap, but exposes the native `30 m`
  field and coarse center tessellation in the near foreground. Seed `12345`
  makes this especially clear. It does not pass the continuous-center fidelity
  criterion.

## Superseded Verdict

Carry the bounded directional placement and `500 m` focused stage forward.
Keep `consumer-owned` cutout as the integration contract and require the host
scene to provide terrain or other composition inside `3.2 km`. Do not clear,
flatten, attenuate, or otherwise reshape the source around the subject.

The external 2K raster is sufficient to validate one far-field stage and to
stress the renderer. It is not a complete runtime terrain asset and should not
be used as continuous close terrain. Runtime asset format, larger-area tiling,
true higher-frequency detail, foreground terrain integration, and performance
promotion are separate follow-ups. No further camera tuning or source shaping
belongs in this batch.

That ownership decision described the first staging pack. The current
refinement keeps its source and placement conclusions but replaces the cutout
direction with the continuous contract above.

## Continuous Refinement Result

The maintained comparison is
`outputs/terrain/terrain-diffusion-continuous-refinement-v1/`. All three seeds
reuse the pinned `2048 x 2048`, `30 m` fields and the same selected focus,
showcase direction, lighting, orbit, and full high-density topology.

Uniform sampling replaces the split center's `9.375-116 m` radial spacing with
96 intervals of about `33.333 m` over `0-3200 m`. Both lanes retain 2,657,280
source samples, 2,694,289 vertices, and 5,305,344 stride-one render triangles.
Unit coverage confirms identical counts and a zero center/outer boundary delta;
cutout hashes and default split-profile hashes remain unchanged.

The surface and normal sheets show a focused improvement rather than a new
terrain model. Uniform sampling restores shape on the broad visible foreground,
most clearly on seed `12345`, without changing the mountain silhouette or
introducing a ring, spoke, or sector seam. Seed `0` and `9012` change more
subtly. The normalized showcase-frame RMSE between split and uniform `500 m`
lanes is `0.0027-0.0077`, consistent with a local sampling correction rather
than source reshaping. Remaining softness belongs to the native field and flat
study material, not the old center-ring allocation.

The `750 m` lane increases minimum camera clearance by exactly `250 m` and
reduces foreground dominance, but it also lowers the mountain mass in frame.
That larger composition change is not a consistent improvement across seeds.
Keep `500 m` as the default and retain `750 m` only as review evidence.

The natural-raster executable now defaults to a continuous center with uniform
sampling. `split-log` and consumer-owned cutout remain explicit regression
controls. The general backdrop product retains split linear/logarithmic
sampling by default, so this study does not alter the shipped `radial-v1`
product. A Cartesian inner patch is not justified by this evidence; revisit it
only if a future consumer exposes polar structure that this pack does not.

## Product Promotion

The later raster-v1 product keeps the study's source, placement, `500 m` focus,
continuous ownership, and fixed camera envelope. It adds an explicit
`cubey.terrain.heightfield.v1` asset contract and a product-only seam-matched
center distribution: the final center interval matches the first logarithmic
outer interval while retaining the same sample budget. The study's uniform
lane remains a useful control rather than the shipped raster profile.

Acceptance also found that identical center/sector boundary vertices were not
sufficient under stride 3. Independently restarted angular phases produced
different rendered edge partitions and visible pinholes. Raster-v1 now derives
one global decimated angular partition and tests the actual shared render edges.
The completed product evidence is under
`outputs/terrain/raster-backdrop-product-v1/`; see
[`terrain-raster-backdrop-v1.md`](terrain-raster-backdrop-v1.md) for the exact
runtime boundary and measurements.

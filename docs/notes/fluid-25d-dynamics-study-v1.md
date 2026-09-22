# Fluid 2.5D dynamics study V1

Date: 2026-09-21
Status: evidence complete for neutral and one visibility-stress tier; no
terrain, solver, shader, camera, or protocol changes promoted

## Study question

Does the existing neutral finite-volume `RainPulse` protocol make rainfall,
runoff convergence, pooling, spill, or outflow legible on an immutable terrain
crop, and does one globally fixed visibility-stress tier make the result more
readable? This study is an evidence pass, not a product-selection pass.

The reproducible runner is
[`run_rain_dynamics_study_v1.sh`](../../projects/fluid/fluid_25d/run_rain_dynamics_study_v1.sh).
Select exactly one tier with `RAIN_STUDY_TIER=neutral` or
`RAIN_STUDY_TIER=visibility-stress`; an unknown tier is rejected before any
output directory is created. The runner independently simulates each
checkpoint from reset, so the sequence never interpolates or invents solver
frames. The first image is the earliest honest post-step state (`f1`, 2
simulated seconds). Subsequent sequence images are every 15 solver frames (30
simulated seconds) through `f600` (1,200 seconds). The video left pane is
oblique Composite and the right pane is top-down WaterDepth. Both tiers use
the same sequence and 4 fps playback cadence (10.25 s per video).

## Fixed protocol and matrix

- Existing terrain-case `RainPulse`, finite-volume solver only.
- Native 256x128 crop at 30 m cell spacing.
- Fixed outer step 2 s, eight substeps (0.25 s each).
- Dry initial water and the existing terrain-case outflow-only perimeter.
- No crop-specific forcing, terrain edits, explicit sink, prewet route,
  infiltration, dye, floaters, roughness change, or solver/presentation change.

| Tier | Fixed forcing | Role |
| --- | --- | --- |
| `neutral` | 12 mm/hour for 600 s (2 mm total depth per cell) | Neutral discovery baseline |
| `visibility-stress` | 60 mm/hour for 600 s (10 mm total depth per cell) | Product-visibility stress only; not climate or hydrology evidence |

The stress tier is the only forcing escalation in V1. It is applied identically
to all candidates and uses the same 2 s outer step, eight substeps, f600
horizon, terrain crops, and boundary policy.

| Candidate | Source crop | Crop origin | Runtime terrain identity |
| --- | --- | --- | --- |
| `rolling-hills-catchment` | `rolling-hills-1` | `(128,1728)` | elevation `00fc8836...f2e273c28d`, crop `b453f3a3...117b00d927` |
| `rolling-lowland-branching` | `rolling-lowland-1` | `(1216,1664)` | elevation `fd52d7c3...86dcf7a2`, crop `aa6777e6...1dd6ce63` |
| `canyon-one-flash` | `canyon-candidate-1` | `(1344,320)` | elevation `4c6cda32...3f68b51`, crop `fa51db36...40ff674c` |

The complete unabridged identities, manifest hashes, app hash, GPU, commands,
tier selector, and worktree state are in the tier-specific metadata files:
[`neutral metadata`](../../outputs/fluid/rain-dynamics-study-v1-neutral-20260921-final/metadata.txt)
and
[`stress metadata`](../../outputs/fluid/rain-dynamics-study-v1-visibility-stress-20260921-final/metadata.txt).

## Evidence

The ignored evidence roots are the separate tier outputs:
[`neutral`](../../outputs/fluid/rain-dynamics-study-v1-neutral-20260921-final)
and
[`visibility-stress`](../../outputs/fluid/rain-dynamics-study-v1-visibility-stress-20260921-final).
Each candidate in each root contains 41 Composite and 41 WaterDepth
checkpoints, six WetDry and six FlowMagnitude diagnostic checkpoints, a
`sequence.csv` mapping sequence index to solver frame and simulated time, and a
review contact sheet.

- Neutral [`comparison.csv`](../../outputs/fluid/rain-dynamics-study-v1-neutral-20260921-final/comparison.csv)
  and stress [`comparison.csv`](../../outputs/fluid/rain-dynamics-study-v1-visibility-stress-20260921-final/comparison.csv)
  contain profile rows at the same 15-frame interval. Profile frame `n`
  represents capture frame `n+1`; therefore the last interval row is capture
  `f586` (profile frame 585), while `f600` is separately captured and checked
  by the app's final finite-volume status boundary.
- Neutral [`acceptance.txt`](../../outputs/fluid/rain-dynamics-study-v1-neutral-20260921-final/acceptance.txt)
  and stress [`acceptance.txt`](../../outputs/fluid/rain-dynamics-study-v1-visibility-stress-20260921-final/acceptance.txt)
  record status, conservation, cumulative source, stored volume, and boundary
  outflow for every candidate.
- [`cross-tier-comparison.csv`](../../outputs/fluid/rain-dynamics-study-v1-visibility-stress-20260921-final/cross-tier-comparison.csv)
  prefixes every profile row with its tier and combines both roots. It is
  emitted by the stress run only after the neutral comparison exists.
- Neutral [`videos/`](../../outputs/fluid/rain-dynamics-study-v1-neutral-20260921-final/videos)
  and stress [`videos/`](../../outputs/fluid/rain-dynamics-study-v1-visibility-stress-20260921-final/videos)
  each contain one deterministic 10.25 s H.264/yuv420p split-screen video per
  candidate. `ffprobe` verified 1280x360 playback streams.
- Neutral [`review/`](../../outputs/fluid/rain-dynamics-study-v1-neutral-20260921-final/review)
  and stress [`review/`](../../outputs/fluid/rain-dynamics-study-v1-visibility-stress-20260921-final/review)
  contain reset, post-rain, and final contact sheets for direct comparison.

## Observations

These are direct observations from both capture sets and profile metrics:

1. At `f1` (`t=2 s`) the first rain increment is below the wet threshold. In
   both tiers, profile frame 15 (capture `f16`, `t=32 s`) already has all
   32,768 cells wet for every candidate. Uniform rain therefore creates a
   shallow sheet before a legible channel network forms.
2. In the neutral tier, every profiled checkpoint has zero active-flow cells
   and a 100% slow-pooled wet fraction. At `f586` (`t=1172 s`), maximum
   speeds are 0.012802 m/s (hills), 0.017778 m/s (lowland), and 0.015420 m/s
   (canyon), all below the 0.02 m/s active-flow threshold.
3. The visibility-stress tier first exceeds that threshold at profile frame
   195 (capture `f196`, `t=392 s`) for all candidates, with 14–26 active cells.
   By `f301` (`t=602 s`) it has 2,598 active cells (hills), 3,486 (lowland),
   and 2,993 (canyon). By `f586`, those counts are 3,213, 4,433, and 3,851
   respectively; slow-pooled fractions fall to 0.902, 0.865, and 0.882.
4. At `f586`, stress maximum depths are 0.655373 m (hills), 1.074400 m
   (lowland), and 1.259225 m (canyon), versus neutral values of 0.042465 m,
   0.047885 m, and 0.049238 m. Stress maximum speeds are 0.106565 m/s,
   0.144232 m/s, and 0.137897 m/s respectively.
5. Rain stops at `f300` (`t=600 s`). Boundary outflow continues during the
   remaining 600 s. Neutral cumulative outflow at `f586` is 1,260.071 m3
   (hills), 1,334.385 m3 (lowland), and 1,542.529 m3 (canyon). Stress is
   10,091.563 m3, 12,416.530 m3, and 13,951.590 m3. These are measured
   open-boundary flows, not expected-zero values.
6. Cumulative source volume is 58,980.695 m3 in neutral and 294,906.656 m3
   in stress for each candidate at `f586`, consistent with the exact 5x
   rainfall tier. Maximum absolute profile conservation residuals remain
   small relative to source volume: 1.352/1.197/1.172 m3 neutral and
   8.151/8.759/8.778 m3 stress for hills/lowland/canyon. All finite-volume
   status flags are zero.
7. Visually, stress is materially better than neutral. Neutral Composite is
   almost entirely terrain bed with faint WaterDepth texture; stress Composite
   reveals dark water traces and the WaterDepth pane clearly exposes coherent
   terrain-conditioned networks. The stress contact sheets and videos still do
   not show an explicit rain front, a clearly filled lake spilling, or a
   visibly identified outlet event.

### Final regular-profile comparison

| Candidate | Tier | Max depth (m) | Active cells | Max speed (m/s) | Slow-pooled fraction | Boundary outflow (m3) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `rolling-hills-catchment` | neutral | 0.042465 | 0 | 0.012802 | 1.000 | 1,260.071 |
| `rolling-hills-catchment` | visibility-stress | 0.655373 | 3,213 | 0.106565 | 0.902 | 10,091.563 |
| `rolling-lowland-branching` | neutral | 0.047885 | 0 | 0.017778 | 1.000 | 1,334.385 |
| `rolling-lowland-branching` | visibility-stress | 1.074400 | 4,433 | 0.144232 | 0.865 | 12,416.530 |
| `canyon-one-flash` | neutral | 0.049238 | 0 | 0.015420 | 1.000 | 1,542.529 |
| `canyon-one-flash` | visibility-stress | 1.259225 | 3,851 | 0.137897 | 0.882 | 13,951.590 |

## Observation versus inference

The measured result is that the visibility-stress tier creates real active
flow, a lower pooled fraction, stronger depth contrast, coherent terrain-shaped
networks, and substantially more measured boundary discharge while preserving
zero solver status flags and bounded conservation residuals. The change is
material in the WaterDepth pane and visible, though subdued, in Composite.

The inference is that stress is a useful visibility study tier, not a complete
rain-hydrology or hero product. Both tiers wet the entire grid very early;
rainfall itself is not rendered; and neither video makes a lake spill or an
explicit outlet event unambiguous. Stress demonstrates why simulation matters
more convincingly than neutral, but it still needs a lower-overlay explanation
or a different dynamic cue before it can carry the product story.

## Review rubric

Scores are 1 (poor) to 5 (strong). For “overlay burden,” 5 means the story is
readable with little explanatory overlay. Numerical credibility is scored
independently from visual appeal.

| Candidate | Tier | Cause/effect | Direction | Accumulation/spill | Temporal change | Terrain readability | Overlay burden | Numerical credibility | Artifact level | Video appeal |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `rolling-hills-catchment` | neutral | 1 | 1 | 1 | 2 | 2 | 1 | 5 | 3 | 1 |
| `rolling-hills-catchment` | visibility-stress | 3 | 2 | 2 | 4 | 2 | 2 | 5 | 2 | 3 |
| `rolling-lowland-branching` | neutral | 1 | 1 | 1 | 2 | 2 | 1 | 5 | 3 | 1 |
| `rolling-lowland-branching` | visibility-stress | 3 | 2 | 3 | 4 | 3 | 2 | 5 | 2 | 3 |
| `canyon-one-flash` | neutral | 1 | 1 | 1 | 2 | 3 | 1 | 5 | 3 | 1 |
| `canyon-one-flash` | visibility-stress | 3 | 2 | 3 | 4 | 4 | 2 | 5 | 2 | 3 |

The stress tier clearly beats neutral on temporal change, accumulation
readability, and video appeal. The evidence-based stress rank is **1.
canyon-one-flash, 2. rolling-lowland-branching, 3. rolling-hills-catchment**:
canyon has the strongest relief and boundary response, while lowland has the
largest active-flow count and lowest pooled fraction. This is a study rank,
not a product-promotion decision.

### Hero gate decision

**Neither tier nor candidate clears the full hero gate.** Neutral fails on
visible dynamics. Stress passes a narrower “simulation is doing something”
gate—active flow, coherent network contrast, and measurable outflow are now
visible—but still fails the full presentation gate because the rainfall cause,
directional transport, lake/spill event, and outlet consequence are not all
readable without the diagnostic pane and metric context.

## Reuse boundary for a later storm tier

The runner now has exactly two strict named tiers: `neutral` and
`visibility-stress`. The matrix, capture schedule, profile extraction, video
assembly, hashes, and rubric are shared; only the globally selected fixed rate
changes. There is no per-candidate forcing hook and no third tier in V1.
`visibility-stress` is deliberately labeled as a product-visibility stress
tier, not climate or hydrology evidence.

## Validation

- `bash -n projects/fluid/fluid_25d/run_rain_dynamics_study_v1.sh` passed.
- Invalid tier and invalid executable failure paths return nonzero before
  writing evidence.
- `git diff --check` passed.
- Both tier roots contain 282 candidate PNG captures (41 Composite + 41
  WaterDepth + 12 selected diagnostics per candidate), plus three review
  contact sheets (285 PNGs per tier).
- Both tiers contain three profile CSVs, all status flags are zero, and
  the maximum conservation residual stays below the runner's fixed 0.01% of
  cumulative-source-volume limit. The stress root also contains the cross-tier
  comparison CSV.
- All six videos were decoded with `ffprobe` as H.264/yuv420p 1280x360 streams
  of exactly 10.25 s.
- Both capture and video SHA manifests verify successfully.
- Neutral and stress contact sheets plus extracted mid-video frames were
  visually reviewed side by side. No solver or app implementation changes
  were required.

## Boundary

This study leaves River V0, virtual-pipes/default behavior, finite-volume
opt-in behavior, the mountain source/outlet demo, the analytic source/outlet
demo, neutral audition behavior, and current presentation behavior unchanged.
It does not implement dye, floating objects, infiltration, erosion, another
storm tier, or a promoted rain product.

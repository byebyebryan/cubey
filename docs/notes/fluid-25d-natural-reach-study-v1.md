# Fluid 2.5D natural-reach study V1 — 2026-09-23

## Decision

Keep two native-resolution Terrain Diffusion reaches as **discovery evidence**, not
as a river fixture or default. The narrower crop looks more river-like, but
neither the low-rain neutral run nor the current wide Composite demonstrates a
visible, end-to-end river. Do not add a site-specific inlet/outlet to this
study tier. A separate opt-in river-reach experiment needs a shorter,
bank-contained segment and an explicit visual/transport acceptance gate first.

This preserves the existing boundary: immutable elevation -> shared neutral
protocol -> measured solver response -> human review. The scanner's skeleton
and cross-sections propose places to look; they are not mapped hydrology, and
none of their masks enter the water solver.

## Presentation isolation

The [render A/B runner](../../projects/fluid/fluid_25d/terrain_presentation_ab_v1.py)
compares baseline, physical-metre terrain palette, vertical scale, camera
distance, and their combination on the *same* frozen rank-2 mountain case.
Opt-in terrain-case flags leave existing defaults intact. The [A/B
record](../../outputs/fluid/terrain-site-water-presentation-ab-v1-20260923/rank-02-visibility-stress/metadata.json)
passes: all 28 sampled water/tracer/solver metrics match baseline exactly at
both f300 and f600. The [baseline](../../outputs/fluid/terrain-site-water-presentation-ab-v1-20260923/rank-02-visibility-stress/captures/baseline/f0300-composite.png)
flattens the terrain; the [combined
view](../../outputs/fluid/terrain-site-water-presentation-ab-v1-20260923/rank-02-visibility-stress/captures/combined/f0300-composite.png)
reveals relief but does not turn the dark, broken water filaments into a river.
The A/B metadata hashes its direct runner, executable, and shaders, but not the
older study runner it imports; treat that as a replay-provenance limitation.

The neutral reach captures sharpen the explanation. The [dry narrow-reach
Composite](../../outputs/fluid/terrain-reach-neutral-v1-replay1-20260923/cases/narrower-river-reach-candidate-x843-z809/captures/composite/f0001-composite.png)
shows valley structure, but [f600](../../outputs/fluid/terrain-reach-neutral-v1-replay1-20260923/cases/narrower-river-reach-candidate-x843-z809/captures/composite/f0600-composite.png)
is a nearly uniform blue sheet. The water fragment shader draws depths above
0.1 mm with at least 0.48 alpha, while this neutral run's wet-cell mean depth
is about 2 mm. That overlay can obscure dry-terrain cues even when there is no
bank-filling water. A future depth-aware-opacity A/B should hold solver state
fixed; it must not be read as a hydrology fix.

## Native site search

The [final reach scan](../../outputs/fluid/terrain-reach-scan-v1-final-20260923/scan.json)
uses the [native 30 m scanner](../../projects/fluid/fluid_25d/terrain_reach_scan_v1.py)
on the pinned cached source corpus: 16 manifests, 12 unique 2048x2048
elevation payloads. After excluding the older V2 crop overlaps, it examined
4,409 contexts, checking nested reaches when a context had a long enough
trunk proxy. Only one 512x256 context and 16
256x128 nested reaches met the fixed gentle-grade/channel-shape gates; all
17 are in the same `rolling-wet-lowland` payload (seed 12345). The [contact
sheet](../../outputs/fluid/terrain-reach-scan-v1-final-20260923/reach-contact-sheet.png)
shows the two nonoverlapping shortlisted crops. No new seeds were generated,
terrain was modified, or site was promoted.

| Native 256x128 crop | Median proxy floor | Smoothed trunk grade | Largest downstream adverse rise | Reading |
| --- | ---: | ---: | ---: | --- |
| Broad (x384,z837) | 210 m, ~7 cells | 0.55% | 0.54 m | Gentler through-route, broad floor |
| Narrow (x843,z809) | 90 m, ~3 cells | 0.84% | 2.75 m | More channel-like, but coarse and possible local ponding/overbank limit |

The adverse-rise numbers are diagnostics from a 300 m smoothed bed trace, not
proof of continuous conveyance. The narrow candidate's downstream bank is
locally weak in the sampled cross-section. It needs a smaller-segment,
stage/containment check before any authored endpoint forcing.

## Shared neutral water pilot

The [frozen recipe](../../projects/fluid/fluid_25d/terrain_reach_neutral_v1_20260923.json)
and [standalone runner](../../projects/fluid/fluid_25d/run_terrain_reach_neutral_v1.py)
pin both exact transformed crop hashes, the final scan, and one common render
policy. Both use the existing opt-in finite-volume terrain-case RainPulse:
dry start, 12 mm/hour uniform rain for 600 s, then 600 s without rain, 30 m
cells, 2 s frames with eight substeps, no explicit inlet or sink, and the
existing outward-only perimeter. Each receives ~59,000 m3 (~2 mm per cell).
The canonical [replay result](../../outputs/fluid/terrain-reach-neutral-v1-replay1-20260923/result.json)
and [provenance](../../outputs/fluid/terrain-reach-neutral-v1-replay1-20260923/provenance.json)
are in the ignored output root. The first output root is retained as an
incomplete diagnostic: render overrides were correctly rejected on
WaterDepth/Flow, and the runner was corrected to apply them only to Composite.

| f600 exact checkpoint | Broad | Narrow |
| --- | ---: | ---: |
| Maximum depth | 0.060 m | 0.054 m |
| Wet mean depth | ~0.002 m | ~0.002 m |
| Maximum speed | 0.014 m/s | 0.013 m/s |
| Boundary outflow / supplied rain | 2.9% | 2.3% |
| Active flow cells (>0.02 m/s) | 0 | 0 |

The [narrow WaterDepth f600](../../outputs/fluid/terrain-reach-neutral-v1-replay1-20260923/cases/narrower-river-reach-candidate-x843-z809/captures/water-depth/f0600-water-depth.png)
shows fine drainage traces, not a filled corridor. Both Flow captures are
blank under the existing active-flow threshold. All sampled status flags are
zero and maximum conservation residual is <0.0024% of supplied water, below
the fixed 0.01% gate. This 20-minute, low-rain run tests a shared drainage
response; it cannot establish bankfull behavior, end-to-end transport, or
failure of a separately forced river scene.

## Next gate

Before an opt-in river-reach source/outlet demo, select a shorter segment from
an immutable crop and measure its downstream bed, cross-sections, possible
spill saddles, and outlet connectivity at a *shared target stage*. Then run a
separate, explicitly authored forcing protocol with source and outlet shown,
steady-state inflow/outflow and containment reported, and dyed/floatable
transport crossing an observable distance in the capture horizon. Keep its
metrics and video separate from this neutral rain study. First test
depth-aware thin-water Composite opacity on fixed solver snapshots, so a
rendering change cannot masquerade as improved flow.

## Validation scope

The new scan has 17 focused Python tests; the A/B runner has 6; the neutral
runner has 7 recipe/preflight tests. The integrated Fluid 2.5D CTest
selection passes 17/17. The pilot records app, shader, source, scan, recipe,
runner, crop, command, profile, and capture hashes. These are local GPU and
synthetic/focused checks on a dirty worktree, not field-hydrology validation.
No commit, push, fixture promotion, or default change is part of this study.

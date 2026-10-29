# SDF query acceleration results

Completed locally 2026-10-29. Implementation `603f073a`, measured executable
`865d909326f73267f79d7ebdd4f68cb6409d61ffb8f2be66f5639d608028a68d`.
The [plan](sdf-query-acceleration-research-plan.md),
[progress record](sdf-query-acceleration-progress.md) and
[archived receipts](../evidence/sdf-query-acceleration-loop-2026-10/README.md)
separate hypotheses, diagnostics and final measurements.

## Decision

**Retain adaptive primary stepping as an explicit headless study. Keep automatic
stepping ordinary.** Adaptive passes the useful-gain gate independently with A
and A+B. Fixed factors 1.2 and 1.5 do not meet the 5% gain threshold. The sampled
default, automatic shadow optimization, authored/history formats and current
window admission stay as before. B remains the explicit prepared-cone study.

This is a useful but modest improvement. Side remains roughly 19.3 ms with
adaptive A+B, above 16.7 ms; preset also remains above target. The result does
not establish full interactive FPS, physical acceptance or windowed root-far.
Image differences below prevent treating numerical/performance success as
automatic visual adoption.

## Matched GPU measurements

Whole-scene GPU p95 ranges over three interleaved runs, milliseconds,
1920×1080, supersample 1, analytic/pruned A64 root-far atmosphere,
RTX 5070 Ti / driver 610.57.04. A is the current shadow optimization; A+B
adds the prepared-cone cache. All controls use the same final executable.

| Guardian view | A ordinary | A adaptive | A+B ordinary | A+B adaptive |
| --- | ---: | ---: | ---: | ---: |
| Preset | 19.28–19.58 | 18.83–19.06 | 18.31–18.63 | 17.80–17.91 |
| Side | 21.86–21.92 | 20.34–20.55 | 20.66–20.81 | **19.22–19.31** |
| Rear | 17.17–17.41 | 16.21–16.35 | 16.15–16.30 | **15.25–15.34** |

Mean-of-three-p95 reductions are 2.50% / 6.57% / 5.58% with A and
3.38% / 6.95% / 5.60% with A+B. Side passes the predeclared 0.5 ms and 5%
gate for both source controls, beyond their observed variation. No view fails
the 0.5 ms and 3% regression gate. Multi-material Robot remains on its original
pipeline and shows no meaningful regression. Full fixed-control results are
retained in the receipt; their occasional small gains do not pass that gate.

The batch has 144 native runs, 21,600 complete whole-scene samples and 64,800
GPU pass samples. Each run measures frames 30–179 after 30 warmup frames.
All indices and p95 values are reproduced from the archived GPU arrays and raw
CSV identities. No owned build, test, capture or other GPU job overlaps timing.

**Shared encoder activity is present at every boundary: 10–50%, means about
32–33% across controls.** These are matched shared-load results. An idle-GPU
result is not qualified; no external job was stopped or changed. Startup and
edit latency are outside these GPU frame measurements.

## Why this candidate survived

The qualified current-path diagnostic samples 64,800 full-HD camera pixels
per case using actual raster UVs and renderer buffers. All 12 Guardian/Robot
view/source-control cases pass native image parity, existing depth/normal
contracts and exact required/skipped shadow decisions. The same comparison
passes separately for every stepping candidate. The diagnostic's counts are
not GPU time shares or hardware instruction counts; they count geometric
field queries and CSG nodes, excluding material-expression work.

Guardian averages 8.7–9.5 consecutive exact primary queries between cached
advances. Fixed relaxation adds roughly 1–4% or 15–20% primary queries because
rejected trials offset savings. One adaptive trial, smoothing 0.5 and factor
limited to [1, 1.8], reduces sampled primary queries by 7–10% and primary CSG
visits by 4–11%. Actual GPU timings, rather than node counts alone, decide the
result. The six-query normal stencil and all secondary tracers remain intact.

The predictor trains only on consecutive accepted exact samples. Cached
certificates and rejected trials reset it. Trial endpoints use their own region
lookup. Overlap coverage, with the existing authored/prepared error guard and
additional floating-point margins, must pass before an endpoint can become a
hit. Failed trials return to the certified anchor's ordinary advance and consume
the same 4,096-iteration budget. Inside exits, padded roots and ray endpoints
retain their original contracts. No screen-space forced hit, new epsilon,
normal smoothing or lower quality setting is introduced.

## Numerical, image and lifecycle boundaries

Each candidate passes 155 frozen checks, 21 cone edge checks and 12 new
paired-thin/oblique/inside checks on A and A+B: 1,128 scored checks total.
The actual tracing loop also passes 21 controlled first-hit, inside-exit,
endpoint, cached-transition, nonfinite and exact-budget-exhaustion observations.
The fixed controls reject a tempting later endpoint after an earlier thin
surface. Existing regular/tangent brackets and the one-degree normal contract
are unchanged. These are bounded qualification cases, not universal proof over
every authored scene or GPU implementation.

Forty-eight fresh full-HD captures retain differences against ordinary stepping.
All twelve ordinary controls match their pre-candidate native images exactly.
Robot is byte-identical. For adaptive A+B:

| Guardian view | Changed pixels | Pixels over 1 level | Pixels over 8 levels | Maximum channel difference |
| --- | ---: | ---: | ---: | ---: |
| Preset | 3,750 | 18 | 3 | 131 |
| Side | 2,449 | 1 | 1 | 112 |
| Rear | 2,846 | 2 | 2 | 111 |

Each image contains 2,073,600 pixels. Disabling shadows reduces maximum
differences to 9 / 2 / 3 levels, with only one preset pixel over eight levels.
This supports hard visibility decisions as the main source of the largest
changes; smaller shading differences remain. Different accepted positions
inside the existing hit band can change a hard-shadow edge. No golden or
geometry/normal tolerance is widened. Human visual acceptance remains a
separate gate before any automatic adoption.

Nine-frame real history replays cover empty/first/second prefixes, undo, redo,
branching, appearance reuse, failed publication and retry for every candidate
on A and A+B. Undo/redo and failed-state retention match pixels exactly;
prepared-cache generation deltas keep their expected sequence. Ordinary
reference and prepared-only controls are replayed as well. Six actual CLI
negative checks and focused unit checks preserve the private admission scope.

There are no additional native cache buffers or pipeline-count increases for
stepping. The diagnostic buffer plus UV image uses 33,177,616 bytes of logical
payload, below 32 MiB, excluding allocation/driver overhead. The 132 retained
SPVs and 155 protected content files match their baselines; canonical authored,
field, history, regions, bounds and prepared-cache CPU sources and shared engine
APIs are unchanged. Twelve focused shader/SDF/documentation groups pass.

The separate, validation-clean compiler pass reports 80 registers for every
selected fragment. Adaptive machine code is slightly larger. Implausible driver
local-memory values remain excluded, and static statistics do not identify
live occupancy or stalls. The diagnostic layout was corrected to include
prepared binding 19; its earlier invalid-layout result is excluded.

## Finer pruning decision

The CPU pilot samples 256 active A64 parents, including 225 with smooth
operators, by program length and spatial octant. It builds 16,384 children and
passes 245,760 independent center/corner/stencil checks; maximum value error
or certificate excess is 3.73e-9.

Stratum-weighted point estimates suggest about 27.17 MiB for a full sparse
near-surface level, but roughly **338 ms additional CPU construction** beyond
the roughly 138 ms base build in that pilot. Memory looks workable; construction
does not look compatible with the proposed edit-latency gate. These are forecasts
without confidence intervals, not full-level measurements. Full refinement is
deferred; no failed full-level runtime result is claimed.

The pilot samples spatial/program-length strata; it does not qualify a
view-hot partial hierarchy or measure every high-cost parent. That selection
work remains with a future partial-refinement experiment.

A partial level, faster construction or partial updates needs its own bounded
decision. This loop does not start a GPU builder, windowed root-far port, hybrid
expansion, new modeling vocabulary, default change or public publication.

The local loop is complete with an explicit numerical/performance winner and
recorded visual limits. [Receipts and reproduction](../evidence/sdf-query-acceleration-loop-2026-10/README.md).

[Interactive HTML report](http://starship:8787/reports/baf8361bd5840455f922c302cf52775abf6f7d414c1e9903f663c1274d4647dd/)
includes all stepping controls, native image comparisons and the shadow ablation.
HTTP delivery and offline controls are verified; no browser rendering pass was
available in the delivery session.

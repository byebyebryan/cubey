# SDF query acceleration loop progress

Started 2026-10-29 from `89c06623`, following the
[research plan](sdf-query-acceleration-research-plan.md). Local goal;
no push, default change or windowed root-far admission.

## Phase 0: current workload and child pilot

The selected-query diagnostic reuses the actual renderer camera, lighting,
program, material, region and prepared-cone buffers. Its compute kernel calls
the same tracing, material and shadow functions as the selected fragment shader.
A private fragment variant checks sampled status, depth, geometric normal and
required/skipped shadow decisions against the replay. The ordinary shaders
remain byte-identical to their frozen baseline.

The first attempt used computed pixel-center UVs. Comparing independent shader
stages with a 2e-6 depth / 1e-4 normal-vector / tight radiance threshold failed.
Those are stricter than the frozen geometry contract. The final diagnostic
captures raster-interpolated UVs in R32G32 first, uses the existing 4-EPSILON
regular-depth and one-degree normal contracts, and requires exact shadow-call
decisions. Its validation images must also match the ordinary native image
byte-for-byte. No production or fixture tolerance changes.

Sampled full-HD workload uses one pixel per 8×4 tile: 64,800 camera rays, a
16,588,816-byte result buffer and 16,588,800-byte UV image. Total payload is
33,177,616 bytes, below 32 MiB; allocator/driver overhead is separate. Per-ray
32-bit counters are aggregated on the CPU with wider integers. These are query
counts, not GPU time shares, and diagnostic setup/timing is not production cost.

All twelve Guardian/Robot × three views × shadow/combined captures pass native
image parity, frozen depth/normal and exact shadow-decision checks. Each hit has
six normal queries; all records have identity stamps and finite outputs; no
query exhausts. Guardian has zero root-fallback queries in these samples.

| Guardian view | Primary exact queries | Normal exact queries | Required shadow exact queries | Mean primary exact run |
| --- | ---: | ---: | ---: | ---: |
| Preset | 70,852 | 33,228 | 47,760 | 8.81 |
| Side | 73,039 | 30,996 | 56,754 | 9.45 |
| Rear | 72,983 | 33,996 | 18,656 | 8.71 |

Counts are identical between A and A+B in this sample; this does not mean equal
query cost. Required shadow rays are 5,416 / 2,944 / 946, and skipped shadow
calls 122 / 2,222 / 4,720. Maximum primary exact runs are 269 / 216 / 270.
Long exact runs make checked relaxation worth a bounded trial. Normal queries
remain a separate floor and no CSG-visit fraction is called a GPU time fraction.

The CPU pruning pilot selects 256 distinct active parents by program-length
bin and spatial octant, including 225 parents with smooth operators. It builds
16,384 children and checks 245,760 independent full-source points: centers,
corners and the normal stencil. Zero failures; maximum value error or far-bound
excess is 3.73e-9. Pilot child programs have 41,874 references and 11,790 far
cells. Mean length among non-far children is 9.11.

Stratum-weighted forecasts are about 2.17 million new references, 27.17 MiB total
region payload, and 338 ms child construction in addition to the base rebuild.
These are point estimates, without a confidence interval or full-level timing.
They support deferring full refinement under the proposed edit-latency gate;
they do not reject every partial or differently constructed hierarchy.

Guardian's existing authored arithmetic guard is 0.000151686, about 8.7 EPSILON;
the region guard is twice that. Relaxation must subtract a justified guard from
its empty-ball radii and keep ordinary steps when that removes the opportunity.
No smaller empirical error or donor constant is substituted for that audit.

Decision: test fixed checked relaxation before adaptive prediction. Keep the
current automatic A and explicit A+B as independent performance controls.
Full refinement and GPU construction are not started by this checkpoint.

Raw receipts: `outputs/sdf/query-acceleration-loop-20261029/`.
Qualified workload is `workload-qualified/workload.json`; earlier attempts are
retained as failed diagnostics. One attempt launched an older executable after
a failed build while its shader files had already changed; Vulkan validation
rejected the missing descriptor binding. It is excluded. Subsequent native
execution follows a successful build and clean validation.

## Guarded primary candidates

Three explicit controls are implemented: `--query-stepping relaxed-1.2`,
`relaxed-1.5` and `adaptive-0.5`; `ordinary` remains the default. They require
static headless analytic/pruned A64 root-far atmosphere with shadow or combined
source optimization. Native admission stays on the single-material lit route;
Robot and debug/general fallbacks retain the original shader. Independent
compute fixtures exercise the candidate even with their multi-material test
documents. Normals and all secondary rays retain the current evaluator/tracer.

The loop keeps a certified anchor and tests overlapping zero-free radii before
accepting a speculative endpoint. It subtracts the existing authored/prepared
arithmetic guard plus a point-reconstruction margin; the overlap comparison
also reserves rounding error. A far certificate can certify an endpoint, but
never becomes a signed field sample. Rejections resume the ordinary advance
from the anchor and consume the shared 4,096-iteration budget. Proposals stop
at the root/ray endpoint. Existing inside-exit, hit and normal constants remain.

Fixed controls increased sampled Guardian primary queries: roughly 1–4% for
1.2 and 15–20% for 1.5. This justified one bounded adaptive predictor trial:
absolute-field secants from consecutive accepted exact samples, smoothing 0.5,
factor constrained to [1, 1.8]. Cached advances and rejection reset its state.
It reduces sampled primary queries by 7.28% / 9.60% / 8.97% and primary CSG
visits by 4.22% / 10.81% / 9.74% across preset/side/rear. These are counters,
not frame-time gains. This is a Cubey adaptation of the researched predictor,
not an assertion of identical donor stopping/inside/fallback behavior.

Each candidate passes the frozen 155 checks, 21 cone checks and 12 new paired
thin/oblique/inside checks separately on A and A+B. A controlled kernel using
the actual tracing loop adds 21 tests for a tempting later hit after a thin
surface, inside exit, exact ray endpoint, covered miss, cached-to-exact
transition, nonfinite diagnostics and exhaustion at exactly 4,096 iterations.
Fixed candidates demonstrably reject the tempting later endpoint. Large depth
maxima in the fixture summaries belong to declared tangent brackets; regular
depth and all other tolerances remain frozen.

Full-scene replay qualification passes all twelve cases per candidate. Forty-eight
fresh uninstrumented full-HD captures cover ordinary and all three candidates,
both A and A+B, all Guardian/Robot views. Robot is byte-identical throughout.
Guardian changes a few hundred to a few thousand pixels. Some differences
reach 111–139 output levels at shadow decisions, despite the numerical geometry
contracts passing. No goldens are changed, and these observations do not pass
automatic visual adoption. Timings are a research comparison; numerical gates,
image review and default adoption remain separate.

## Completed matched timing and review

Implementation checkpoint `603f073a`; measured executable SHA-256
`865d909326f73267f79d7ebdd4f68cb6409d61ffb8f2be66f5639d608028a68d`.
The 144-run batch is complete: all four stepping controls, A and A+B, six
Guardian/Robot views, three interleaved repetitions, 180 frames / 30 warmup /
150 completed samples. Every GPU pass has frame indices 30 through 179.

Adaptive A+B whole-scene GPU p95 ranges are 17.80–17.91 ms preset,
19.22–19.31 ms side and 15.25–15.34 ms rear. Matched ordinary A+B ranges are
18.31–18.63 / 20.66–20.81 / 16.15–16.30 ms. Mean-of-three-p95 reductions are
3.38% / 6.95% / 5.60%. Adaptive also passes the useful-gain gate independently
on A's side view. Fixed factors fail the proposed 5% useful-gain threshold.
Robot stays on the original pipeline with no meaningful regression.

All 288 GPU boundary observations show shared encoder use, 10–50%; means are
32.3–33.0% across stepping controls. This is a matched shared-load result, not
idle-GPU qualification. No external process was stopped, and no owned build,
capture or other GPU job overlapped the timed batch.

The shadow ablation narrows the image caveat. With adaptive A+B, 3 / 1 / 2 pixels
exceed eight output levels in preset/side/rear (out of 2,073,600 pixels per view).
Disabling shadows reduces maximum differences from 131 / 112 / 111 levels to
9 / 2 / 3. Small normal/material differences remain; only one no-shadow preset
pixel exceeds eight levels. This supports visibility discontinuities as the
main source of the largest differences, not a claim of universal image identity.
No thresholds or goldens are changed. Automatic visual adoption remains deferred.

Nine-frame authored-history replays pass for every candidate with A and A+B:
empty/first/second, undo, redo, branching, appearance reuse, failed prefix and
retry. Undo, redo and failed-state retention match pixels exactly. The unchanged
reference and prepared-only controls are also replayed. Prepared-cache generation
deltas retain their existing expected sequence. Six actual CLI rejection checks
cover hybrid, A16, optical lighting, moving timeline, general shaders and windowed
requests.

The compiler-statistics utility initially lacked prepared binding 19; that
compile-only result is excluded. Its layout is corrected and a fresh eight-shader
pass has clean validation. All selected fragments use 80 registers; adaptive
code is slightly larger. Driver local-memory statistics remain explicitly
unusable, including the implausible fullscreen-vertex value. These are static
statistics, not live occupancy/stall attribution.

The [result and reproduction record](sdf-query-acceleration-results.md) owns the
final decision and archived receipts.

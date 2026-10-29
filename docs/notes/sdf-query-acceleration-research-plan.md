# SDF query acceleration: research and proposed experiments

As of 2026-10-29, baseline `17a4219c`. Research and planning only: no new
tracer, refinement level, benchmark or default change is implemented here.
The [previous query-cost loop](sdf-source-query-optimization-results.md) is
complete. This proposes the scope of a separate future loop.

## Recommendation

Investigate **fewer exact queries** and **cheaper exact queries** together before
choosing the next implementation. Begin with a bounded workload diagnostic and
a CPU sampling pilot for finer pruning. Then qualify one candidate at a time:
checked fixed relaxation as the stepping control, adaptive relaxation if that
control is promising, or a budgeted sparse near-surface level if pruning has
the stronger opportunity. Do not assume their gains add.

Keep exact CSG at the surface. Both directions retain the reason for the old
hybrid design: grid traversal can be coarse while surface intersections and
normals retain authored detail, including oblique planes and thin sticks.
They also retain canonical history and undo; their caches remain disposable.

## Measured starting point and missing evidence

The completed study measures 1920×1080, supersample 1, analytic/pruned A64
root-far, RTX 5070 Ti / driver 610.57.04. Ranges below are three whole-scene
GPU p95 results per view, in milliseconds.

| Guardian view | Automatic shadow optimization A | Explicit combination A+B |
| --- | ---: | ---: |
| Preset | 19.32–19.58 | 18.32–18.37 |
| Side | 21.80–21.91 | 20.74–20.86 |
| Rear | 17.23–17.47 | 16.14–16.41 |

B is the prepared-cone study. Combined side needs about **19.7% less total GPU
time**, or 24.5% more throughput, to reach 16.7 ms. That target is not an
expected gain from either proposal. These final measurements include shared
encoder activity of 8–50%; they do not qualify idle-GPU or interactive FPS.
Root-far remains headless-only and sampled/full remains the default.

The [capacity forecast](../evidence/sdf-query-acceleration-research-2026-10/capacity-forecast.json)
records the raw metric identity and arithmetic. Current Guardian has 262,144
A64 cells: 249,086 far cells, 13,058 active cells, 562,292 references and zero
root fallbacks. Region payload is 6,443,520 bytes (6.15 MiB), with a descriptive
one-run CPU scratch peak of 8,197,080 bytes. Full geometry publication in the
previous action replay was roughly 139–144 ms; it was not a repeated latency
benchmark.

We do **not** yet know current primary/normal/required-shadow GPU time shares.
The retained 320×180 workload replay predates A and B. Its shadow path uses the
geometric normal and traces every hit; it does not reproduce A's final bumped
normal visibility decision. Old stage ablations are nonadditive whole-program
experiments, not a current cost pie. Six exact normal queries remain per hit;
larger tracing steps cannot remove them.

For perspective only, if a candidate accelerates 25%, 50% or 75% of the frame,
that portion would need to become about 4.68×, 1.65× or 1.36× faster to close
the side-view gap. Those fractions are hypothetical, not measurements.

## What the primary research contributes

The [source ledger](../evidence/sdf-query-acceleration-research-2026-10/sources.json)
records inspected sections, pinned code, access limits and Cubey-specific
inferences. Paper timings are not forecasts for this renderer.

| Approach | Mechanism and fit | Proposed place |
| --- | --- | --- |
| [Checked relaxation, Keinert et al. 2014](https://diglib.eg.org/items/8ea5fa60-fe2f-4fef-8fd0-3783cb3200f0) | Try a step larger than the distance bound; validate overlap of consecutive empty balls and fall back on failure. Smallest useful control for query reduction. | First stepping control, primary rays only. |
| [Planar prediction, Bálint and Valasek 2018](https://diglib.eg.org/items/ce5ffffe-d78a-4d90-8ace-5184cc0f336c) | Uses a locally planar prediction with relaxation. Potentially relevant to grazing straight surfaces. | Retain as an alternative; avoid implementing three steppers at once. Exclude the paper's separate multiresolution strategy. |
| [Automatic relaxation, Bán and Valasek 2023](https://github.com/Bundas102/auto-relaxed-trace/blob/2e9ca0c9d4e4ba9b4ae375457016d27a289a8965/Shaders/trace.slang) | Learns a local slope from accepted field samples, proposes a larger step, and resets conservatively after a failed overlap check. | Adaptive trial after the simple control; finite parameter sweep, no claim of parameter-free behavior. |
| [Segment tracing, Galin et al. 2020](https://aparis69.github.io/public_html/projects/galin2020_Segment.html) | Computes a bound over a whole segment. The inspected code has primitive-specific and recursive bound queries; two point samples do not supply that proof. | Defer the new bound evaluator until simpler stepping is evaluated. Existing global 1-Lipschitz support alone does not provide tighter segment bounds. |
| [Lipschitz pruning, Barbier et al. 2025](https://wbrbr.org/publications/LipschitzPruning/) | Finer cells can remove more exact CSG instructions and establish more far certificates. Benefits can reach primary, normal and shadow queries. | Compare a sparse near-surface extension under explicit memory and build limits. |

The 2023 code calls fixed relaxation `traceRelaxed` and planar prediction
`traceEnhanced`; the 2014 paper itself is titled *Enhanced Sphere Tracing*.
Use mechanism names in our controls to avoid confusing those algorithms.
The pruning paper lists sparse adaptive partitioning and partial updates as
future work (§6.1). The proposed packed child level is a Cubey extension, not
an already qualified feature of its released implementation.

## Phase 0: establish which opportunity survives our current path

This is the first proposed execution checkpoint. It produces a decision receipt
before a production candidate is added.

1. Qualify a diagnostic against the selected A and combined routes. Reproduce
   primary hit/status/depth, the unchanged six-query normal, final bumped-normal
   shadow eligibility, geometric shadow origin and selected cone evaluator.
   Record exact queries, CSG instructions visited, far advances, required/skipped
   shadows, hit/miss/exhaustion, and consecutive exact-query run histograms
   between cached advances. Keep per-ray tail distributions, not just means.
2. Bound diagnostic storage to 32 MiB. For example, one-sixteenth of full-HD
   pixels with a 256-byte record uses 31.64 MiB. Stratify across image tiles;
   retain a denser bounded replay for silhouettes and tails. Use per-ray 32-bit
   counters with CPU 64-bit aggregation. Diagnostic timings are not production
   performance, and instruction counts are not GPU time attribution.
3. Run a CPU-only child-pruning pilot on at least 256 distinct active A64 parents,
   stratified by parent program length and spatial location, including smooth
   operators and expensive ray-visited cells. Build their 4³ children from the
   inherited exact programs. Measure reference counts, far conversion, build
   time and CPU scratch; verify values independently. Report sampling weights,
   quantiles and uncertainty rather than claiming a full-level measurement.
4. Inspect the floating-point safety margin needed for an overlap certificate.
   Start from the current authored error audit, including prepared cones and
   point reconstruction. A pessimistic guard may consume the near-surface
   opportunity. Establish this before selecting relaxation parameters.

Prefer stepping if costly primary work contains useful runs of exact samples
and guarded predictions can remove queries after counting rejected trials.
Prefer refinement if exact programs remain expensive across query types and
the child pilot predicts useful reductions within the budgets below. Short
exact runs interrupted by far advances weaken adaptive learning. If neither
pilot supports a useful gain, stop and record that result; do not expand into
a GPU builder, new expression compiler or lower-quality rendering by default.

## Stepping candidate: required safety contract

Start with static analytic/pruned A64 root-far primary rays. Keep the current
tracer as an explicit control. Do not initially change normals, shadows,
optical routes or hybrid tracing. Fixed checked relaxation can compare factors
1.2 and 1.5 against the ordinary factor 1 control; they are trial parameters,
not accepted tuning. Adaptation is a separate candidate and comparison.

The central hazard is **accepting a speculative hit after skipping an earlier
thin surface**. An exact evaluation at the endpoint does not certify the path
to it. The implementation must retain a last certified anchor, validate the
entire advanced prefix, and only then apply the original hit test.

- Derive conservative empty-ball radii from signed field values and their
  numerical error, conceptually `max(abs(d) - error, 0)`. Test actual point
  separation with a strict rounding margin. Do not copy the donor's `1e-5`
  denominator clamp as a Cubey proof or silently widen our tolerances.
- Reject sign-changing or uncertified trials and resume from the certified
  anchor with the existing ordinary advance. Count every evaluated trial,
  including rejection, against the existing 4,096 evaluation/iteration budget.
  No unbounded inner retry or reset of that budget is permitted.
- Preserve `abs(d) <= EPSILON` and inside-to-exit behavior. Donor code's
  one-sided stopping and final `t + r` return are not drop-in replacements.
  Preserve finite/no-progress diagnostics and required-query exhaustion.
- Never declare a miss merely because a speculative proposal passes the root
  or ray limit. Certify coverage to the endpoint or fall back. Keep the padded
  root and existing endpoint-hit behavior, including inside exits.
- Region and sampled certificates are traversal lower bounds, not exact field
  samples. A cached advance must invalidate/reseed adaptive exact-sample state.
  Each new point performs its own region lookup; a compact program is only
  valid in its certified cell. Do not carry a cell program across a boundary.
- Keep the normal stencil, geometric shadow offset, final-normal shadow
  eligibility and material footprint unchanged. Exclude screen-space forced
  hits, tolerance scaling, fixed-point hit smoothing and dynamic offsets from
  the research papers. They change a different contract.

New tests must include thin paired surfaces with a tempting later endpoint,
grazing/tangent and oblique surfaces, stick bundles, both signs, CSG/smooth
seams, adjacent region cells, cached-to-exact transitions, root/ray endpoints,
and rejection/exhaustion near the budget. Check first intersection and certified
coverage, not just a small field residual at the reported point.

## Refinement candidate: sparse effective 256³ under a budget

Retain A64 as the base and fallback. Only active parents need 4³ child cells.
For the measured Guardian this is 835,712 children, adding **12.75 MiB** of
16-byte headers; dense 256³ headers alone would use **256 MiB**. This is a
capacity calculation, not an allocated or timed candidate.

References dominate the uncertainty. With existing base programs retained:

| Assumed mean child program length | Total region payload, one generation |
| --- | ---: |
| 5 references | 34.84 MiB |
| 10 references | 50.78 MiB |
| 15 references | 66.72 MiB |
| No child pruning, inherited upper bound | 156.18 MiB |

These are scenarios, not predicted child lengths. The inherited bound is
35,986,688 new references, already above the current 33,554,432-entry limit.
That existing limit is 128 MiB of reference payload, not a 32 MiB byte budget.

Propose a **64 MiB retained region-payload cap per device generation**, hence
at most 128 MiB for overlapping old/new device payloads. Count base headers,
child headers and all retained references. This excludes staging, CPU copies,
allocator capacity, source/material data and other renderer resources; record
each separately. Before a full pilot, derive explicit CPU and staging caps
from its layout and the sampled builder, with no unbounded reserve growth.
Full refinement under 64 MiB needs a mean child program of at most about 14.15
references. Partial refinement remains a valid candidate.

Build complete child blocks in deterministic parent order, initially ranking
by parent program complexity without camera dependence. On budget exhaustion,
retain each unrefined parent's valid A64 program. Do not turn those volumes
into empty cells or 2,021-instruction root fallbacks. A complete budget-limited
hierarchy can be published atomically; an incomplete or failed generation
cannot. Any new child-link header kind is disposable renderer ABI only.

Every query type must obtain the correct exact child or parent program.
An exact request in a far child must use its valid A64 parent program; the
child's traversal certificate is not a field value. Existing base far cells
retain their authored-evaluator fallback for exact requests.
Preserve complement bits, original instruction identity, smooth CSG order,
workspace clipping and separate far certificates. Preserve prepared-cache
identity through edit, undo/redo, branch, source reorder and appearance reuse.
Partial-upload or descriptor failure retains the previous complete generation.

Measure full Guardian rebuild/publication before broadening. Proposed stop:
reject a first candidate whose repeated full-geometry publication p95 is more
than **25% and 10 ms above its matched A64 control**, or exceeds its resource
caps. This is an experiment stop rule, not an interactive-edit acceptance
claim; the roughly 140 ms baseline itself remains expensive. A GPU builder or
partial-update architecture would require a separate decision.

## Qualification, timing and decision gates

Use the frozen independent 155 checks and 21 additional analytic edge checks,
then add the failure cases above. Preserve authored/history bytes, tolerances,
normal stencil and source ordering. Cross-check actual GPU routes against the
independent CPU evaluator. For pruning, require the existing exact-program
contract. For stepping, validate first-hit depth/status and normals against
analytic fixtures and the retained tracer; endpoint residual alone is weak.

Capture all three Guardian and Robot views at full quality, plus diagnostic
depth/normal images and thin/inside/tangent views. Retain pixel differences and
their cause. Prepared cones' previous one-level image differences do not give
a new tracer blanket permission to refresh goldens. Any additional image
tolerance needs an explicit reason before timing acceptance.

Benchmark only the uninstrumented final shader after correctness qualification:
matched resolution, cameras, quality, device/driver, 180 frames, 30 warmup,
150 completed samples, three interleaved serial repetitions. Compare candidates
against A and A+B separately, not only against the older 51 ms shader. Keep
owned builds/captures serial; record shared GPU/encoder state. Seek a batch with
zero observed encoder activity; label any shared-load result and repeat if
noise obscures the gate. Do not stop unowned workloads.

Retain the previous useful-gain gate: at least **0.5 ms and 5%** in a Guardian
view, exceeding observed control variation; reject a view regressing by both
over **0.5 ms and 3%**. Also report worst view, median/p95, query rejection tails,
memory, cold/warm setup and edit latency. Do not infer gains by adding separate
trials. Combine only independently qualified winners and remeasure them.

The next loop can finish with a qualified opt-in winner or a well-supported
rejection. A 16.7 ms worst-view result is an aspiration, not a completion
requirement or permission to reduce quality. Automatic adoption, hybrid and
shadow expansion, windowed root-far admission, default changes, physical/human
acceptance and publication remain distinct later decisions.

[Research receipt and capacity assumptions](../evidence/sdf-query-acceleration-research-2026-10/README.md).

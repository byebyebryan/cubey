# SDF continuous editing: research and proposed validation plan

Date: 2026-10-29 local; research retrieved 2026-10-30 UTC.

Status: research complete; the staged native study below is proposed. No live
editor, incremental builder, pending-edit tracer, new default or VR integration
is implemented by this pass. This record follows the
[representation review](sdf-representation-design-review.md) and the
[selectable-mode result](sdf-render-mode-promotion-results.md).

## Assessment

Retained source/history, a committed cache, a bounded pending CSG expression and
budgeted cache construction form a credible candidate for continuous editing.
projectR supplies real precedents for each of several pieces. They do not yet
form an implementation-ready continuous editor in Cubey.

The first native experiment should compare **live geometry over a frozen base**,
including subtraction, against independent geometry and the future baked result.
Do that before building a persistent allocator or a VR input shell. Test the
pending representation, then the updater's service rate, then publication and
actual input. A cheap brush overlay is a separate UI cue and cannot pass as
an edited-surface preview.

The main newly exposed risks are cache-to-live semantics, geometry changes at
bake handoff, sustained edit throughput and canonical source capacity. Faster
rendering alone does not decide these questions.

## Verified starting point

Cubey source is `b9568b85b90f298b4f28f897e77294b081386294`, clean at intake.
Original projectR source inspected is local `master` at
`e2b80796d8f2f9478965a552342bf66fab0e4114`. Its archived chunk engine and its
later active brick engine are different implementations. No Unity application
or headset was launched. The [evidence packet](../evidence/sdf-continuous-editing-research-2026-10/README.md)
contains file identities and independent probes.

| Component | projectR evidence | Current Cubey state |
| --- | --- | --- |
| Stroke input | Archived controller emits jobs as pose/strength changes; one job set groups a gesture. | Scripted strokes and grouped saved history; no live brush input. |
| Budgeting | Archived `LateUpdate` replenishes a computation allowance; per-chunk queues defer work. This is a count budget, not a measured millisecond deadline. | GPU ownership/submission and retirement primitives exist; SDF construction still waits synchronously. |
| Immediate cue | Pending jobs use primitive triangle meshes with different fill/remove materials; they disappear when work finishes. | Depth-aware red cutter cue is color-only. |
| Actual result preview | Brick controller replaces a temporary last edit, runs construction, then draws the resulting working field. Subtraction uses actual edited geometry. | No equivalent live temporary-edit path for authored CSG. |
| Batch reuse | Committed/working fields and a short active batch; filtering and copies reduce old evaluation work. | Procedural box path retains this principle. Authored CSG rebuilds the complete field. |
| Spatial source reduction | Earlier brick-local edit lists; later historical proposals to retain lists instead of distances. | Exact regional programs support source queries. The authored sampling builder still evaluates the complete program. |
| Undo/persistence | Earlier job-set undo/caches; later brick undo bodies are commented out and saved current batches omit earlier baked edits. | Complete named source and saved history are retained; snapshot/recovery studies are stronger than the ordinary reveal-history UI. |

Current **Hybrid** is a distance-query policy: cached bounds guide source-defined
tracing. A **committed base plus pending expression** is a scene/version policy.
They can coexist, but one does not automatically implement the other.

Existing 2026-10-29 measurements remain dated receipts: Guardian geometry-action
p95 is about 939 ms Cached and 1077 ms Balanced; the requested paired-field and
working payload is 912,997,552 bytes. These were not remeasured here. Native
1080p timings are neither a stereo VR budget nor an input-to-display result.

Source accounting is current: Guardian has 1,011 leaves / 2,021 instructions,
Robot 82 / 163. The limits are 1,024 / 2,048. Guardian therefore has room for
only **13 root-appended primitive leaves**. Baking does not remove them from
canonical history. A long session cannot be enabled by silently forgetting its
source or raising shader limits.

## Primary research and what it contributes

These are source/reference assessments, not ported algorithms or predicted
speedups. Videos and attractive images are not used as runtime evidence.

| Reference | Useful contribution | Transfer limit |
| --- | --- | --- |
| [Lipschitz Pruning, 2025](https://onlinelibrary.wiley.com/doi/10.1111/cgf.70057) | Equivalent regional reduction for supported hard/smooth CSG, GPU hierarchical construction, separate conservative far-field bounds; applicable to discretization. | Section 6.1 explicitly leaves partial pruning updates to future work. Cubey's source-query pruner is not already a fast/incremental field builder. |
| [Claybook, GDC 2018 slides](https://media.gdcvault.com/gdc2018/presentations/Aaltonen_Sebastian_GPU_Based_Clay.pdf#page=20) | Pages 20–21 explain local modification through a deliberately bounded distance band, sparse tile work, propagation to mips, and temporary-buffer/copy handling. | Its dense, limited-range field and sampled brushes differ from Cubey's cache. Locality follows from those rules; a primitive AABB alone is insufficient. |
| [Buttenshaw, GPU-driven construction](https://jamiebuttenshaw.com/projects/signed-distance-fields/) and [dissertation, section 3.3](https://jamiebuttenshaw.com/resources/Dissertation.pdf#page=24) | Hierarchical edit culling, dependency analysis and indirect GPU work offer a concrete fast full-construction comparison. | The dissertation explicitly rebuilds its compact structure per modification. Its D3D12/DXR renderer and reported timings do not establish Cubey performance or arbitrary nested-tree semantics. |
| [AMD Brixelizer 1.0 manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/brixelizer/) | Low-level section documents invalidation jobs, static/dynamic separation, reusable brick storage, merging and rebuilding the traversal tree. | Input is triangle geometry, with a different distance representation. This is a storage/invalidation precedent, not an editable smooth-CSG implementation. |
| [MPR, Keeter 2020](https://www.mattkeeter.com/research/mpr/) | GPU interval evaluation and expression reduction accommodate a broader continuous-function class, including warps beyond the simple unit-Lipschitz contract. | A different representation/evaluator. Retain as an alternative if general domain operations become the selected capability. |
| [Reiner et al., 2011](https://cg.ivd.kit.edu/publications/p2011/IntModelingSDF/IntModelingSDF.pdf) | Direct function rendering makes parameter changes visible without rebuilding a sampled surface. | Its deformation stepping discussion includes heuristic safeguards and pathological failures. It is not a correctness donor for arbitrary warps. |
| [Khronos synchronization guide](https://docs.vulkan.org/guide/latest/synchronization.html) | Explicit execution/memory dependencies and reader retirement support publishing coherent versions. | Moving a monolithic dispatch onto another CPU thread does not bound its GPU cost or avoid competition with drawing. Additional queues are not a prerequisite or a frame-time guarantee. |

The Dreams 2015 primary PDF was located but the web reader rejected its size.
No technical claim here relies on its unread contents. No new donor runtime
code is copied; donor licensing/integration would be checked before reuse.

## Contracts to settle before selecting a renderer/updater

### 1. Define continuous editing

Dragging one temporary stamp and continuously drawing a durable stroke are
different workloads. The study must cover both. Raw pose capture is time-based;
stroke segmentation is based on spatial/rotation/radius error, with explicit
handling of fast motion, variable radius, pauses, mirror pairs and release.
Existing capsules/rounded-cone segments are a starting vocabulary, not a new
continuous-sweep primitive. State the segmentation tolerance before results.

Uncommitted replacement poses may be coalesced. Accepted path segments and
completed strokes may not be dropped to hide a backlog. Undo groups a gesture
and its mirrored parts atomically. A later swept-stroke source node or larger
scene budget is a separate representation decision.

### 2. Define what the committed cache can answer

The current cache provides different information: trilinear sampled values
inside bricks, sign/classification for uniform cells, and bounded traversal
information. These are not interchangeable scalar operands. Current
`hybridBound` deliberately falls back to source for uniform labels.

A proposed query interface must distinguish sampled value/error, a certified
interval/sign, and an exact source-function evaluation. For hard operators,
signed intervals compose monotonically: union takes endpoint-wise minimum;
subtraction takes maximum against the cutter's negated interval. A possible
zero requires refinement or source evaluation. A lower bound on absolute
distance alone does not provide the signed operand needed for arbitrary CSG.

Pending additions must invalidate old empty-space skips and expand candidate
bounds. Cuts can expose interior surfaces where no base surface brick existed.
Normal stencils must query the appropriate region for every point. Retaining
old material/region data while displaying new geometry is not valid reuse.

### 3. Choose a preview fidelity contract

Candidate A applies the pending expression to sampled-base geometry; its target
is an explicitly approximate edited surface. Candidate B evaluates source base
plus pending expression, using valid cache bounds only for traversal; it is the
source-defined geometry control. Full-source scalar evaluation is not a claim
of exact Euclidean distance or unlimited numerical precision.

For root append operations, preserve original order, grouping, radii, clipping
and material provenance. A smooth union is not associative. Moving an earlier
group, changing an earlier radius, or warping a subtree is a source-tree change,
not generally an appended inverse operation. Such requests must select an
explicit recompute/fallback path or reject visibly in the first prototype.
Whole-workspace rigid/uniform transforms can reuse local geometry. Arbitrary
warps require bounds on the coordinate mapping and invalidation of affected
derived data; surface relaxation has a separate field/history contract.

### 4. Specify the transition from live expression to cache

An analytic pending cut can be visible before baking and disappear afterward
if the new cavity has no sampled sign change. The independent probe below
constructs exactly this case. This can happen even when baking fresh canonical
source; it is not necessarily accumulated numerical drift.

Measure live versus freshly baked visibility, first hit/depth, normals, material
ownership and shadows at the same camera, including motion. A topology/sign
change in an admitted editing fixture blocks automatic tail retirement until
the fidelity policy is resolved. Possible alternatives are a working patch on
the same lattice/reconstruction, selective source retention, or an explicit
editing resolution limit; each needs its own cost and acceptance evidence.
Keeping unrepresentable edits in an ever-growing tail is not a bounded solution.

Canonical source is the default bake oracle. Initially bake from the source
version, so the error does not depend on an unbounded chain of approximate
rebakes. Fixed-grid hard min/max is a useful non-drifting control; smooth
intermediate quantization and shifted resampling need separate checks.

### 5. Specify versioned publication and failure behavior

A displayed state binds scene/branch identity, source version, cached-prefix
identity, pending suffix, materials, region layout and GPU reader lifetime.
If prefix 10 is cached and a build of prefix 12 completes after edits 13–14
arrive, publish cache 12 with suffix 13–14. Do not clear the entire tail.

Undo/branch replacement may make a build stale even when its numeric prefix
looks suitable. Reject stale candidates, retain the last compatible display,
and retire resources after their GPU readers. Cancellation of CPU preparation,
admission of unsubmitted GPU work and retirement of already submitted work are
different operations. Do not promise to interrupt a running dispatch.

Keep at most one building version and one coalesced future request initially.
An active provisional pose can change without mutating submitted data. Completed
stroke records remain durable. Reuse Cubey's GPU owner and submission tickets;
do not allocate/upload on arbitrary worker threads.

### 6. Bound work and resources

Rendering deadline, preview latency and cache service rate are independent.
If durable arrivals exceed service, the tail grows even while frames look
smooth. Admission needs measured limits and visible backpressure; overflow is
a study failure for that workload, not a successful dropped-input mode.

Split maintenance into measured GPU work units; classify, sample, upload and
publication can each stall. A thread, second queue or indirect dispatch does
not make a long sampling kernel fit between frames. Pre-create relevant
pipelines and report first-use setup separately. Keep ordinary rendering moving
without a queue-idle wait in the editing loop.

Retain the existing 2 GiB requested geometry-resource screen initially, with
complete live/build/retiring snapshots, metadata and staging counted. Also
record allocator/device and CPU scratch peaks separately. A second `Field`
contains its own paired resources; two C++ instances are not just two small
published atlases. No unlimited history fields, tails or stale builds.

## Candidates worth comparing

| Candidate | Main question | Main risk |
| --- | --- | --- |
| A: sampled base + short ordered CSG tail | Is the user's accepted cached visual quality sufficient for real live cuts at an affordable query cost? | Cache query semantics, filled/empty cells, handoff changes, tail scaling. |
| B: source base + tail, with certified cached traversal | How much source-defined quality costs when the tail changes? | Guardian source work may remain too expensive; old bounds need composition with the new expression. |
| C: temporary working bricks + brush cue | Can local or budgeted canonical resampling give consistent cached preview/results? | Preview latency, valid dirty regions, stable addressing and topology updates. |
| Updater comparison: pruned full construction versus local patches | Does a simpler full rebuild sustain the admitted stream, or is persistent local storage justified? | Pruning setup and full sampling may remain too slow; patches introduce allocation, invalidation and retirement complexity. |

Do not implement all candidates as products. A/B are the first small rendering
comparison. Existing synchronous rebuilding is the control. C/local allocation
is a conditional next experiment if the live/cached contract or service rate
requires it. Source-only rendering remains a useful small-scene reference.

## Proposed staged native study

1. **Query contract and frozen-base experiment.** Extend the existing private
   query/fragment study harness with a bounded pending expression and matching
   CPU reference. No editor UI or background builder yet. Compare A/B and the
   old published surface on known sphere/box cuts, additions outside old occupied
   cells, inside exits, grazing rays, smooth joins and region/voxel seams. Reuse
   the 155 frozen source checks without changing tolerances where applicable.
   Stop a candidate on false skipping, unsupported uniform-cell assumptions,
   exhaustion or ambiguous provenance. Exact source and approximate-cache
   profiles must be reported separately.

2. **Measure the complete live path and bake handoff.** Sweep tail lengths
   1/4/8/16/32/64 on eligible scenes; full Guardian is limited to 1/4/8/13.
   Use fixed native cameras at 1080p SS1, Cached as an explicit study profile,
   and include Balanced/Analytical controls. Keep source, lighting and samples
   matched. Measure primary, normals, hard shadows and two-material cut ownership
   together, not only distance queries. Glass/general warps remain explicitly
   unsupported in the new study. Compare the pending image with a fresh
   canonical bake and record topology, error tails and native motion. Stop
   automatic handoff on unresolved admitted-fixture changes.

3. **Select and budget an updater from measurements.** Compare current full
   construction with spatially pruned canonical sampling; include pruning,
   CPU preparation, all GPU stages, transfers, first-use setup and memory.
   Measure work-unit size versus elapsed GPU time before defining admission.
   If a full builder cannot drain the admitted stream within the tail/memory
   limits, investigate stable brick addressing and canonical local patches.
   Derive invalidation from exact dominance/interval information, or explicitly
   introduce a bounded-band cache with halo/topology rules. A brush AABB alone
   is not the implementation. Retain full rebuilding for arbitrary edits and
   recovery. Do not expand into a general shared-engine scheduler in advance.

4. **Qualify publication under concurrent input.** Add owner-safe scheduling
   and version/ticket retirement only after the earlier contracts pass. Test
   new edits arriving during a build, undo, redo, branch replacement, cancel,
   failure/retry, appearance changes, workspace transforms, capacity exhaustion
   and pipeline recreation. Compare settled output with a fresh source rebuild.
   Validate source persistence and exact suffix accounting. Submitted GPU work
   may finish and be discarded; it must never publish to the wrong branch.

5. **Run continuous input and deadline trials.** Use deterministic recorded
   pose streams before a native brush UI. Distinguish provisional stamp motion
   from durable spatially sampled strokes. Report every deadline miss, maximum
   stall, input-to-first-completed-frame latency, accepted/rejected segments,
   tail high-water, oldest-edit age and service/drain behavior. Run input and
   construction while rendering; serial action completion alone cannot pass.

6. **Offer a narrow opt-in native interaction.** Only after the study succeeds,
   wire a desktop brush and atomic gesture undo/save/reload around the qualified
   contract. Existing rendering modes/defaults and accepted content remain
   unchanged. XR host/input, stereo views, compositor timing, physical latency
   and human VR comfort are a later headset qualification; desktop timing does
   not establish them.

## Workloads, proposed gates and stop conditions

These are proposed experiment settings, not adopted product promises. Preserve
the actual output/sample settings in receipts. Run native GPU jobs serially
with no owned build/capture/encoding overlap; record external load observations.

| Dimension | Proposed evaluation / gate |
| --- | --- |
| Frame deadlines | Include B = 11.11 ms (90 Hz engineering profile) from the first timing experiment; also report B = 16.67 ms. Success for a profile means no misses in its specified warmed finite run. Report p50/p95/p99/max and every miss; a failed 90 Hz profile remains failed if 60 Hz works. This is not stereo/headset acceptance. |
| Feedback | Proposed input-to-first-completed-GPU-frame p99 at most 2B, with separate CPU/frame-queue timing. Physical motion-to-photon remains unmeasured. |
| Durable stream | Three repetitions of a five-second synthetic stream at nominal 120 segments/s (600 leaves), plus drain and exact replay. Capture raw poses at 120/240 Hz for sampling invariance; raw pose rate is not automatically leaf rate. Every accepted segment survives, and the tail stays within its measured rendering cap. A limit hit/rejection or growing backlog fails sustainability at that rate. |
| Provisional motion | Three 60-second streams replacing a single uncommitted stamp; this tests responsiveness without consuming source capacity, and does not substitute for the durable-stroke test. |
| Content capacity | Robot plus 600 leaves is 682. A private Guardian history prefix 32 has 389 leaves, so plus 600 is 989. Full Guardian permits only 13 appended leaves and is a short-tail/capacity fixture. Mirrored trials use at most 300 pairs for the same 600-leaf budget. No canonical scene file changes or silent budget increase. |
| Geometry | Independent known-answer controls plus the existing source-contract tolerances; cached/source/handoff diagnostics remain separate. Inside cuts, new empty-region additions, thin cavities, smooth groups and tangent rays must be present. No widened tolerance after a failure. |
| Appearance | Same edited geometry for visible primary rays, normals, cut ownership and hard shadows. A deliberately stale-shadow ablation is labeled separately and cannot pass the coherent-preview route. |
| Handoff | No unresolved sign/topology change in admitted fixtures; report full image/depth/normal differences, including features the cache cannot retain. Numerical gates are set before the native run. Human normal-size and motion review follows. |
| Resources | Requested geometry working set at or below 2 GiB initially, including overlapping and retiring versions; pending source and canonical budgets enforced. Record allocator residency, CPU scratch/staging and build queue separately. |
| Recovery | Exact accepted source sequence after undo/redo/branch/cancel/failure and reload; failed/stale candidates never replace the compatible display. |

A five-second finite stroke test is deliberately bounded by current source
capacity. It cannot justify indefinite VR sculpting. If a longer session is
selected, compact stroke representation and scalable source storage need a
separate acceptance decision; baking alone does not solve them.

## What this research pass validated

The independent [probe](../evidence/sdf-continuous-editing-research-2026-10/probe.py)
passes seven scalar/cell/sequence examples. These are not native shader tests.

- A sphere add/cut changes exact scalar distances outside the primitive's solid
  AABB. This rejects that AABB as a complete exact-value invalidation rule.
- A radius 0.35h cavity inside a box has positive analytic center distance but
  all eight negative sampled corners. Its R16 trilinear reconstruction fills the
  cavity. A cache handoff can change topology even after a fresh source bake.
- Polynomial smooth grouping produces different values when reassociated.
- Across 2,000 fixed-knot chains of 16 operations, hard min/max has zero final
  R16 differences between intermediate and final-only quantization. Smooth
  chains differ in 364 cases, maximum scalar difference 0.0001220703125 in
  the probe's units. This is a limited control, not an image-error forecast.
- 20,000 eight-operation scalar chains preserve an initial 0.001 error bound
  within numerical allowance. This supports bounded base-error composition,
  but does not prove ray safety or topology preservation near zero.
- A sequence example preserves edits 13–14 when prefix 12 replaces prefix 10;
  clearing all pending input loses them. Actual native concurrency remains open.
- A queue example demonstrates growth when arrivals exceed service. Its rates
  are illustrative, not measured Cubey throughput.

Current source capacities and source-file hashes were checked. Browser discovery
returned no connected browser. The HTML report therefore receives static
interaction checks and HTTP delivery verification, with browser-render acceptance
reported separately. No native GPU timing, Unity playback or physical input
acceptance is added by this pass.

## Decision to carry forward

Carry forward **retained source/history + a frozen base + a bounded ordered live
expression** as the first hypothesis to test. Keep the public Cached/Balanced
quality choices available and keep the current default fixed during the study.

The next useful implementation scope is stages 1–2: a private live-edit geometry
and cost comparison with bake handoff. Passing it would justify selecting the
updater and publication work. Failure would identify whether cache fidelity,
source query cost or a temporary patch representation must change first.
No architecture winner or continuous-editor readiness is claimed yet.

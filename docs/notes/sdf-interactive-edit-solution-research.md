# SDF edit service solution review

Date: 2026-11-01. Source reviewed: `6e9bce4e`. Research only: source inspection,
primary references, arithmetic over retained native measurements and a small
mathematical counterexample. No runtime/shader implementation or new GPU timing.
The [evidence packet](../evidence/sdf-interactive-edit-research-2026-11/README.md)
retains the reproducible analysis and reference identities.

## Assessment

The preparation and scheduling proposals are justified, but **they are not a
demonstrated sufficient solution to Guardian's four-frame target**. In the
retained validation-enabled runs, the cut workload needs more than four 4 ms
maintenance windows even before source preparation and publication are charged.
The first new control should establish matched Release
performance with validation off while retaining validation-on correctness gates.
The next implementation should test
preparation, dispatch/submission granularity and an explicit budget/rate choice
as separate factors. A smaller source-admitted scene can independently qualify
the complete input/preview/commit loop.

Exact center-value reuse needs more design before implementation: unchanged
surface topology does not imply unchanged exact distance values. Long-session
storage/history is also a separate representation decision, rather than a
consequence of improving this scheduler.

| Candidate | Assessment | Evidence still needed |
| --- | --- | --- |
| Matched validation-on/off performance control | Required before treating recorded host overhead as production cost. | Same Release binary, shaders, source and workload; separate calibration per mode, exact cache checks and complete-frame timings. |
| Immutable prepared source and palette identity | Strong near-term candidate; fixes actual repeated work. | Stage-level CPU measurements, faithful V2/V3 palette identity, stale-version tests and input-inclusive timing. |
| More specific unit estimates and earlier publication | Useful, with limited measured opportunity in isolation. | Eligibility/completion timestamps, held-out estimate coverage, actual frame and edit deadlines. |
| Coarser work, pooled/batched submissions, explicit 4/6/8 ms controls | Necessary branch of the next service study. | Preserve dependencies and raw cache identity; measure GPU cost, host overhead and complete frames separately. |
| Finite timed rounded strokes on a modest scene | Appropriate integration target. | Bound every queue, preflight all source limits, prove reader retirement and account for every accepted edit. |
| Center state for classification | Conditional research candidate; a restricted form is more promising than a global exact-value cache. | Prove locality for SUBDIVIDED centers, uniform-cell certificates, initialization and newly exposed centers; then measure cost. |
| Spatially reduced CSG programs | Credible later scaling direction. | Cubey-specific preparation/invalidation cost, resource limits and numerical equivalence. |
| Field-authoritative editing with history/checkpoints | Credible long-session alternative. | A new representation/quality contract, new-sample construction, drift and undo/replay tests. |

These are recommendations for a proposed next pass, not adopted public modes or
permission to expand an implementation loop after a miss.

## Preparation is doing whole-scene work

`prepare_cached_stroke` takes two `AuthoredScene` values. It calls
`serialize_authored_scene` on each, parses those serialized documents again,
and extracts only their material arrays for comparison. The serializer itself
parses the retained whole document, updates materials, computes hashes and
pretty-prints the whole scene. Native material data therefore makes a round
trip through the complete geometry/history document.

The service worker prepares the pair, then `CachedCommitStudy::build` prepares
it again. Across these two measured calls, source inspection identifies eight
whole-document parse traversals and four whole-document pretty-print traversals,
plus value copies and hash/validation work. Startup parsing is additional.
The previous native measurements allocate about 19–20 ms to worker preparation
and another 16 ms to builder admission. They do not profile the individual
substeps, so no particular percentage is assigned to JSON alone.

The JSON library documents serialization as linear work; that supports the
scaling diagnosis, not a specific speedup estimate.
[nlohmann JSON API](https://json.nlohmann.me/api/basic_json/dump/#complexity)

Two changes deserve independent measurements:

1. Remove the whole-scene round trip from palette comparison, using a normalized
   palette representation equivalent to the current serialized material array.
2. Seal the prepared result as an immutable, owned snapshot and let the builder
   consume that validation result without repeating the same preparation.

Checking only colors or reading raw material JSON from `scene.document` would
weaken the current contract. Native workshop edits can make that document stale;
V3 recipes, names/order, patterns, optics, domains and library values matter.
The geometry hash omits appearance, while the appearance hash also includes
geometry. Neither is already a complete palette/version token.

The proposed snapshot must own its source, bytecode, palette and pending-prefix
IDs; creation must be restricted to the validator. Bind it to an editor/session
epoch, base cache version and palette version. A geometry hash is insufficient
to distinguish an old job after undo/redo or loading a geometrically identical
scene. Initially reject such stale results conservatively.

Measure new-edit preparation after the input receipt timestamp. Reusing an
immutable, previously validated base is legitimate; preparing the entire future
edited scene before starting the clock is only a precompiled replay control.
Moving work outside the measured interval is not a latency improvement.

## The four-window arithmetic changes the experiment

The analysis reads the October 31 service receipts without executing their
native commands. All 24 service commands require Vulkan validation; there is no
matched validation-off service cohort. Each recorded work unit uses sequential
submit-and-wait, so its completed wall cost contributes to the maintenance
allowance.

| Guardian control | Completed unit wall work | Perfect-cost sequential packing into 4 ms windows | Observed work windows |
| --- | --- | --- | --- |
| Add, 512 groups, three runs | 10.28 ms median | 3–4 | 5–6 |
| Cut, 512 groups, three runs | 19.97 ms median | 6–7 | 10 |
| Cut, 1024 groups, one probe | 16.35 ms | 6 | See retained probe; replacement at 169.7 ms. |

For every 512-group cut run, the total alone requires at least 5–6 windows.
The 1024-group probe also exceeds 16 ms of completed unit wall work. Consequently
the present serialized protocol cannot meet four 4 ms windows solely by making
source preparation free or choosing better estimates. These are lower bounds
for the recorded protocol, not hardware limits: new submission organization can
change those costs, as can a controlled change in validation mode. The command
buffer reference explicitly recommends Release performance measurements without
validation layers; correctness/failure runs should continue to require them.
[Khronos performance measurement guidance](https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html)

A held-out replay of more detailed `(kind, level, groups)` estimates reduces
Guardian cut packing from ten to nine windows in this limited data. Addition
drops from five to four, but one held-out addition unit exceeds its estimate.
This model omits readiness gaps, callback costs and frame/publication work. It
is not a new timing result or a worst-case duration guarantee. Even perfect-cost
packing is only an optimistic counterfactual.

The existing traces lack absolute per-unit ready/grant/submit/completion times.
They cannot attribute every difference between GPU work and 120–203 ms elapsed
latency to avoidable idle time. Add those timestamps before claiming utilization
improvements. The current `before` callback supplies only a work-kind string;
a more specific scheduler also needs the imminent unit's level, group count and
workload context before admission.

## Submission and publication need their own controls

Small dispatches have a measured throughput penalty beyond host overhead:
Guardian cut GPU work was about 16 ms at 512 groups, 12.14 ms in the 1024-group
probe and about 10.6 ms whole in the previous calibration. The exact hardware
cause is not profiled. Do not label it a proven register spill or assume that
batching host submissions alone removes the GPU penalty.

The current unit path constructs immediate commands and waits for each unit;
the service also waits for each draw to complete. Khronos recommends granular
completion tracking and command-pool recycling rather than unnecessary pipeline
drains and repeated allocation. This supports experiments with pooled commands
and bounded submission batches; it does not justify removing required
classification-counter dependencies or memory barriers.
[CPU/GPU synchronization sample](https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html),
[command-buffer management sample](https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html)

Retain 4 ms as the control. Compare dispatch size and submission organization
first, then explicitly test 6/8 ms allowances if complete-frame measurements
justify them. Do not rename a GPU-only allowance as a wall-time allowance or
infer available frame time from render-only GPU measurements. Include close
views and CPU/publication work. An adaptive policy follows fixed-budget evidence.

Polling ready work after maintenance and before drawing could avoid waiting
another draw period. It still needs a coherent version of cache, program,
materials and pending IDs. `Renderer::update_field` rewrites every descriptor
set; its current safety relies on completed readers. Vulkan does not permit
ordinary descriptor bindings used by pending commands to be rewritten.
[Descriptor update requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html)

Cubey already distinguishes `GpuWorkTicket` from `GpuSubmissionTicket` and
provides deferred destruction after a submission ticket. Prefer those existing
completion primitives for the first integration; adding a new timeline API is
not required. A CPU future becoming ready does not, in general, prove completion
of every GPU reader. An immutable CPU pointer alone also does not protect GPU
buffers from premature reuse. The Khronos timeline sample demonstrates the
underlying completion-counter pattern and explicitly bounds queued work to
avoid latency growth.
[Timeline semaphore sample](https://docs.vulkan.org/samples/latest/samples/extensions/timeline_semaphore/README.html)

Keep two Fields and eight uncommitted primitives. If old readers still hold a
bank/version, defer reuse and account for the resulting admission delay. A
separate multi-frame qualification must delay old readers across publication,
cancellation and stale results. Neither additional threads nor queue priority
provides a deadline guarantee.
[Vulkan queue scheduling requirements](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html#devsandqueues-priority)

## Exact center reuse has a validity problem

Consider a finest-level lattice center at x = 0.400390625. Align its y/z with
an old radius-0.1 sphere centered at x = 0: the distance is 0.300390625. Add a
radius-0.01 sphere at x = 0.300390625: the new union value is 0.09. The cell
remains uniformly empty, yet its
exact scalar changed. This is a mathematical counterexample to a proposed
cache invalidation rule, not a bug in the existing builder, which reevaluates
the source.

Therefore a local surface/topology mask cannot by itself keep a globally exact
center cache valid. Candidate strategies are:

- Update every valid center against each new hard tool, including far centers.
- Track the version of each center and replay all required suffix operations
  before use; retained suffix length and per-center version storage are costs.
- Store certified clamped values or intervals tailored to classification, with
  a separate proof and canonical fallback for unresolved/new samples.

The current raw R16 active-sample reuse does not establish any of these
strategies for arbitrary node centers. Keep workspace clipping, zero/sign
behavior, numerical guards and newly exposed child closure explicit.

A dense F32 center array on levels 2/4/6/8 has 17,043,520 entries. With validity
bits, two Fields add about 134.1 MiB. Adding this to the recorded geometry and
largest diagnostic allocation leaves about 88.4 MiB under the 2 GiB screen.
That arithmetic excludes new per-center epochs, work lists, update/copy
traffic, renderer allocations and driver overhead. Memory fit alone is not
enough to select this approach. A dense 32-bit version per center in both Fields
would exceed that screen by about 41.7 MiB before other new storage. Sparse or
coarser version schemes would need separate cost and correctness analysis.

A narrower candidate deserves a separate proof: retain F32 center values only
for currently SUBDIVIDED nodes, and use EMPTY/FILLED nodes as classification
certificates. A finite subdivided center satisfies `abs(d) <= radius + epsilon`.
The existing far-tool condition puts the new tool above that range, so a hard
add/cut leaves that center value unchanged. Dirty centers can be updated or
fall back to source evaluation; newly exposed children need conservative
certificates or canonical evaluation. Uncertain numerical cases must fall back.

This does not promise globally exact scalars for uniform cells. It also has a
smaller possible layout: dense coarse centers plus one value per existing
finest brick slot, about 2.08 MiB per Field including validity bits. That is
arithmetic for a proposed classification-only scheme, not a validated allocation
or speedup. Initial cache seeding, lifetime, clipping/F32 equivalence and exposed
child closure remain proof obligations. This candidate is preferable to blindly
allocating a dense globally versioned exact cache, but is outside the proposed
first preparation/submission pass.

## Long sessions have two credible representation directions

The 1024-leaf cap is an implementation bound, but raising it increases the
full-source work still done by classification and fallback. The parser also
limits tree depth to 24 and stack to 32. A small scene with a long chain of
binary appends can hit depth before leaf capacity. Preflight the complete
finite stream. Same-operation n-ary unions may preserve evaluation order, but
rewriting named history or arbitrary mixed operations needs its own contract.

| Direction | Benefit | Cubey-specific question |
| --- | --- | --- |
| Retained authoritative CSG, derived spatial programs on GPU | Keeps parametric edits and analytic/hybrid source detail available. | Can local program creation/invalidation scale while preserving numerical and history semantics? |
| Bounded sampled working field plus recent tools; history/checkpoints stored separately | Active work need not grow with the entire stroke history. | How are new values, band boundaries, drift, undo and exact settling defined? |

Keeter's MPR establishes GPU interval evaluation and local expression reduction
as a credible way to avoid evaluating a complete expression everywhere. The
current Fidget repository also has a WebGPU tape interpreter and simplifier;
it should not be dismissed as CPU-only. Its native CPU JIT is a separate path.
Neither implementation is a drop-in Cubey evaluator or evidence of identical
F32/R16 results. No library integration is selected.
[MPR research](https://www.mattkeeter.com/research/mpr/),
[pinned Fidget GPU source](https://github.com/mkeeter/fidget/blob/25528e0c2c5251f3b7538b2d43968a0fe5d718c8/fidget-wgpu/src/lib.rs)

Aaltonen's Claybook slides describe limited-range multilevel fields and updating
from previous sampled volume data using only modifications. This supports the
field-working-model direction, but its format and update semantics differ from
Cubey's sparse bricks/uniform labels. The adaptive-distance-field literature
likewise makes reconstruction accuracy an explicit representation choice.
These references do not establish Cubey undo or raw canonical cache identity.
[Claybook slides, pages 20–21](https://media.gdcvault.com/gdc2018/presentations/Aaltonen_Sebastian_GPU_Based_Clay.pdf#page=20),
[Frisken et al., adaptive distance fields](https://www.merl.com/publications/docs/TR2000-15.pdf)

## Proposed next experiment and acceptance

The recommendation is a bounded **preparation + submission/service** experiment,
followed by finite integrated strokes on a source-admitted modest scene. Keep
Guardian as a separate complex-source stress control. Do not block the first
bounded end-to-end trial solely on a near-capacity Guardian model.

| Stage | Compare | Required evidence |
| --- | --- | --- |
| 0 | Existing Release route with validation on/off, before optimization | Match source, SPVs, inputs, work counts and image/cache checks; calibrate each mode independently. Keep validation-on negative cases and report both modes rather than treating layer removal as an algorithmic speedup. |
| A | Existing preparation versus palette-only identity and an immutable prepared token, independently | Timed copy/parse/serialize/hash/validation stages; latency begins before new-edit preparation. V2/V3 persistence, palette changes, altered prefixes, undo/redo/load epochs and cancellation reject stale tokens without losing accepted IDs. |
| B | Existing immediate units versus calibrated coarser units and pooled/bounded submission batches | Same source/work counts, exact topology/R16, unit ready/grant/submit/GPU/complete timestamps, setup/memory costs and numerical/failure controls. Preserve GPU dependencies and one owner. |
| C | 4 ms control, then explicit 6/8 ms trials | Completed frame and replacement deadlines, overruns, queue age and actual maintenance utilization. Record misses without changing the queue, hiding worker time or relaxing quality. |
| D | Timed rounded strokes on a finite preflighted simple/Robot control; separately Guardian | Input at 120/240 poses/s independent of drawing; preserve path/width tolerance, all accepted IDs, eight slots including the tip, bounded input buffering and drain. Qualify versioned bindings and delayed readers before multiple in-flight frames. |

Use both add and cut, constant and changing width, corners, pauses and
release/restart. Reuse the original sphere and zero-edit controls. Retained
source must replay to original Cached output; startup, precompiled-input and
live-preparation results remain separate. No public UI, default, optical,
allocator, smoothing or domain-operation expansion is part of this proposal.

If this bounded route still cannot supply the declared accepted edit rate,
report the limiting stage and choose explicitly between lower admitted work,
a larger measured-safe budget, a new GPU evaluator/cache experiment, or the
field-authoritative quality fork. Exact-center reuse is not automatically the
next implementation merely because memory arithmetic fits.

# Local cache updates and eight pending edits

Date: 2026-10-30 PDT. Intake `79362e46`, clean `sdf-tracing-comparison`.
Research and proposed experiments; runtime, shaders and defaults are unchanged.
The [research packet](../evidence/sdf-local-edit-research-2026-10/README.md)
contains source identities, CPU probes and primary references. No new native
GPU timing is claimed here.

This plan's bounded native loop is complete; see the
[results and measured stop gate](sdf-local-edit-results.md). The
[interactive editing followup plan](sdf-interactive-edit-followup-plan.md)
records the next proposed experiments: a 4 ms maintenance/worker probe for
Guardian, and separate varying-radius rounded-segment and path/width admission
qualification based on existing projectR/Cubey mechanisms.
That bounded followup is now executed; see its
[results and service stop gate](sdf-interactive-edit-results.md).
The original study scope and measurements below are preserved.

## Recommendation

Test **stable brick slots with local patches**, a **bounded eight-edit preview**,
and **construction split across frames**. Start with a copied private candidate
so unchanged bricks need no shader recomputation or remapping. Measure the copy
before investing in a persistent shared atlas. Keep the existing canonical cache
format and full builder as controls.

The complete commit can span several frames. Each maintenance submission must
fit the available frame budget, while the service rate must keep up with the
admitted stamp stream. Eight pending edits provide temporary headroom; they do
not make an overloaded builder sustainable.

The user's selected simplification is either one large discrete edit, or a
continuous stream of small localized edits. Proposed first-study restrictions
are opaque geometry, hard sphere stamps, a fixed predeclared brush material,
and one operation per stroke: all add or all cut. A change of operation waits
for the current stroke to drain. These are experiment restrictions, not inherent
limits of eight-edit composition. Keep smoothing, general domain changes,
capacity redesign, undo UI, brush UI and XR outside this study.

## Current construction still rewrites the whole atlas

The [qualified reuse experiment](sdf-cached-commit-results.md) retains original
canonical hierarchy classification and compact destination allocation. It
reduces old CSG evaluation, but dispatches across every destination brick and
assigns its physical slot from the compact work-list index. Even unchanged
samples are read and rewritten. Numerical fallback is also considered across
the entire surface. This is an implementation choice, not a requirement of
SDF editing.

Pooled complete commits currently take 88–116 ms Guardian and 25–27 ms Robot.
Guardian stage medians include 42–70 ms canonical fallback, about 13 ms global
classification, 16 ms copy/tool work and 14–15 ms study validation. Robot spends
about 18 ms on copy/tool work. These are the previous matched measurements,
without rendering alongside maintenance.

The current cached tracer already follows a logical node's stored brick pointer
and interpolates in brick-local coordinates. It does not require compact physical
slot order. Stable slots can therefore preserve the ordinary Cached traversal.
Active-brick count and allocated-slot high-water mark must become separate study
metadata; count must never be used as the next slot after removals create holes.

## The current sparse field offers a finite locality bound

A full SDF can change outside a brush's solid bounds. For example, outside a
cutter where its distance is +0.5, a solid's distance can change from -10 to
-0.5 under subtraction without changing occupancy. A raw brush AABB is not a
complete scalar invalidation rule.

Cubey does not store that full field everywhere. Its active leaf bricks satisfy
the canonical center test `abs(f(center)) <= R + tau`, where leaf-cell radius
`R = sqrt(3)/(2*256)` and `tau = 2e-6`. Under the supported 1-Lipschitz real
function, every point in an active brick then satisfies:

```text
abs(f(sample)) <= 2*R + tau
               = 0.006767823467... world units
```

This is a bound on values already stored near the surface; it does not require
changing the cache to a newly clamped representation. To turn it into a native
skip certificate, account for center/sample F32 evaluation errors and R16
encoding, for example with a conservative bound of the form:

```text
U >= 2*R + tau + E_center + E_sample + E_storage
```

For a hierarchy cell with radius `R_cell`, a conservative brush lower bound
`b(center) - R_cell - E_brush > U` can prove the old subtree's stored samples
unaffected. Hard union uses the workspace-clipped brush; hard subtraction keeps
the cutter raw. Classification has its own level-dependent threshold. Derive
and qualify both the sample and classification certificates, including the
workspace and strict comparisons around ties. The existing engineering guard
in `authored_evaluation_guard` is a starting point to review, not a substitute
for that qualification.

The general bound-based pruning principle is described in
[Lipschitz Pruning, Appendix A](https://wbrbr.org/publications/LipschitzPruning/documents/LipschitzPruning_submitted_to_EG25.pdf#page=12).
The application above to Cubey's active-brick invariant is our proposed
derivation. It does not adopt the regional evaluator rejected by the last loop.
Dirty samples keep canonical arithmetic; proved unchanged samples keep raw bits.

### Saved native fields support the locality hypothesis

CPU reanalysis scans 55 unique canonical field dumps, covering 2,767,768,064
stored samples. The largest observed absolute stored value is
0.006763458251953125, below the ideal real-function bound. This observation is
not a universal F32 proof.

Across 22 adjacent native chain transitions, radius-.025 tools move along the
existing qualified path. The diagnostic uses the real bound plus an explicitly
unproved `1e-4` margin as spatial padding:

| Subject | Transitions | Active destination bricks | Bricks in diagnostic band | Fraction |
| --- | ---: | ---: | ---: | ---: |
| Guardian | 7 | about 108,000 | 1,108–1,428 | 1.03–1.32% |
| Robot | 15 | about 139,000 | 1,133–1,427 | 0.82–1.02% |

Every observed changed common-brick sample and added/removed leaf lies within
that diagnostic band. Raw brush AABBs miss 210 Guardian and 176 Robot changed
brick occurrences across these transitions. These are repeated occurrences,
not unique bricks. The percentages estimate candidate sampling work; they are
not measured update speedups or full hierarchy-work fractions.

The twelve saved single-edit subject fields give these destination sampling
estimates at their existing add/cut placements:

| Brush radius | Fraction inside the same diagnostic band |
| --- | ---: |
| .02 | 0.40–0.64% |
| .065, the current commit workload | 2.86–5.35% |
| .15 | 9.76–23.43% |

Those counts describe the edited field's active leaves. They do not independently
qualify old-to-new change coverage, removed leaves or exposed child blocks for
those twelve inputs.

### Tree changes require structural closure

A spatial mask alone does not enumerate every header that must be initialized.
When an old uniform node becomes subdivided, its 64 child headers become
reachable, including children outside the brush band. Old descendants below a
uniform ancestor may be stale.

The reanalysis finds 680 Guardian and 1,539 Robot topology-entry presence changes
outside the pointwise diagnostic mask. All are explained by changed subdividing
ancestors within the mask. Existing reachable flags do not change outside it.
The candidate must initialize full exposed child blocks, update ancestors, and
never treat an old unreachable child as valid. Newly active bricks use canonical
sampling. Removed subtrees may leave stale unreachable bytes, provided every
later exposure initializes them correctly.

## What carries over from projectR and other systems

| Source | Useful mechanism | Transfer boundary |
| --- | --- | --- |
| projectR active brick engine at `e2b80796` | Paired fields, old-header lookup, hierarchical edit lists, batch sampling and old-sample reuse. | Its compute pass still remaps every active brick to the work-list index. Its sign/uniform shortcuts are not canonical scalar equivalence. Unity source inspection does not establish runtime or headset performance. |
| Cubey legacy box builder | Committed/working state, retained batches and copied/updated counters. | Still synchronous and compact; box clipping and scalar sentinels cannot become authored-cache rules. |
| [Claybook GDC 2018, slides 20–21](https://media.gdcvault.com/gdc2018/presentations/Aaltonen_Sebastian_GPU_Based_Clay.pdf#page=20) | Restrict modifications to a defined distance band, read old values and copy temporary tiles. | Its limited-range multilevel volume differs from Cubey's format and tree. |
| [AMD Brixelizer manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/brixelizer/#low-level---updating-a-cascade-from-mesh-geometry) | Explicit invalidation, stable brick IDs, bounded update resources and separate traversal rebuilding. | Triangle-driven unsigned distance generation is a storage precedent, not a CSG correctness donor. |
| [MIT CSG ray casting notes](https://groups.csail.mit.edu/graphics/classes/6.837/F98/talecture/) | Compose ray intervals with Boolean operations. | Native Cached hit bands, normals and workspace semantics still need independent tests. |

No donor code is copied. Existing license/provenance boundaries remain; a future
copy requires checking the exact donor files. A general regional-pruner port is
excluded because it failed strict cache identity in the last experiment.

## Private candidate storage

First preserve the old field and make a candidate with the same slot assignments.
Copy its displayed R16 atlas and node buffer using raw GPU transfers, then patch
only affected regions. At axis 64, one atlas is 256 MiB and one node buffer is
73.14 MiB: **345,131,300 bytes per complete clone**, before small metadata.
Transfers preserve storage bits and avoid brush evaluation on unchanged texels.
The existing image and node creation flags already permit these copies; Vulkan
supports explicit [image](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyImage.html)
and [buffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html)
copy regions. No copy latency is inferred from bandwidth specifications.

Keep old slots for retained leaves. Allocate new slots from a bounded append
range and leave removed slots unused in the first finite trial. Track live count,
high-water mark and exhausted capacity separately. Failure leaves the old cache,
source and pending list intact. Capacity exhaustion stops admission or rejects
the private candidate; do not invoke an unsliced synchronous rebuild in the
interactive frame path and call it recovery without a hitch.

The first route still pays a global raw-copy cost. Measure whole and sliced
copies before local-tree implementation. If that floor prevents the service
target, next compare a bounded journal that brings the retired second field up
to the published version by copying changed bricks/header blocks. Reuse it only
after its readers retire. A shared atlas with copy-on-write slots, free-list
reclamation, extra indirection or sparse Vulkan residency is a later alternative.
The measured copy cost decides whether those extra mechanisms are justified.

Retain at most two complete Fields initially. Existing two-field geometry payload
is about 1.70 GiB, before study lists and staging. Keep the 2 GiB requested
geometry-resource screen and report residency separately. A third full retiring
field is not an acceptable shortcut.

## Eight pending edits and exact prefix accounting

The fast current preview handles one convex sphere/box. The older general
multi-edit tracer is a separate, expensive route. Extend the cheap interval
approach rather than treating its eight-edit timings as a prediction.

For eight spheres there are at most sixteen brush interval endpoints per ray.
For a same-operation stroke, merge their occupied intervals and apply the one
add/cut operation to those intervals. Use the existing Cached walk where brush
occupancy does not determine the result; retain one syntactic base-walk call
site. Include shadow rays and primitive normals. Measure endpoint storage,
register pressure and resume work; fixed sizes do not guarantee cheap shaders.
Zero pending selects the original Cached path.

For future mixed hard operations, at any point the last containing tool determines
occupancy; if none contains it, the cache determines occupancy. The CPU probe
checks all 174,762 base/tool/operation Boolean cases for sizes zero through eight,
plus 64,000 random interval points. This supports the composition, but does not
qualify native intersections, tangency, seams or material ownership.

The eight-slot cap includes the frozen prefix being built. It is not eight frozen
edits plus eight new edits. Start with a four-stamp batch, leaving four slots for
new stamps. Immutable edit IDs, a source/branch epoch and the compiled base
identity bind every job to its exact prefix:

```text
Old displayed version: cache A plus edits 1 through 8
Private build target:  cache A plus edits 1 through 4
Replacement frame:    cache B plus edits 5 through 8
After frame completes: retire prefix 1 through 4 and old readers as permitted
```

Input after the frozen prefix remains visible and pending. Do not restart a
running build for each arriving stamp. Bind the cache, source/material data and
pending suffix consistently for each frame. The current `Renderer::update_field`
rewrites every descriptor set; it cannot be reused during in-flight rendering
without changing that lifetime discipline. Use immutable per-version/per-frame
bindings, and do not overwrite controls still read by a submitted frame.

Cancellation, stale completion and a failed build preserve the last coherent
version and pending IDs. No prefix retires merely because an owner callback was
queued or returned. Use actual GPU completion and replacement-frame completion;
retain resources until all readers finish. This follows the ordering requirements
in the [Vulkan synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html).

## Scheduling and admission

Begin with one owner and the existing queue; multiple queues do not themselves
bound latency or guarantee independent hardware, as the
[Vulkan queue guide](https://docs.vulkan.org/guide/latest/queues.html) explains.
Split copy, classification, exposed-child initialization, sample updates and
canonical fallback into measured work units. Poll completion; avoid full-job
waits and unbounded readback/upload/descriptor work on the frame thread.

Calibrate copy slabs and 32/128/512/2048-brick groups. Compare proposed 0.5/1/2 ms
maintenance budgets per 60 Hz frame, including command overhead and all submitted
work. A budget is a measured admission estimate, not GPU preemption. Source-heavy
bricks can require smaller chunks than tool-only bricks. No individual unit may
silently exceed the budget; reduce it, restrict admission or report a miss.

At one admitted stamp per 60 Hz frame, a batch of four must finish and free its
slots within about four frames (66.7 ms) to sustain that rate. A 1 ms/frame
maintenance allocation allows only about 4 ms of GPU work for that batch, before
other serial delays. A large edit can use more frames, but measure completion
under 4/8/16-frame allowances and label a miss; “a few frames” is not established
by background submission alone. If eight slots fill, pause stamp admission and
record the delay. Do not silently discard accepted geometry or build an unbounded
queue. Raw pointer events and accepted geometric stamps are different quantities.

Guardian currently spends about 15 ms on repeated study validation, apart from
initial parsing. Include CPU source preparation in job age and received-to-visible
latency. Either use bounded worker preparation of immutable source snapshots or
qualify reuse of an already validated compiled prefix. Precompiled scripted inputs
are useful controls, but cannot establish live-admission latency.

Eight pending slots do not free canonical source capacity. Existing limits remain
1024 leaves, 2048 instructions, tree depth 24 and stack 32. CPU admission now passes
12-stamp Guardian and 16-stamp Robot streams for each of add and cut. Use those
finite streams initially. Long strokes, balancing source trees, stroke primitives
and history compaction need a separate capacity decision; repeated resets are
not evidence of a sustained long session.

## Bounded implementation sequence

| Stage | Change and comparison | Acceptance and stop condition |
| --- | --- | --- |
| L0 | Qualify numerical locality certificates and tree closure against fresh canonical fields. Measure raw full/sliced clone cost separately. | No missed changed sample, leaf or reachable flag. Complete every exposed child block. Retain guard derivation and adversarial native evidence; the research margin is not the production constant. |
| L1 | One hard edit, stable slots, copied candidate and local classification/sampling. Compare full canonical and qualified global reuse at .02/.065/.15 radii. | Every reachable topology entry and active R16 word matches canonical by logical cell; unchanged slots stay unchanged. If global copy dominates, stop for the journal/storage decision. |
| L2 | If new-brick canonical sampling dominates, test a root-operand dominance certificate before sampling a whole brick. | A full old-source center evaluation plus a strict `2R`/numeric margin can select tool-only sampling where proven. Else retain canonical sampling. Qualify this route separately; do not substitute uniform labels for distances. |
| P8 | Cheap zero/one/two/four/eight-edit interval preview, then prefix batch commit. Primary workloads are same-operation strokes; preserve source/history separately. | Native add/cut, normals, shadows and material ownership pass the frozen Cached-quality controls. Zero is byte identical to original Cached; eight-edit render cost must leave measured maintenance headroom. |
| S1 | Split qualified construction and transfers into bounded units, then interleave with ordinary rendering. | Measure unit maximums and complete-frame p50/p95/p99/max and every 16.67/11.11 ms miss. A render-only GPU time or a CPU job return is insufficient. |
| S2 | Timestamped small-stroke arrivals during a frozen build; publish prefixes and drain. Separately run one large edit. | Never render an edit twice or omit it, exceed eight pending, or mutate in-flight bindings. Report admissions, delays, received/accepted/visible times, prefix completion, commit age and drain rate. Keep scripted/precompiled and live-preparation results separate. |

L2 is a narrow optional acceleration, not a general pruner. For a hard union,
`a(center)-b(center) > 2R + guard` can prove the tool wins throughout the brick;
subtraction compares `-b` with `a`. Workspace clipping and raw R16 identity remain
mandatory. The general regional sampling implementation remains excluded.

The next loop should checkpoint L0/L1 and P8 independently, then proceed to S1/S2
only if their correctness and measured costs leave a credible service budget.
Stop at the first failed qualification gate and deliver the result and next
decision. Do not grow the eight-slot cap, relax quality or add a persistent
allocator to hide a failed experiment.

## Qualification and measurement roster

Retain the 35 prior fixture/radius cases, eight Guardian and sixteen Robot chain
steps, original Cached controls and independent raw snapshot comparisons. Add
affected-band boundaries, closed endpoints, shared brick samples, workspace faces,
large interior cuts, disconnected additions, changed uniform ancestors, repeated
remove/recreate at the same cell, slot holes and append exhaustion. Include the
smallest admitted tools and existing sub-grid diagnostics. Integer-address
remapping must not alter interpolation, normals or shadows.

Preview controls cover overlapping and disjoint stamps, through cuts, nested
cavities, starts inside cache/tool, equal endpoints, tangencies, grazing rays,
workspace clipping and material cut ownership. Measure both preset and close
Guardian/Robot views at 1080p SS1 with existing lighting. Run short rejection
pilots first; for qualified final rows retain three interleaved repeats of
30 warmup plus 150 measured frames, all samples and deadline misses.

Publication tests inject allocation and append/list exhaustion, a real build
failure, cancellation before submission, discard after completion, stale epoch,
input during every build stage and pending overflow. Force delayed old readers
and validate no descriptor/control/resource reuse before completion. At drain,
replay retained source through original Cached and require byte-identical settled
images. Preserve the earlier Guardian unscored depth pixel and preview/bake
sub-grid/grazing differences; cache identity is not analytical accuracy.

Measure copy bytes, classified/skipped cells, exposed children, sampled/copied/
new/removed bricks, canonical and numerical fallback samples, allocator high-water
and requested peak resources. Separate setup, CPU preparation, per-unit GPU work,
waiting, publication and replacement completion. Do not encode/read back images
during timing. Keep owned native jobs serial and retain external-load observations.

Desktop 60 Hz is the proposed first interaction target. Report 90 Hz misses, but
do not claim stereo or physical VR acceptance. The native paced stream must use
arrival timestamps independently of draw count; unpaced headless frames alone
cannot establish the wall-clock service rate. Keep all public modes/defaults and
canonical contracts unchanged. Implementation stays private and local; no push
or project-site deployment.

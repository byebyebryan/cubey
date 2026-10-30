# SDF cache commit: research and proposed experiment

Date: 2026-10-30. Intake: `e042beaf` on `sdf-tracing-comparison`, clean.
Status: research and proposed implementation plan. No runtime, shader, default,
source/history format, builder or scheduler changes in this pass. No new native
GPU timings. The [evidence packet](../evidence/sdf-cache-commit-research-2026-10/README.md)
contains source identities, mathematical diagnostics and reanalysis of saved
native receipts.

## Recommendation

Compare **hard-edit sample reuse** against **exact regional canonical sampling**
before building a persistent dirty-brick allocator. The first experiment should
reuse the existing sample lattice and retain the current full canonical
hierarchy classification. For each destination brick, update old samples with
the single hard tool if that logical brick already existed; evaluate canonical
source for newly required samples. Fetch old data by logical cell and copy it
into the new compact atlas slot.

This is incremental in evaluation work, while initially retaining a complete
hierarchy rebuild and atlas copy. It targets the measured dominant cost with a
smaller implementation than persistent local storage. It is an experiment, not
a demonstrated fast commit. Classifying the full hierarchy, copying the atlas,
sampling new bricks and allocating a candidate may still be too expensive.

Use exact regional CSG reduction as the comparison and the next fallback
acceleration. Only pursue stable local storage when measurements show that
global classification/copying is the remaining limit. The earlier general
[field-update proposal](sdf-field-update-investigation.md) put pruning first;
the user's narrower hard root-append contract now makes sample reuse worth
testing first. General scene mutation still needs a canonical rebuild path.

## Measured starting point

The [qualified one-edit study](sdf-cached-one-edit-study.md) changed Default to
Cached. At zero pending edits it uses the original Cached pipeline. Its private
preview composes one hard sphere or unrounded box, including rigid/uniform
transforms, with a frozen cache. Retained source/history is separate. Those
rendering and publication results are established; continuous editing is not.

The saved commit receipts use runtime `e5bc3628`, visibility-qualified harness
`1a498e7c`, RTX 5070 Ti / 610.57.04, and 960x540 SS1 replacement frames at the
unchanged 3D field resolution. Four successful attempts per subject give these
independently summarized medians:

| Work | Guardian ms | Robot ms |
| --- | ---: | ---: |
| Candidate allocation/initialization | 8.255 | 7.977 |
| Hierarchy classification, GPU | 13.809 | 1.152 |
| Brick sampling, GPU | 1095.508 | 94.446 |
| Entire field build, wall | 1110.660 | 96.376 |
| Publication preparation | 0.045 | 0.025 |
| Ready to render | 1119.064 | 104.342 |
| Completed replacement frame | 1125.025 | 110.760 |

These are old measured runs reanalyzed here, not fresh performance. The final
frame includes readback synchronization and excludes PNG encoding; the ready
time includes candidate creation. Approximately 98.6%/98.0% of field wall time
is sampling. Reducing only allocation, readbacks or publication cannot remove
the main cost.

`Field::build(AuthoredScene)` resets the working hierarchy, classifies depths
2/4/6/8, and runs the complete CSG program for 512 samples per active brick.
Guardian's edited program has 2023 instructions; Robot's has 165. Each dispatch
waits for completion. A candidate allocates 912,997,552 bytes; two complete
`Field` instances request 1,825,995,104 bytes of geometry payload. Each instance
already contains a paired atlas/node arrangement. These are allocation requests,
not measured residency or the complete renderer working set.

## What already exists

| Existing part | Reusable mechanism | Qualification still needed |
| --- | --- | --- |
| projectR brick builder/compute | Hierarchical edit filtering, retained base, copy/update of old sample values into a new compact atlas. | Original source at `e2b80796`; no Unity runtime or headset test. Its locality/sign shortcuts are not canonical authored scalar equivalence. |
| Cubey `Field::build(span<BoxEdit>)` | Completed batches, temporary tail, ordered filtering, copied versus updated brick counters and publication after completion. | It is synchronous, uses uniform-cell sentinels and geometric edit filtering, and has box-specific clipping. Do not transplant those assumptions into the authored contract. |
| Cubey `build_region_programs` | Hierarchical exact-distance postfix references, complemented operands, numerical guard and full-source fallback. | CPU preparation/upload, kernel integration and boundary selection must be timed. Use `far_bounds=false`; traversal certificates are not exact sample operands. |
| Dense logical tree + compact atlas | Morton cell identity is stable even when physical atlas pointers move. | Old child headers below a uniform ancestor may be stale. Resolve ancestors first; do not directly trust every depth-eight header. |
| GPU services/tickets | Owner-safe enqueue, completion tickets, readback status and deferred resource retirement. | The current SDF builder blocks. Bounded submissions and rendering interference remain separate work. |

Sources: [field construction](../../projects/sdf/sdf_field.cpp),
[legacy filter](../../projects/sdf/shaders/filter.comp),
[legacy sampling](../../projects/sdf/shaders/bricks.comp),
[exact regions](../../projects/sdf/sdf_regions.cpp), and
[cached traversal](../../projects/sdf/shaders/trace.glsl).

## Primary research and transfer limits

| Primary reference | Applicable idea | Limit for this plan |
| --- | --- | --- |
| [Lipschitz Pruning, 2025](https://onlinelibrary.wiley.com/doi/10.1111/cgf.70057), sections 3.3–3.4 and 6.1 | Regional branch reduction preserves supported CSG values; useful for discretization as well as tracing. | Partial pruning updates remain future work in the paper. Exact expressions and conservative far-field constants have different contracts. |
| [Buttenshaw construction study](https://jamiebuttenshaw.com/projects/signed-distance-fields/) and [dissertation](https://jamiebuttenshaw.com/resources/Dissertation.pdf#page=24), section 3.3 | Hierarchical edit dependencies/culling reduce expensive brick evaluation; compact storage favors full reconstruction. | Its structure is rebuilt per modification. Sequential edits, bounded distance representation, D3D12/DXR and published workloads do not establish Cubey timings or arbitrary nested-tree correctness. |
| [Claybook GDC 2018](https://media.gdcvault.com/gdc2018/presentations/Aaltonen_Sebastian_GPU_Based_Clay.pdf#page=20), slides 20–21 | Local updates combine previous samples with modifications; sparse tile work follows a deliberately limited distance band. | Cubey currently stores unclamped R16F values in active bricks. Adopting a bounded band is a separate representation/traversal decision. |
| [AMD Brixelizer low-level manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/brixelizer/#low-level---updating-a-cascade-from-mesh-geometry) | Invalidate changed areas, preserve static storage, allocate/release bricks and rebuild traversal data. | Triangle-input distance generation is a storage/invalidation precedent, not a CSG donor. |
| [Khronos image writes](https://docs.vulkan.org/spec/latest/chapters/images.html#images-writes) and [floating-point conversions](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html#fundamentals-fp16) | Image encoding and finite-precision conversion are part of the cache contract. | Generic format-conversion rounding is not fixed to Python's half rounding. Native texel comparisons are required. |
| [Khronos synchronization guide](https://docs.vulkan.org/guide/latest/synchronization.html) | Explicit execution/memory dependencies protect coherent publication and readers. | Another thread/queue does not establish a maintenance time bound. |

These are algorithm/source assessments, not reproduced donor benchmarks. No
donor code is copied. Follow the earlier pinned donor/license audit and recheck
exact files before any future copy; no new dependency or device feature is
needed to propose the first reuse experiment.

## Why hard sample reuse deserves a native test

Let Q be a deterministic monotone, idempotent quantizer and a the canonical
base value at a fixed lattice point. Hard union/difference select operands:

```text
Q(min(Q(a), b))  = Q(min(a, b))
Q(max(Q(a), -b)) = Q(max(a, -b))
```

This follows because monotone Q commutes with min/max and Q(Q(a)) = Q(a).
By induction it also covers an ordered chain of hard edits at unchanged sample
positions. It does not assert that approximate rebaking always accumulates
error, nor does it make a claim for smooth operators or shifted resampling.

The CPU probe expands the earlier fixed-knot control: 200,288 pair evaluations,
2,000 chains of 64 operations and 40,000 workspace-composition checks have no
numeric half-value differences. It also finds **two signed-zero bit differences**
in targeted tie cases. This is evidence for a narrow candidate, not a native
bit-equivalence pass. Float evaluation order, ties, signed zero, subnormals,
overflow rejection and actual image conversions must be covered before reuse.
Preserve the reference's zero semantics or use canonical fallback where needed.

Workspace clipping must remain algebraically correct. With raw base a, tool b
and workspace field c, the stored base represents Q(max(a,c)). Union combines
it with max(b,c); subtraction combines it with **-b, with the cutter raw**.
Clipping the cutter first changes the result. This matches the workspace bug
already caught in the one-edit preview pilot.

Four boundaries remain:

- An old empty/filled cell supplies classification, not an exact base scalar.
  Newly active bricks use canonical sampling initially. No +/-1000 substitution.
- Interpolation and min/max do not commute. A commit can match fresh Cached
  samples while the analytical pending preview still changes when retired.
  Preserve the known sub-grid/grazing diagnostics separately.
- A primitive AABB does not bound changes to the full scalar distance field.
  The first reuse pass visits every destination brick. Copy-only skipping later
  needs a proven dominance test or an explicitly adopted bounded-band contract.
- New topology can assign different physical slots. Copy/update by logical
  cell into each destination slot; no skipped write may reference unrelated data.

## Proposed bounded implementation sequence

### E0: freeze controls and expose sample reuse opportunities

Keep the current one-edit admission: identical retained base prefix, exactly
one root hard union/subtraction, sphere/unrounded box, unchanged palette,
opaque atmosphere, existing workspace and capacity limits. Keep Cached default
and all other modes. Preserve canonical source/history for every successful edit.

Add private study diagnostics to export logical leaf identity and R16 samples
in bounded chunks. Reuse fresh base/full-bake controls for Guardian, Robot and
small fixtures. Count old/new/removed leaf sets and actual copied/updated/
canonical-sampled texels. Net brick-count growth is not a measure of new work.
Do not change rendering tolerance, field resolution or sample locations.

### E1: hard-edit reuse with canonical topology

1. Build destination topology with the existing full canonical classifier.
2. For each destination leaf, resolve the corresponding old cell through its
   ancestors. If an old active brick exists at the same lattice resolution,
   fetch matching texels and combine with the admitted tool using hard min/max.
3. For new bricks, ambiguous numeric cases or unsupported reuse, evaluate the
   complete edited canonical source. Record fallback reasons and work counts.
4. Write to a distinct working atlas with the destination's physical indices.
   Keep the old field immutable. Publish the matching source/material/field
   version after completion; retain pending until a replacement frame completes.
5. Preserve failure/retry behavior. Explicitly reject a stale scene/branch
   candidate even in the serialized study; no general concurrent input yet.

The first E1 pass still classifies/copies globally. It does not need a persistent
atlas allocator or an influence AABB proof. Start by evaluating one tool per
existing texel; dominance-based whole-brick copying is a subsequent optimization.

### E2: exact regional sampling as comparison and fallback

Use the existing exact region reduction, initially A4/A16 with bounded references,
and full-source fallback. Compare a full pruned bake with the original full bake,
then apply the qualified evaluator to E1's classifier and new-brick sampling if
those dominate. Qualify each change separately. Never store a far-field bound
as an exact sample. Include region borders, closed sample endpoints, complements,
workspace clipping and original smooth-base grouping in the controls.

Account for region construction and upload on every changed scene. Historical
Guardian analytical setup receipts had about 35 ms for exact A16 and 445 ms for
exact A64. Those are dated setup measurements, not commit predictions, but they
rule out treating CPU pruning as free. Promote a GPU pruning port only if the
end-to-end comparison justifies it; do not begin with a full donor integration.

### E3: conditional persistent local patches and time budgeting

Reopen stable local addressing only if E1/E2 show that global topology/copying
prevents the desired latency. Define dirty cells with conservative source/tool
dominance intervals, or explicitly select a bounded-band cache. Specify ancestor
updates, new/removed surface bricks, boundary samples, workspace clipping and
normal/shadow dependencies. A brush AABB by itself is insufficient.

Use copy-on-write bricks or a separate working map, explicit free-slot exhaustion
and reclamation after GPU readers. Measure map lookup/render overhead. Preserve
full rebuilding for arbitrary tree changes, recovery and undo/branch replacement.
An in-place mutation plus a saved CPU program does not supply GPU rollback.

Once useful work reduction is established, calibrate bounded classify/sample/copy
submissions (for example 32/128/512/2048 brick groups). Test them interleaved with
ordinary Cached rendering through the existing GPU owner/tickets. Start with a
1 ms maintenance quantum as a proposed engineering target; report actual max
and misses. Source complexity and render headroom determine the usable count.
One pending edit remains the admission bound: while its commit runs, accepting
another durable operation requires a separate queue/input design.

## Acceptance and measurements before promotion

These are proposed experiment gates, not measured results or product promises.

| Area | Proposed test / decision rule |
| --- | --- |
| Sample identity | Compare topology and every active R16 texel by logical cell against a freshly built canonical edited cache. Require bit identity for E1; resolve signed-zero/numeric cases by canonical fallback. E2 must meet the same comparison or remain unqualified; do not widen tolerances to hide errors. |
| Geometry and images | Run the existing 29 regular cached-profile controls unchanged; add logical/region boundaries, starts inside, new surfaces in old uniform cells, whole-brick fill/remove, transformed tools, material cut ownership and workspace edges. Settled images must match fresh ordinary Cached, including shadows. Keep the pre-existing Guardian unscored pixel explicitly unqualified. |
| Repetition | Repeated hard add/cut on admitted small scenes; up to eight Guardian and sixteen Robot edits only if unchanged parser limits admit them. At each step compare with fresh source. Separately exercise zero-value ties, subnormals and shared brick endpoints. No long-session capacity claim. |
| Publication | Inject allocation/build/publication failure, explicit retry, stale version, cancel-before-submit and discard-after-submit. Retain the old field/pending on failure. Retire old GPU resources only after readers; retire pending after completed replacement. |
| Time accounting | CPU compile/admission, cold pipeline setup, candidate allocation/clear, region preparation/upload, per-level classification, copy, brush evaluation, canonical fallback, publication and first completed frame. Report cold and preallocated steady state separately. No PNG/analysis encoding during timing. |
| Comparison matrix | Original full canonical, E1 reuse, E2 pruned full; combine E1/E2 only after separate qualification and a measured reason. Guardian/Robot visible add/cut at small, current .065 and larger .15 radii, plus no-op and topology-changing fixtures. Three interleaved repeats of ten successful commits per subject/operation at current placements; report all samples and finite-run tails. Reset controls explicitly so repeated timing does not silently grow source. |
| Useful acceleration | Proposed first usefulness bar: >=5x reduction in Guardian and >=2x in Robot median warm commit-to-ready, without image/sample regression. This justifies follow-up; it does not establish smooth editing. Also report how many complete commits meet 16.67/11.11 ms. |
| Rendering during maintenance | After bounded submission exists, measure complete frame p50/p95/p99/max, every 16.67/11.11 ms miss, preview latency, commit age and CPU stalls. Test preset and close views; do not infer headroom from preset alone. A fast serial commit is not a concurrency pass. |
| Memory | <=2 GiB requested geometry working set including old/build/retiring fields, source/region/control buffers and GPU staging. Record allocator/residency and CPU scratch separately. No third full Field or unbounded scratch. Candidate pooling may remove repeated ~8 ms setup, but its retained bytes still count. |
| Scope protection | Original Cached shaders/output, defaults and all explicit modes; canonical scenes/presets, source/history/material formats and numerical gates retained. Current limits remain 1024 leaves, 2048 instructions, depth 24 and stack 32. |

If new-brick canonical sampling dominates, prioritize exact local programs. If
classification dominates, qualify pruning there before persistent topology. If
copying dominates, stable local storage becomes justified. If allocation
dominates, reuse a bounded preallocated candidate. If GPU work units still stall
rendering, split or restrict admission rather than relabeling background work.
Any correctness failure keeps that route private and falls back to full rebuild.

The next loop should stop after the bounded E0/E1 comparison and the smallest
necessary E2 experiment, with a measured decision on E3. A brush UI, smooth
pending edits, general domain changes, source-capacity redesign, undo UI and XR
remain later work. No implementation, push or site deployment occurs in this
research pass.

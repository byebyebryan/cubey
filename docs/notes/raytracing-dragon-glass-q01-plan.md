# Dragon glass Q0.1: numerical replay research and proposed next pass

2026-11-08. Research follows the [Q0 measurements](raytracing-dragon-glass-q0-results.md).
This pass inspects source/evidence, reviews primary references and checks replay
inputs on the CPU. It implements no probe, renderer fix or new quality policy,
and runs no new GPU captures. Recommendations below remain proposals.

**Recommendation:** the next implementation pass should replay and classify the
known medium failure, apply the smallest correction justified by that evidence,
then reopen full-HD reference qualification. Keep the accepted dragon, marble
slab, shared atmosphere/sun and canonical material unchanged. Q1 smoothing,
Q2 filtering and Q3 motion reconstruction remain separate.

## Confirmed starting point

The Q0 production-policy failure is pixel `(908,344)`, 1920 × 1080, seed 7,
zero-based sample `1335`. The first failing prefix contains 1336 samples; the
1335-sample prefix is clean. Repetition is byte-identical. Doubling the budget
to 256 events / 2048 radiance queries / 8192 total queries retains bit 16, as
does disabling shadows. No event/query/shadow/stack exhaustion accompanies it.

The current bit does not identify the failure mechanism. On the active workshop
radiance route, two checks emit it:

| Check | Cases currently collapsed into bit 16 |
| --- | --- |
| Inside medium, `s.id != medium` | Missed exit (`s.id < 0`) or hit on a different instance |
| Dielectric hit, `entering != (medium < 0)` | Geometric-normal facing disagrees with expected entry/exit |

The [closest-hit payload](../../projects/raytracing/shaders/primary.rchit) retains
distance, barycentrics, instance and primitive, but not hardware hit kind. The
[workshop path loop](../../projects/raytracing/shaders/material_workshop.glsl)
also has an audit asymmetry: its main continuation checks the outgoing geometric
hemisphere; the queued Fresnel reflection at depth 0/1 does not have the same
world-space check before enqueue. That identifies a useful probe point, not a
proved bug or an instruction to discard that branch.

The selected route has one certified glass body and one opaque support. Its
instance culling-disable flag is zero, so medium-aware facing culling is active.
The dragon has unit scale and a vertical translation; the slab has an identity
transform. General shear/large-world transform problems are therefore a lower
initial priority than local facing, branch state and spawning. The exact uploaded
matrices and positions still belong in the replay identity.

## What the literature changes about the investigation

[NVIDIA's origin-offset analysis](https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/)
explicitly excludes direction-side flips at extreme grazing angles from its
origin guarantee. Its intersection bound is tuned for RTX hardware. Primitive
ID rejection also does not cover nearby adjacent triangles. These limits make
direction and local-neighborhood measurements necessary even with the donor
offset implementation already present. Dropping grazing glass paths is not an
assumed remedy: [dielectric reflection/TIR](https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF)
can retain substantial weight.

[Vulkan ray traversal](https://docs.vulkan.org/spec/latest/chapters/raytraversal.html)
defines facing from projected ray-space triangle area and exposes that result
through hit kind. A shader recomputes its normal/direction dot product through
different arithmetic. A disagreement near zero is therefore a hypothesis to
test. The specification's shared-edge/closed-fan watertightness wording is an
expected/tested `should`, not a universal exact-arithmetic guarantee.

[PBRT's rounding-error discussion](https://www.pbr-book.org/4ed/Shapes/Managing_Rounding_Error)
also explains why increasing a fixed epsilon can skip nearby geometry. Preserve
the error-bound reconstruction and measure the actual spawn displacement before
adjusting it. The current adjacent-face bisector can magnify the offset; its
clearance relative to other nearby faces needs evidence, especially at folds.

[Woop, Benthin and Wald's watertight intersection work](https://jcgt.org/published/0002/01/05/)
is a suitable reference for robust local edge tests. A naive double-precision
Moller–Trumbore implementation is not automatically an exact oracle at shared
edges or vertices. Double precision is a diagnostic baseline; near-zero cases
need a robust predicate or an explicit unresolved classification.

## Proposed implementation sequence

### R1 — A faithful event journal and exact replay

Start with the full 1336-sample reproducer, keeping the original logical view.
Capture only the target pixel and logical sample, including every branch it
spawns. Record explicit reason codes for inside miss, wrong-instance hit and
orientation disagreement; retain the existing aggregate bit 16.

The journal should include:

- Pixel, seed, logical sample, branch ID/parent, split/pending order, depth and
  RNG sequence/dimensions. Reflection children and trunk continuations need
  separate identities; sample index alone does not name a branch.
- Incoming medium, instance mask, culling flags, ray origin/direction, `tmin`,
  `tmax`, and query counts before traversal; returned distance, instance,
  primitive, barycentrics and `gl_HitKindEXT` for a hit. A miss needs a tagged
  record, not uninitialized hit-kind data.
- Hit position before spawning, outward normal, local scattering direction,
  basis/world direction before and after normalization, both side tests,
  Fresnel/TIR, reflection/transmission decision, weight and resulting medium.
- Spawn normal, scalar offset, actual rounded displaced origin, near-edge
  distances, participating neighbors/projections and the next traversal result.
  Capture the queued reflected child at enqueue and dequeue, not only the trunk.
- Model/geometry and material identities, GPU/driver, executable and exact
  SPIR-V hashes, compiler options and the uploaded instance matrices.

Store float bits alongside readable values. A bounded single-invocation log can
avoid global atomic contention; its counter/overflow flag must be explicit.
Allocate from actual event/query limits with checked byte arithmetic, retain
the first failure and all its ancestors, and treat overflow as an invalid probe.
Give the journal its own opt-in output rather than increasing the existing
32 MiB whole-image optics dump cap.

Keep it project-local to this triangle/medium investigation. As an illustrative
state-size estimate, four 128-byte packets per query would use about 2 MiB at
4096 queries or 4 MiB at 8192, before headers/branch records. Final allocation
must follow the chosen packet layout and proven record bound, not that estimate.
Use a separate debug control; `environment.glass.w` already selects optical
spawning and the dragon-hidden reference and must not become a probe ordinal.

Use a separate owned diagnostic build/pipeline and shader directory. Do not
replace the live build's SPIR-V files in place or allow a frozen executable to
silently pick up different shaders. Hardware hit kind is available in closest
hit, so the debug payload must explicitly carry it back to ray generation.
Compile all participating stages with the same payload layout and pin the
whole shader set; replacing only ray generation would not establish that ABI.

There are two fidelity gates:

1. Passive logging must reproduce the baseline failure, with unchanged raw
   output or a clearly explained instrumentation effect. Extra shader code can
   change compilation; observation is not assumed non-invasive.
2. Only then add a fast isolated-sample replay and compare its event/branch float
   bits with the full-prefix journal. If they diverge, retain full-prefix replay
   while resolving the difference; a nonreproducing probe cannot select a fix.

**Keep logical 1920 × 1080 coordinates.** Changing the render to 1 × 1 changes
the pixel hash, primary jitter, camera ray, cone footprint and proposal choices.
For an isolated invocation, preserve the original pixel/extent, uploaded camera,
seed, sample 1335 and branch sequencing. Record that this is one replayed sample,
not an accumulated 1336-SPP image. Do not fabricate history counts to bypass
these semantics.

The research-only CPU check supplies useful starting assertions: linear pixel
`661388`, random key `3943738118`, stratum batch/within `20/55`, dimension-0 key
`914582703`, cell `36`. Expected float32 subpixel coordinates are approximately
`(.5198941231, .5716764927)`. Integer arithmetic was transcribed from current
source; the GPU must confirm float-bit agreement. No camera ray or failing
event has been measured in this research pass.

### R2 — Explain the first divergence, then choose one correction

Replay the recorded ray against the unchanged serialized float32 geometry and
uploaded transforms promoted to double precision. Inspect the emitting triangle,
its adjacent faces and any nearby intersected surface; expand the neighborhood
if necessary rather than assuming only edge neighbors matter. Compare the
reconstructed hit point and displaced origin, and retain ordered candidate
distances, side signs and their separation in float ULPs. Reversing culling is a
diagnostic counterfactual, never the default interpretation of a successful ray.

| Measured first divergence | Candidate investigation/correction | Evidence required |
| --- | --- | --- |
| Hardware hit kind and recomputed facing disagree | Separate traversal facing from geometric scattering normals; examine arithmetic and the uncertainty band | Same boundary identified, both classifications retained, justified numerical semantics, grazing controls |
| Reflected/transmitted direction changes side during basis conversion or normalization | Improve or bound that construction, including the pending reflected branch | Pre/post direction bits, geometric side margins, Fresnel/TIR and branch weights preserved |
| Displaced origin leaves the intended local volume or crosses nearby geometry | Correct the local spawn/crease handling or its assumptions | Measured displacement and neighborhood clearance, thin-fold/adjacent-face controls |
| Wrong instance or corrupted branch/medium state | Correct indexing, routing or branch bookkeeping | Matched AS/shading identity and parent/child state; no medium toggle used as recovery |
| Float traversal disagrees with the robust local query near an edge | Derive a bounded numerical policy for the observed ambiguity | Classified uncertainty and neighborhood evidence; no unsupported claim of exact hardware intersection |

These are alternatives selected by R1, not a patch list. Do not blanket-increase
epsilon/tmin, ignore a primitive, remove culling, flip a ray after sampling,
toggle medium state or silently treat an inside miss as sky. Keep failures
visible. A correction that changes direction or discards positive throughput
needs its probability/energy and image-bias consequences assessed; hemisphere
admissibility alone is insufficient.

Do not remesh/repair the dragon to make the reproducer disappear. Reopen model
clearance only if the captured local geometry actually contradicts its audit or
has separation below the numerical policy's supported resolution.

### R3 — Requalify the production path and close the replay work

Preserve the failing baseline and make one focused candidate reviewable. Require:

1. Clean exact prefixes 1335, 1336 and 2048 at the original pixel/view/seed, plus
   the deeper-budget and shadows-off controls. Check the whole image, not only
   that pixel. No raw throughput cutoff, status suppression or new hidden
   termination is allowed.
2. A small neighboring pixel/sample neighborhood and one minimal geometric
   regression for the measured mechanism, with grazing/near-critical-angle,
   shared-edge, crease and thin-feature controls as relevant. Define uncertainty
   honestly for coincident/edge cases; do not encode arbitrary float tie order.
3. Full-HD canonical, elevated and grazing views with independent seeds 19/43
   at 4096 SPP, keeping the production budget and zero invalid/budget flags.
   Retain seed-7 reproduction coverage. Extend selected references to 8192 only
   if reference uncertainty still prevents the intended comparison.
4. Existing optical qualification, IOR-1/neutral/tinted controls, default/deep
   budget agreement, material/base isolation, native edit/reload/orbit/resize,
   AS reuse, memory accounting and clean Vulkan validation. Raw/display image
   differences need explanation; unchanged output is expected only for the
   untouched baseline or unaffected controls, not as a universal fix criterion.
5. A capture from the normal build with diagnostics disabled. Journal buffers,
   descriptors and shader overhead must be absent from that path, and source,
   executable and SPIR-V identities must match the measured candidate. Profile
   active tracing separately from Xvfb presentation and settled dispatches.

R1's first deliverable is a classified failing path and a minimal reproducer.
The whole pass clears the Q0 hold only after R3. If instrumentation loses the
failure, the robust query remains ambiguous or multiple unrelated mechanisms
appear, retain the evidence and narrow the follow-up; do not call the repair
complete or promote another visual policy.

In particular, copying the main continuation's rejection check onto the queued
reflection is not, by itself, an accepted fix. It could remove the medium flag
by terminating a positive-weight path. Report rejected/discarded weight and
branch counts as well as status, and verify that no such replacement explains
the apparent improvement.

## Product roadmap after this gate

After numerical acceptance, resume the guarded smooth-normal/consistent-footprint
study for the faceted appearance. Keep the solar-variance and one-sample motion
findings as separate sampling/reconstruction work. Xvfb cost is not a desktop
performance target. GI, caustics, clouds, rough glass and a different model remain
outside Q0.1; none is needed to explain this failure.

[Research evidence and preservation](../evidence/raytracing-dragon-glass-q01-research-2026-11/README.md)
record the source observations, primary references, replay-input check and
unchanged runtime. The next goal loop can implement R1–R3 within these boundaries;
this research pass implements none of them.

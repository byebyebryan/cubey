# Frozen-base live-edit experiment: execution record

Date: 2026-10-30. Goal: stages 1–2 of the
[continuous-editing plan](sdf-continuous-editing-research-plan.md), with local
commits and a native evidence/report deliverable. No background updater,
continuous brush UI, persistent allocator or VR integration is part of this loop.

## Intake and recovery point

- Runtime starts from `b9568b85b90f298b4f28f897e77294b081386294`.
- The preceding research files and note-index edit are this thread's work;
  capture them in a planning checkpoint before runtime changes.
- Raw output root: `outputs/sdf/live-edit-loop-20261030/`.
- `baseline.json` retains original executable, all existing shader hashes and
  protected authored/history/field/region/material/recipe, preset/scene and
  shared source identities; `baseline-sdf` is the original executable copy.
- The initial GPU boundary observation is RTX 5070 Ti / 610.57.04, 0% GPU and
  encoder utilization. This is not a continuous exclusivity claim.
- Owned native work, building, tests, capture, image analysis and encoding will
  remain serial with respect to GPU timing. External processes remain untouched.

## Implementation boundary and gates

Use a separate private study executable and shader entrypoints, reusing the
native SDF renderer and lighting. Public mode/CLI semantics stay unchanged.
The field and source acceleration describe the frozen base; the combined
canonical source supplies material ownership and the fresh-bake control.

Candidate A traces a sampled base plus the pending postfix suffix. Uniform-cell
labels remain intervals/certificates, with explicit source fallback when an
actual value is required. Candidate B composes valid cached base intervals with
the suffix and evaluates source geometry when the interval cannot certify a
step. B must apply workspace clipping after the combined expression, rather
than inserting a clipped base into the middle of a smooth expression.

Admission must establish that the combined program has an unchanged base prefix
and a supported bounded suffix. Preserve canonical 1,024-leaf / 2,048-instruction,
tree depth 24 and stack 32 limits. Additive tails can use the canonical variadic
left-fold operator. Smooth/mixed operations cannot be reassociated to evade
depth limits. Unsupported source edits reject before resource allocation.

Freeze the existing source fixture gates: regular depth four source epsilons
(`4/(256*7*32)`), compatible normal one degree, exact declared classifications
and no exhaustion; retain each fixture's existing tangent/known-answer rules.
The approximate candidate is scored against its declared sampled-base geometry
and separately against source truth. No tolerance changes after a failure.

For automatic bake handoff, require admitted fixtures to retain classifications
and meet those declared regular depth/normal gates. Report native visibility
and image differences independently, including thin-feature loss. An inherent
cache-fidelity failure blocks updater/retirement promotion; it does not justify
erasing the case or calling the renderer exact. Stage-2 cost measurements may
still characterize an explicitly approximate preview, with that failed gate
visible. The full rendering path includes edited normals, hard shadows and
two-material cut ownership.

Timing is native 1080p SS1, three serial interleaved repeats with 30 warmup plus
150 measured frames. Preserve complete per-frame values, p50/p95/p99/max and
counts above 11.11 and 16.67 ms. Static GPU times do not establish input latency,
sustained throughput, concurrent baking, stereo rendering or physical VR.
Candidate tails are 1/4/8/16/32/64 when canonically valid; full Guardian is
1/4/8/13. An inadmissible mixed/deep tail is recorded, not rewritten silently.

## Progress

Planning checkpoint: `800b4692`. Implementation adds a private executable,
eight private shader entrypoints and an optional renderer attachment. Existing
194 shader binaries and 567 protected source/content files remain unchanged.

Pre-timing pilots pass all 155 existing source checks and 58 pending checks for
B; A passes the 58 pending checks and has the expected sub-grid/thin-normal
source-fidelity failures with zero pending leaves. No tolerances were widened.
The CPU sampled oracle now interpolates the actual eight stored R16 corners;
recreating half conversion on the CPU differed from the GPU cache at one Robot
point. Source known-answer contracts remain independent of those corners.

An initial oversized box fixture exceeded the existing 262,144-brick frontier
capacity. The smaller box preserves the cavity/through-hole question; the
clipping case uses a small sphere crossing the workspace boundary. Failed pilot
receipts remain in raw output directories. A negative admission pilot also
accidentally shared Python objects between base and edited inputs; independent
copies correct that harness bug.

Native pilot images exposed shadow exhaustion in A. Legacy lookup's spatial
bias was incompatible with this walk's boundary advancement, especially at
small shadow-ray parameters. Private lookup now chooses the forward cell at an
exact boundary and solves the crossing from the original ray using one
representable grid coordinate. It has no arbitrary forced minimum march step.
Camera/shadow replay now completes for both subjects/policies without exhaustion.

Native compute-to-fragment quality validation at 320x180 has zero flagged pixels
for B on both subjects. A flags one Guardian classification pixel and three
Robot depth/normal pixels; those pixels are unqualified, not a full-image pass.
This and the sampled bake handoff failures block production promotion. The
remaining stage-2 measurements characterize a private approximate prototype.
Static native motion changes camera and pending geometry with exactly one base
field and region build; it serializes upload/readback and proves no input-latency
or concurrent-updater claim.

The first 1080p timing attempt at `f39fb741` was stopped after four completed
Robot runs: A was about 110-114 ms p95 and B 93-97 ms. Those are failed pilot
receipts, not final matrix results. Whole-workspace traversal and zero-bound
uniform cells caused avoidable full-source evaluation in empty space. The
private controls now carry the combined edited source bounds; A expands them
by `.002 + 3*base_guard`, covering the active-brick reconstruction envelope
and rotated primitive sublevel enclosure. An active brick's center lies within
its diagonal radius of zero, so its corners are bounded by twice that radius;
the existing trilinear/half/guard envelope is below the chosen expansion.
Hard and supported smooth operators do not amplify a scalar base error.
Pending additions are included in the combined bounds; empty roots exit early.
Uniform cells also intersect the existing signed region certificate with their
cache interval, recovering a useful bound without treating it as a scalar.
The bounded pilot is about 13.3 ms p95 for A at zero-tail Robot; all frozen
source and pending geometry checks still pass. Final timing is rerun under a
new committed identity rather than mixing the two shader versions.

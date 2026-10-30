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

Planning checkpoint being captured. Native candidate implementation has not
started at this checkpoint. Later sections will record actual gates and source
identities rather than relabeling older receipts as current measurements.

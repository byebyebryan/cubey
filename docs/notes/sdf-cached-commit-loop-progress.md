# Cached commit experiment: execution

Date: 2026-10-30. Intake `1f8bc565`, clean `sdf-tracing-comparison`.
The user authorized the bounded loop in the
[cache-commit research plan](sdf-cache-commit-research-plan.md).

## Frozen scope and acceptance

Implement private E0/E1: canonical topology, logical-coordinate lookup of old
samples, hard root append, raw cutter/workspace semantics and canonical fallback
for new/numerically ambiguous samples. Preserve the full canonical builder as
control, all existing Cached shader identities/defaults, source/history/material
contracts and limits. Compare reachable logical topology and every active raw
R16 texel with a fresh canonical bake; signed-zero mismatches are failures until
resolved by fallback. Keep the existing regular geometry/image gates unchanged.

Measure full versus reuse cold/preallocated commits for Guardian and Robot;
three interleaved repeats of ten commits at the current visible add/cut
placements, plus smaller/larger tools and adversarial fixtures. Repeated native
chains stay within unchanged parser limits. Separate compilation, setup,
allocation, classification, sampling, publication and completed-frame cost.
No owned builds, analysis, captures or encoders overlap timed GPU jobs.

Use at most two complete Fields and bounded staging below the 2 GiB requested
geometry screen. Old data remains immutable during building. Retain failure,
retry and stale-candidate checks; clear pending only after a replacement frame
completes. Qualify the smallest exact A4/A16 regional sampling comparison needed
to explain remaining cost. A useful speedup target is >=5x Guardian and >=2x
Robot without sample/image regression; a miss is reported, not hidden by weaker
quality or faster timing boundaries.

Stop after E0/E1 and the bounded E2 decision. Persistent local storage,
background/continuous-input service, brush/undo UI, source-capacity changes and
XR are separate work. Keep commits local; no push or project-site deployment.

## Intake evidence

Preserved current public/study executables, all 206 SPIR-V hashes and tracked
source/content identities in
`outputs/sdf/cached-commit-loop-20261030/baseline.json`.
Initial RTX 5070 Ti / 610.57.04 observation: 18% GPU, 0% encoder, 797 MiB used.
This is a boundary observation, not continuous GPU exclusivity.

## Private implementation checkpoint

The study uses a separate friend helper; ordinary Field methods and all 206
original shader binaries remain unchanged. The first reuse shader evaluated
canonical fallbacks in divergent groups and was too costly. Compacting new
bricks and numerical fallback texels into a separate canonical pass removes
that avoidable work. Group-local list compaction reduces contention. These
development pilots are excluded from the final measurement set.

The candidate resolves old logical cells through all ancestors, preserves
workspace/raw-cutter composition and routes zero/subnormal values through the
canonical evaluator. A bounded two-million-entry fallback list uses about 8 MiB;
overflow selects a full canonical build rather than publishing partial samples.
Raw R16 images are copied in 16 MiB chunks for complete logical-cell comparisons.

Four current Guardian/Robot add/cut pilots match reachable topology and every
active stored texel. Formal fixture/radius/chain qualification, matched cold and
preallocated timings, native images and publication dispositions follow this
checkpoint. Focused SDF/documentation/shader checks pass 13/13. No continuous
input, persistent allocation or time-sliced updater is implemented.

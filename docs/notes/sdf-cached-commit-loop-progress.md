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

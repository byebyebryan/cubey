# SDF render-mode promotion execution

Started 2026-10-29 from `a9ab2976`, with a clean worktree on
`sdf-tracing-comparison`. The [plan](sdf-render-mode-promotion-plan.md) owns the
scope. Raw evidence: `outputs/sdf/render-mode-loop-20261029/`.

## Initial checkpoint

The baseline preserves 192 existing SPVs and the prior native executable.
The first change admits only uninstrumented SS2 cached-quality studies;
headless/static/lighting and all geometry-diagnostic limits remain intact.
No product mode/default, authored/history/builder or shared-engine change.

The bounded sampling matrix contains 32 configurations, three serial
interleaved repetitions, followed by 32 native captures. Each timing run uses
180 frames, 30 warmup and 150 verified samples per GPU pass. Owned GPU jobs,
builds, tests and encoding stay out of the timed batch. GPU load is observed
at run boundaries; this is not a continuous exclusivity claim.

Runtime selection, compatibility/lifetime checks, promotion and final report
remain pending.

# SDF optimization opportunity investigation

Date: 2026-10-29 UTC. Baseline: `651b9074`, initially clean.

## Investigation boundary

Profile the qualified automatic source shader, then rank optimization experiments
by measured frame-time opportunity, correctness/quality risk and implementation
cost. This pass may add private static diagnostic shaders and evidence tooling;
it does not adopt a new tracing algorithm, tolerance, sky rendering route,
pipeline cache, scene representation or default. Keep accepted content and
complete source/history semantics intact. Work stays local.

## Evidence plan

- Save the baseline binary and all 108 existing SPIR-V plus 138 content hashes.
- Re-measure the actual automatic path at three fixed Guardian 1080p views.
- Add fixed diagnostic stops for primary, normal, material and shadow on the
  same debug-free/eligible-material shader conditions. Compare a full diagnostic
  against the automatic shader's pixels before using it as a proxy.
- Compare selected-path shadows/procedural/bump toggles and a compiled
  no-background ablation. Ablations are whole-program controls, not additive
  stage timers, and changed-output controls are not accepted optimizations.
- Gather executable statistics for the actual Vulkan material specialization.
  Continue excluding the driver's invalid local-memory statistic.
- Reuse only hash-matched historical workload counters, clearly labelled as
  320p counter replay rather than current 1080p hardware time.
- Read primary references for the opportunities supported by those results.
  Rank bounded next trials and define their acceptance/stop conditions.
- Retain source, measurements, limitations and an HTML report. Do not install
  profiling tools, change machine-wide caches/settings, push or deploy.

Raw receipts: `outputs/sdf/optimization-investigation-20261029/`.

## Completed investigation

- 0fef50a0 and 1ccd71ca: selected stages, specialization statistics and admission checks.
- dbed1a97 and 3bb17e9d: private zero-light shadow probe and order-only control.
- 153 repeated runs / 22,950 GPU samples, exact capture parity, preserved source/kernel bytes and primary-source review.
- [Findings](sdf-optimization-opportunities.md) recommend shadow qualification
  before adoption, then independent primitive-preparation / guarded-step trials
  and a budgeted finer-pruning survey.

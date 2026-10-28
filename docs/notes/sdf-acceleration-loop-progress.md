# SDF acceleration first loop

Date: 2026-10-28. Starting checkpoint: `81e6216` on
`sdf-tracing-comparison`. Scope: E0–E3 in the
[research plan](sdf-acceleration-research-plan.md).

The authorized loop implements and qualifies the ordinary GPU-local control,
the hybrid clamp experiment, conservative root bounds and separate regional
traversal bounds. The sampled default, authored content, frozen fixture answers
and history semantics remain fixed. Work stays local. Stop after the decision
report; finer regions, GPU construction, default adoption and publication are
separate decisions.

## Checkpoints

- E0: ordinary corrected control and portable source/region upload.
- E1: global sampled-bound clamp audit and isolated candidate.
- E2: offset-aware conservative model root bounds.
- E3: explicit exact/far/fallback regional cells and source-only hit/normal paths.
- Qualification: frozen independent fixtures, additional bound controls,
  per-variant semantic publication/undo replay, camera/optical checks.
- Measurement: serial GPU work, matched ordinary pipelines, three interleaved
  repetitions and existing resolution progression guards. Counters are separate.
- Review: evidence and decision report, local checkpoints, unchanged defaults.

## Execution record

- Initial worktree clean; research proposal re-read. No concurrent edits found.
- Goal loop active. No new GPU results yet.

- E0 implemented: private `control` selector, ordinary padded-root shaders,
  portable staging/device-local source and region buffers. Source allocation is
  prepared before publication. Both source modes pass 155 frozen independent
  checks and the 20-action replay; seven existing CPU tests pass. Raw receipts:
  `outputs/sdf/acceleration-loop-20261028/e0/`. Timings pending.

- E1–E3 implemented as private variants. Preliminary candidate matrix passes
  all six sets of 155 frozen checks and all six 20-action semantic replays.
  Far certificates include the workspace clip and retain inherited parent
  certificates without shrinking their construction radius. Exact far-cell
  requests use the full source. Root bounds propagate sublevels through smooth
  unions. The hybrid controls share an additional independent numeric allowance.
- All 54 previously recorded shader binaries retain their hashes. Final expanded
  qualification, ordinary timings and decision report remain pending.

- Expanded CPU bounds checks pass after correcting a synthetic fixture that
  omitted required source metadata. Seven CPU tests now pass, including
  transformed cases for all seven primitives and separate far-bound inequalities.
- Existing background NVENC encoding was observed. The user was asked whether
  to wait or measure alongside it; no unrelated process was changed. Per-run
  GPU/encoder load, clocks, temperature and power are recorded for timing.

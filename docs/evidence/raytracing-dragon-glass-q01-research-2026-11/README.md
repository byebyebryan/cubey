# Dragon glass Q0.1 research, 2026-11-08

See the [proposed numerical replay/fix pass](../../notes/raytracing-dragon-glass-q01-plan.md).
This is research and planning only. No new GPU ray, probe, renderer candidate or
correctness pass is claimed.

- `source-observations.json`: active route, collapsed bit-16 cases, pending
  reflection/main-continuation asymmetry, current diagnostic limitations and
  explicit hypotheses. These observations do not establish the failing event.
- `sources.json`: reviewed primary sources and the limits of what each supports.
  NVIDIA's origin/direction distinction is reviewed from the article body,
  separately from its automatically generated summary.
- `replay-input-check.json`, `replay_input_check.py`: deterministic CPU
  transcription of the target pixel/sample's integer RNG inputs and expected
  float32 primary jitter. It is not a GPU float-bit comparison or a path replay.
- `validation.json`: documentation checks and preservation of the current dirty
  RT worktree's source, executable, SPIR-V, materials, presets and model hashes.

The Q0 invalid-medium gate remains failed. The proposed journal must distinguish
inside miss, wrong-instance hit and orientation disagreement, reproduce the
baseline, and retain every branch before a correction is selected.

Reproduce the CPU input check from the RT worktree:

```sh
rtk proxy python3 docs/evidence/raytracing-dragon-glass-q01-research-2026-11/replay_input_check.py --output outputs/raytracing/dragon-glass-q01-replay-input-check.json
rtk proxy ctest --test-dir build/dev -R '^documentation_' --output-on-failure -j1
```

Full starting-state hashes remain under
`outputs/raytracing/dragon-glass-q01-research-20261108/`. Previous Q0 work and the
original SDF worktree are preserved. No goal, implementation, commit, push or
report/public-site publication is part of this research pass.

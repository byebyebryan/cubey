# Dragon glass Q0 evidence, 2026-11-08

See the [measurement results and next loop](../../notes/raytracing-dragon-glass-q0-results.md).
This pass measures the committed optical policy; it does not qualify a new
appearance or a full-HD high-SPP reference. The full-HD gate failed on one
invalid-medium pixel and remains explicitly open.

- `summary.json`: measured convergence, solar controls, reference uncertainty,
  final native timings, identity and the unresolved acceptance boundary.
- `failure-diagnosis.json`: exact full-HD pixel/sample, binary search,
  byte-identical reproduction, deeper-budget and shadows-off controls.
- `failure-receipts.json`: commands, artifact hashes and explicit rejected
  transport policy for the failing prefix and controls. Failed diagnostic
  captures are never accepted quality references.
- `analysis.json`: regional raw/display errors, reference-floor estimates and
  phase-separated native statistics. Proxy masks and Xvfb timing scope are
  described in the results; these numbers are not physical-display acceptance.
- `validation.json`: scoped tests, baseline-versus-instrumented image identity,
  native validation and unchanged shader/model/material/preset identities.
- `diagnose_failure.py`: asset-bound, strict-scope reproduction driver using the
  project capture utility. Its diagnostic calls explicitly retain failures.
- `build_q0_report.py`, `report-template.html`: authoring inputs for the internal
  image comparison, plots, failed-gate explanation and native recording report.

The opt-in tools live under `projects/raytracing/tools/`. Normal builds/tests
do not download the model or add NumPy/Pillow dependencies. The seven statistics
and frame-join checks are an opt-in, asset-independent test script.

Run from the RT worktree, with the accepted prepared model already available:

```sh
rtk proxy uv run --no-project --no-build projects/raytracing/tools/measure_dragon_glass.py --output outputs/raytracing/q0-pilot-reproduction --suite pilot
rtk proxy uv run --no-project --no-build projects/raytracing/tools/test_glass_quality.py
rtk proxy env CUBEY_RT_PRIVATE_XVFB=1 xvfb-run -a -s '-screen 0 2048x1200x24' python3 -B projects/raytracing/tools/measure_dragon_window.py --output outputs/raytracing/q0-native-reproduction
rtk proxy ctest --test-dir build/dev -R '^(raytracing_|documentation_)' --output-on-failure -j1
```

Use a new output directory. Capture receipts allow a resume only after matching
binary/preset/model/recipe identities and artifact hashes. The ordinary
production qualifier remains strict. Full-resolution queries still obey their
32 MiB readback cap.

Full artifacts and frozen baseline/instrumented executables are retained under
`outputs/raytracing/dragon-glass-q0-20261108/`. The failed initial full-HD ladder
and the short-hold/native frame-index trial are preserved alongside the final
observations. No commit, push or public-site update is part of this pass.

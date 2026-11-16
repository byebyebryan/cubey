# Dragon glass quality research, 2026-11-08

See the [proposed G3 plan](../../notes/raytracing-dragon-glass-quality-plan.md).
Baseline is local commit `10328ef2dff12bd26b6ff20a589c47dde5e8eabc`.
This is research evidence, not a quality/performance pass for a new renderer.

- `normal-audit.json`: fresh float64 analysis of the accepted serialized float32
  GLB, with normalized stored corner normals. Includes angles, opposed corners
  and an unoccluded triangle-centroid direction probe at the default camera.
  The probe includes occluded faces and entering IOR 1.5 only; its counts do not
  measure visible rendering failures or validate exits/TIR.
- `historical-window-analysis.json`: reanalysis of the earlier native exercise,
  separating reset/accumulating/settled glass frames. Source metrics are hashed.
  Views, IOR and extents vary; reset includes non-motion edits. There are no
  per-frame CPU/input timestamps and the old memory values predate the fix.
  These are not fresh steady-state frame-rate or latency measurements.
- `analyze_baseline.py`: read-only, asset-specific reproduction using pinned
  NumPy. It refuses another model hash, assumes the reviewed GLB accessor layout
  and identifies patch faces from that asset's known source-vertex prefix.
- `sources.json`: primary-source URLs, reviewed scope, mutable-document pins
  and adoption limits. No external implementation has been imported.
- `validation.json`: documentation checks and current renderer/binary/model
  preservation. No GPU captures, implementation, commit, push or public-site
  update are part of this pass.

Reproduce from the RT worktree with the already prepared accepted model and the
retained historical metrics under the ignored output root:

```sh
rtk proxy uv run --no-project --no-build docs/evidence/raytracing-dragon-glass-quality-research-2026-11/analyze_baseline.py --output outputs/raytracing/dragon-glass-quality-reproduction
rtk proxy ctest --test-dir build/dev -R '^documentation_' --output-on-failure -j1
```

Full research outputs and the initial source/binary hash manifest remain under
`outputs/raytracing/dragon-glass-quality-research-20261108/`. Model and historical
native metrics are not duplicated into this compact evidence directory.

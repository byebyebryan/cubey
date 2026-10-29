# SDF query acceleration loop, 2026-10-29

The [result](../../notes/sdf-query-acceleration-results.md) records the decision;
the [progress receipt](../../notes/sdf-query-acceleration-progress.md) retains
failed diagnostic assumptions and qualification boundaries. The
[research source ledger](../sdf-query-acceleration-research-2026-10/sources.json)
owns literature provenance and access limits.

- [Summary and gates](summary.json), [144 matched runs](timing.json),
  [GPU samples](gpu-samples.json), [native resource observations](resources.json).
- [Current and candidate workloads](workload.json), [CPU pruning pilot](pruning-pilot.json).
- [Numerical gates](numerical.json), [controlled loop checks](controlled.json),
  [thin first-hit fixtures](thin-contracts.json), [retained cone fixtures](cone-contracts.json).
- [Full-HD captures](captures.json), [shadow ablation](shadow-review.json),
  [history/publication](publication.json), [actual CLI rejections](negative.json).
- [Validation-clean compiler statistics](compiler.json), [preservation](preservation.json),
  [focused checks and identities](checks.json).

Raw host pack: `outputs/sdf/query-acceleration-loop-20261029/`. Native images,
sampled workload payloads, complete profiles, logs and rejected diagnostics stay
there. The tracked archive retains exact inputs, hashes, sampled GPU values and
compact receipts. It does not claim that raw native images are embedded here.

The measured source checkpoint is `603f073a`, executable SHA-256
`865d909326f73267f79d7ebdd4f68cb6409d61ffb8f2be66f5639d608028a68d`.
Later closeout edits affect documentation/archive helpers and the compile-only
statistics tool; they do not change that executable. Historical diagnostic
receipts may name earlier binaries. Final timing requires current-binary capture
completion, passing numerical receipts whose kernels still match, and the
controlled first-hit/endpoint/budget checks.

All 288 timing boundaries record external/shared encoder activity, 10–50%.
There is no idle-GPU or windowed root-far qualification in this archive. GPU
queries stay serial; do not overlap owned builds, tests, captures or other GPU
jobs with timing. Do not stop unowned workloads.

## Reproduction

Build the SDF app, guarded-contract tool, pruning pilot and optional compiler
statistics tool in the release configuration. Image capture comparisons use
ImageMagick `magick` to decode exact 8-bit channels; the numerical tools use the
Python standard library. The existing independent 155-fixture input remains
`projects/sdf/comparison/fixtures.json`.

Use a **new output directory for every execution phase**:

```sh
python3 projects/sdf/tools/query_acceleration_loop.py contracts --query-stepping adaptive-0.5 --supplemental docs/evidence/sdf-query-acceleration-loop-2026-10/cone-contracts.json --output outputs/sdf/query-repeat-contracts-adaptive
build/release/projects/sdf/cubey_project_sdf_guarded_contract build/release/projects/sdf/shaders outputs/sdf/query-repeat-controlled.json
python3 projects/sdf/tools/query_acceleration_loop.py capture --output outputs/sdf/query-repeat-captures
python3 projects/sdf/tools/query_acceleration_loop.py workload --query-stepping adaptive-0.5 --output outputs/sdf/query-repeat-workload-adaptive
python3 projects/sdf/tools/query_acceleration_loop.py publication --query-stepping adaptive-0.5 --output outputs/sdf/query-repeat-history-adaptive
python3 projects/sdf/tools/query_acceleration_loop.py shadow-review --qualification outputs/sdf/query-repeat-captures --output outputs/sdf/query-repeat-shadow-review
build/release/projects/sdf/cubey_project_sdf_query_pruning_pilot projects/sdf/scenes/sculpture-guardian-limestone.json outputs/sdf/query-repeat-pruning.json
```

Repeat the numerical phase for `relaxed-1.2` and `relaxed-1.5`. The timing phase
accepts the three completed `contracts.json` receipts through `--numerical`,
the controlled receipt through `--controlled`, and completed captures through
`--qualification`. It runs all four stepping controls, both A and A+B, all six
views and three interleaved 180-frame repetitions. Workload/publication phases
can also be repeated with each stepping control; `ordinary` is the default.

The compile-only utility accepts the eight native fragment filenames with
`@single`. Its layout includes prepared binding 19. Exclude the earlier
`compiler-invalid-layout/` result. The driver local-memory statistic remains
unusable and must not be reinterpreted from its low bits.

`assemble_query_acceleration_results.py --raw ... --output ...` rebuilds this
archive from a completed pack with the recorded phase names. It checks every
GPU frame index/p95, numerical kernel identity, undo/redo/failure image hashes,
the retained shader/content hashes and canonical CPU/shared-engine preservation.
It does not launch benchmarks or manufacture a pass from incomplete phases.

## HTML report delivery

[Adaptive stepping results](http://starship:8787/reports/baf8361bd5840455f922c302cf52775abf6f7d414c1e9903f663c1274d4647dd/)
bundles interactive timing controls, native image comparisons, pixel magnification,
the shadow ablation and downloadable evidence. [Delivery receipt](report-delivery.json)
records verified HTTP byte parity for all 36 files and 21 offline control cases,
including the HTTP copy/select fallback. No browser was connected during this
delivery, so browser rendering/layout acceptance is not claimed.

Rebuild a fresh explicit export with `query_acceleration_report.py --output ...`;
it reads this archive and the retained native PNGs, checks source image hashes,
and copies only the intended report assets. Publish that directory through the
shared report publisher under project `cubey`. Its own delivery receipt is
excluded from the bundle to avoid a recursive publication identity.

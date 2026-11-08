# Solid-glass dragon, 2026-11-08

Read [the result and limitations](../../notes/raytracing-dragon-glass-first-pass.md)
and [native comparisons](http://starship:8787/reports/471822d5ea971b044267052227c6c2802111dcd88ecfa93b0ed9f41d558a69c3/).

- `qualification.json`: certified GLB binding; depth controls; hidden / IOR 1 /
  neutral / tinted / grazing / thin-feature checks; independent base evaluation;
  exact effective-view roundtrip; original and tampered model rejections.
- `captures.json`: final native full-HD captures, finite HDR and accumulated
  transport status. Flags cover all 256 completed samples per pixel.
- `filtering.json`: higher-resolution/downsample observations, with sampling and
  edge-jump limitations explicitly retained. Relief-off was identical here.
- `native.json`, `native-summary.json`: owned Xvfb material/edit/reload/exposure,
  orbit/dolly camera-exclusion and resize acceptance. Three AS builds retained.
- `opaque-regression.json`: the existing original-mesh qualification, including
  all ten opaque materials and saved effective recipe/view roundtrip.
- `raytracing-ctest.log`: all 18 scoped CTests passed on this implementation.
- `source-sha256.json`: source identity at delivery.
- `delivery.json`: verified internal HTTP snapshot and browser acceptance.

The optical certificate and offline preparer are project source. A second full
preparation reproduced the accepted GLB/certificate exactly. Eight independent
topology/predicate controls from the G0 study were run against the production
preparer's functions and passed. The protected original worktree and existing
recipe/preset/model hashes were rechecked; preservation details and backups
remain in the ignored output root.

Full evidence, including failed numerical experiments and the double-precision
ray diagnosis, is under
`outputs/raytracing/dragon-glass-first-pass-20261108/`. Diagnostic shaders and the
temporary enlarged readback cap used in that diagnosis were removed. The final
renderer uses no any-hit primitive filtering or medium-state recovery.

This delivery covers G1 plus essential G2 controls. It does not establish G3
motion/high-SPP convergence or isolated footprint accuracy. No commit, push or
public-site update was performed.

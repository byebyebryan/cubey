# SDF source-query optimization evidence

Date: 2026-10-29. Baseline `0737bec9`; measured implementation `b942d6bb`.
See the [result and decisions](../../notes/sdf-source-query-optimization-results.md)
and [execution progress](../../notes/sdf-source-query-optimization-progress.md).
These are study receipts and native captures, not refreshed image goldens.

- [Summary and promotion gates](summary.json): final 1080p ranges, setup/memory
  observations and preservation checks.
- [Four-way matched runs](four-way-timing.json), independent
  [shadow](shadow-only-timing.json) and [prepared-cone](prepared-only-timing.json)
  trials; [raw GPU samples](gpu-samples.json) retain every measured frame index.
- [Encoder observations](encoder-observations.json): A/B independent runs were
  idle at boundaries; the final four-way batch had 8–50% shared encoder activity.
  Final ranges are not an isolated/idle-GPU claim.
- Final [automatic](automatic-qualification.json),
  [prepared](prepared-qualification.json) and [combined](combined-qualification.json)
  native qualification, including completed action-frame captures.
- [Prepared](prepared-frozen-results.json) and
  [combined](combined-frozen-results.json) frozen query results identify the
  actual compute shader and its SHA-256.
- [Primitive numerical probes](primitive-numerics.json),
  [supplemental analytic contracts](supplemental-contracts.json), their
  [four-mode results and admission rejections](supplemental-qualification.json),
  and [controlled shadow exhaustion](shadow-exhaustion.json).
- [Actual authored-history prefixes](authored-history.json),
  [full Guardian publication](guardian-publication.json),
  [cone lighting](cone-lighting.json) and [native window](native-window.json).
- Earlier [shadow appearance](earlier-shadow-appearance.json) and
  [lighting/camera edges](earlier-shadow-edge-cases.json), on unchanged shadow
  SPIR-V, supplement the final-binary gate.
- [Focused checks](focused-checks.json) and [CTest output](focused-checks.log).
- [Baseline identities](baseline.json) and [content-addressed archive](manifest.json).

The six full-HD Guardian images retain both the original and combined outputs:
[preset reference](images/guardian-preset-reference.png) /
[combined](images/guardian-preset-combined.png),
[side reference](images/guardian-side-reference.png) /
[combined](images/guardian-side-combined.png), and
[rear reference](images/guardian-rear-reference.png) /
[combined](images/guardian-rear-combined.png).
The [native source window](images/native-source-window.png) uses ordinary A16
source tracing, not windowed root-far.

## Reproduction

Build the release SDF app plus `cubey_project_sdf_shadow_contract` and
`cubey_project_sdf_prepared_cone_probe`. Run GPU jobs serially on the recorded
device/driver; do not overlap owned builds, tests or captures with timings.
Each output directory below must be fresh. Native tools require Vulkan
validation; the window harness requires an owned private Xvfb display.

```sh
python3 projects/sdf/tools/qualify_shader_selection.py qualify --output outputs/sdf/query-repeat-auto
python3 projects/sdf/tools/qualify_source_query.py cones --output outputs/sdf/query-repeat-cones
python3 projects/sdf/tools/qualify_source_query.py combined --output outputs/sdf/query-repeat-combined
python3 projects/sdf/tools/qualify_source_query.py contracts --output outputs/sdf/query-repeat-contracts
python3 projects/sdf/tools/qualify_source_query.py history --output outputs/sdf/query-repeat-history
python3 projects/sdf/tools/qualify_source_query.py publication --output outputs/sdf/query-repeat-publication
python3 projects/sdf/tools/qualify_source_query.py lighting --output outputs/sdf/query-repeat-lighting
python3 projects/sdf/tools/qualify_source_query.py timing --modes reference shadow prepared-cones combined --qualification outputs/sdf/query-repeat-combined --output outputs/sdf/query-repeat-timing
```

The receipt commands retain the full native argv for individual runs. The window
receipt records the inspected lifecycle command; copy its plain-sphere scene
beside an owned output config and use automatic optimization, atmosphere,
analytic/pruned A16 with `--source-contract --material-scroll 1`. The source
scene copy must be owned because the harness temporarily writes an invalid
document and restores it to test rejected reload/retry.

The original raw pack, complete CSVs, input action manifests, PNGs, logs and
rejected attempts remain under `outputs/sdf/source-query-optimization-20261029/`.
The archive is assembled by
`projects/sdf/tools/assemble_source_query_results.py`, which verifies sample
coverage/p95s, numerical limits, preserved bytes and the proposed performance
gates before writing receipts and hashes.

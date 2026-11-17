# Dragon glass noise-control evidence

[Interactive raw, clamp and OIDN comparison](http://starship:8787/reports/a74e3a575917d5015441ba6d25b6e15248a1913b63a333834237e5008dcbcf3d/).

2026-11-16 bounded implementation and matched appearance trial. The
[result note](../../notes/raytracing-dragon-glass-noise-controls.md) explains
the recommendation and bias. The prior
[solution research](../raytracing-dragon-glass-solution-research-2026-11/README.md)
records the production references and TIR path diagnosis.

- `analysis.json`: 16 full-HD renderer captures, 16 offline OIDN results,
  independent-seed comparisons, raw parity and full capture receipts.
- `validation.json`: exact final executable/shader/source identities, 20 RT
  CTests, 16 optical controls, nine documentation checks, native saved values
  and positive-cap saved-view HDR roundtrip.
- `analyze.py`, `validate.py`: reproduce matched statistics and checks.
- `build_report.py`, `report-template.html`: export only the intended report.
- `report-delivery.json`, `browser-qa.json`: immutable report receipt and
  browser interaction/visual acceptance, when delivered.

The ignored output root is
`outputs/raytracing/dragon-glass-noise-controls-20261116/`.
`original-source/` and `original-build/` retain the starting recovery point.
The first exploratory captures remain in the root; **`final/` owns the final
report's captures, denoising logs, regression logs and native checks**.
The original official OIDN archive, private package and checked provenance
are retained in the root. No global OIDN install is required.

Reproduction, from the RT worktree:

```sh
rtk proxy uv run projects/raytracing/tools/compare_dragon_noise.py --stage capture --output outputs/raytracing/dragon-glass-noise-controls-20261116/final --reference-manifest docs/evidence/raytracing-dragon-glass-speckle-2026-11/captures.json
rtk proxy uv run projects/raytracing/tools/compare_dragon_noise.py --stage denoise --output outputs/raytracing/dragon-glass-noise-controls-20261116/final --oidn outputs/raytracing/dragon-glass-noise-controls-20261116/tools/oidn-2.5.1.x86_64.linux/bin/oidnDenoise
rtk proxy uv run docs/evidence/raytracing-dragon-glass-noise-controls-2026-11/analyze.py
```

The capture/denoise stages verify existing receipts before reuse and refuse
identity drift. Use a fresh output directory for another executable. The
asset-bound optical qualifier also requires a fresh output directory.
`validate.py --regressions` runs final-build checks; `validate.py` collects after
the native exercise. Native screenshots and XTest driver remain under the
ignored output root. The driver runs only in its owned Xvfb display.

The comparison uses 128/512 samples, seeds 19/43 and caps 0/100/1,000/10,000.
OIDN is RT, HDR color-only, high quality, CUDA, automatic scale. All variants
use calibrated ACES/sRGB output at exposure zero. Main report crops are scene,
head, body and marble base. Seed differences and mean HDR retention are not
accuracy scores. These are still images; no native temporal denoising,
physical-display latency, commit, push or public-site update is claimed.

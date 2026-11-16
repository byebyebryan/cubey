# Dragon glass Q1 interface experiment evidence

The [results](../../notes/raytracing-dragon-glass-q1-results.md) reject v1 whole
interface fallback at the analytical/GPU control stage. `arithmetic_passed`
does not mean `candidate_promotion_passed`: the former is true, the latter false.

- `analysis.json`: independent calibration, 65,536 GPU interface observations,
  finite/side guards, critical-angle exceptions, seam witnesses, reverse paths,
  GPU/CPU flux integrals and CPU sphere controls.
- `interface-probe.json`, `flux-probe.json`: exact commands, private binary,
  source, shader and raw readback identities. The private builds substitute the
  dielectric probe entry point and are not ordinary renderer builds.
- `preservation.json`: qualified baseline/runtime/model/material identities and
  preservation of existing work. `validation.json` records scoped checks.
- `report-delivery.json`: internal report storage, HTTP-byte and browser checks.
  This is separate from the public project site.
- `render_report.py`, `report-template.html`: dedicated report bundle generator
  and presentation. Analytic illustrations are labeled separately from GPU
  observations. They are not dragon captures.

Raw data and complete snapshots are retained under the ignored
`outputs/raytracing/dragon-glass-q1-20261114/` root. `baseline-source/` and
`baseline-build/` preserve the pre-implementation Q0.1 state. `probe-02/` and
`flux-probe-03/` contain complete private source/executable/shader sets and
16 MiB bounded readbacks. The 4,194,304-cell flux control evaluates arithmetic,
not dragon ray queries. The failed first shader compile in `probe-01/`, coarse
initial quadrature and an overstrict initial bit-identity analysis are retained
as exploratory records and excluded from the accepted numerical result.

Reproduce with the existing configured toolchain/foundation libraries:

```sh
rtk proxy python3 -B projects/raytracing/tools/run_optical_normal_probe.py --fixture interface --output outputs/raytracing/q1-interface-new
rtk proxy python3 -B projects/raytracing/tools/run_optical_normal_probe.py --fixture flux --output outputs/raytracing/q1-flux-new
rtk proxy uv run --no-project --no-build projects/raytracing/tools/analyze_optical_normal.py --interface outputs/raytracing/q1-interface-new --flux outputs/raytracing/q1-flux-new --output outputs/raytracing/q1-analysis-new
```

The analyzer's zero exit status means the recorded experiment was analyzed and
its arithmetic controls passed. Read its explicit rejection decision. It does
not certify a new renderer feature or run a full-dragon qualification.

Primary-source scope is retained in the
[planning research record](../raytracing-dragon-glass-q1-research-2026-11/README.md).
No external normal-adjustment implementation was imported.

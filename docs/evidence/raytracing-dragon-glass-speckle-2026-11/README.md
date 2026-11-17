# Solar speckle investigation

[Interactive comparison report](http://starship:8787/reports/f708b465ab998d9f25ef9e0090b34d00b95e514d163823badc044e604d38e891/)
includes lighting/seed/sample controls, the broader-sun preview and sample-tail
figures. HTTP entrypoint bytes and desktop-browser controls are verified;
receipts are in `report-delivery.json` and `browser-qa.json`.

2026-11-16. The ordinary renderer is pinned to smooth-default commit `2e86702`.
Raw work lives under `outputs/raytracing/dragon-glass-speckle-20261116/`.
The combined seed-19 4096-SPP capture is reused from the immediately preceding
default-integration validation, with the same executable and shader set.

- `captures.json`: twelve ordinary render receipts, SHA256 input identities,
  flags and original output paths. Marble detail is enabled in all controls.
- `broad-sun.json`: four private lighting previews at fourfold shared solar
  angular radius, retaining sharp glass and direct illumination strength.
- `analysis.json`: seed differences, white-pixel counts, selected pixel values,
  private sample-series parity and solar contribution tails.
- `capture_controls.py`, `probe_samples.py`, `preview_broad_sun.py` and
  `analyze_controls.py`: explicit capture/probe preparation and analysis.
- `build_report.py` and `report-template.html`: bundle original PNG bytes and
  standalone matplotlib SVG figures. No render-image editing is performed.

The private sample logger skips unrelated pixels while retaining the logical
full-HD camera and original sample sequence. Its isolated mode runs an entire
4096-sample prefix, unlike the ordinary one-sample replay. It records sample
RGB and solar terminals only; the private source/build manifest is in
`sample-build/build.json`. The raw sample journals remain in ignored outputs.
Their mean RGB must match ordinary HDR within a recorded tolerance before
interpretation; the observed maximum relative error is `5.90e-6`.

All sixteen captures and three probes pass finite/nonnegative checks and
Vulkan synchronization validation. Some high-sample smooth captures retain
one/two cumulative event-budget pixels. The selected pixels have zero flags;
no medium/query/shadow/stack errors occur. This is an appearance/noise diagnosis,
not strict all-view optical qualification. The two seeds and illustrative
selected pixels do not define population variance or ground truth.

The broader-sun build changes one line of a private environment snapshot. Its
fourfold radius is used by shared atmosphere and traced solar emitter together.
It is a different light and is not promoted to ordinary source or presets.
The proposed solar-only appearance trial is not implemented. No commit, push
or public-site update is included in this research stage.

See [the investigation and next-pass proposal](../../notes/raytracing-dragon-glass-speckle.md).

# Guardian current-candidate frame profile evidence

Date: 2026-10-28. See the [findings](../../notes/sdf-guardian-frame-profile-results.md).

- [summary.json](summary.json): grouped timing ranges, usable compiler statistics
  and native capture qualification.
- [timing.json](timing.json): 51 accepted runs / 7,650 completed GPU samples,
  commands, shader hashes and per-run GPU/encoder snapshots.
- [compiler.json](compiler.json): raw driver pipeline executable statistics.
  Local-memory-size values are unusable and explicitly excluded; register and
  binary-size values are compile-time evidence, not live counters.
- [qualification.json](qualification.json): 155 independent checks, five invalid
  configuration rejections and eighteen byte-identical full-shading captures.
- [context.json](context.json), [baseline.json](baseline.json): source/binary/shader
  identity and preservation of 78 existing shader binaries / 138 content files.
- `images/`: ordinary and specialized images at each of three tested views.
- [rejected-runtime-setup.json](rejected-runtime-setup.json): excluded initial
  host logging failure, corrected before accepted measurements.
- [checks.json](checks.json): seven SDF CPU checks, three documentation checks
  and the shader build-rule check pass.
- [report-delivery.json](report-delivery.json): immutable HTML URL, HTTP file
  verification and desktop/mobile/control acceptance.

Raw profiler CSVs, pilot/capture receipts and compiler outputs remain local in
`outputs/sdf/guardian-frame-profile-20261028/`.

## Reproduction

Build Release targets `cubey_project_sdf` and `cubey_project_sdf_shader_stats`.
The compiler tool requires the profiling extension only on its own separate
device. It makes no submission or live occupancy measurement. Use fresh output
directories and no concurrent owned CPU/GPU work during timings:

```sh
python3 projects/sdf/tools/profile_guardian_frame.py capture --width 1920 --height 1080 --output CAPTURE
python3 projects/sdf/tools/profile_guardian_frame.py compiler --output COMPILER
python3 projects/sdf/tools/profile_guardian_frame.py timing --output TIMING
```

The native private selector is `--frame-profile`, with `runtime` or `fixed`
paired with `--comparison-stage`; `nodebug`, `single-material`, `specialized`
and `no-sky` are separate ablations. The sampled default and ordinary source
routes remain unchanged. Selection guards deliberately limit this diagnostic;
it is not a general editor or production pipeline selector.

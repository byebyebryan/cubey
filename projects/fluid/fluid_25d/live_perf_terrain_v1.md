# Live performance and terrain options V1

Approved study: three cached Terrain Diffusion sites (mountain/alpine/rolling),
five power-of-two configurations each, plus the historical 512x512/30m control.
No native kernel edits, solver port, CUDA interop, source-terrain modifications,
per-site forcing tuning, or automatic numerical/visual default promotion.

Configurations: same 16.384km square at 512/32m, 1024/16m and 2048/8m; nested
8.192km and 32.768km squares at 512/16m and 2048/16m. Larger footprints may shift
near source borders, but stay nested and retain the original mountain patch.
Sampling uses the shared LOD0 bilinear terrain surface; source-native 30m detail
is not invented by interpolation. Climate and render terrain bind to the same
world coordinates in recorded and external views.

All start dry, use uniform continuous 512mm/h, Manning n=0.05, no infiltration,
evaporation, point source, prefill or drain, and the existing native fall/open
boundary policy. The primary horizon is 7200 physical seconds, requested pace
60x. Current accepted Scenic Macro, B-spline 2x, noise off, rough water, subtle
foam/flecks, visible rain and trails are frozen at 1080p initially.

Coverage: bounded worker-only and live combined trials for all 15 configurations;
three paired repetitions of worker-only, recorded renderer, identical recorded
renderer plus worker, and live renderer for reference/demanding viable cases.
Later: selected 1440p/4K output, 1024mm/h stress and 600-wall-second live soaks.
Control transitions are outside primary timing windows. Source/binary/shader/
extension/runtime identities and actual viewport/presentation mode are retained.

Targets: frame-loop p95 <=16.7ms, publication-to-command-record p95 <=1s,
warm ack p95 <=250ms, sustained physical/wall pace >=54x for the 60x request,
no growing lag, bounded working storage/memory and clean owned shutdown.
Record CPU/GPU spans, positive-native-step distributions, publication freshness,
advancing-state age, fleck classification/source age, geometry and resources.
Worker-only includes transport publication; no pure-kernel or scanout claim.

Stop rules: 600-wall-second cap per two-hour trial, 8-hour total campaign cap;
bounded snapshots/profiles, explicit failures/incomplete horizons, no silent
fallback. Profiled private-X11 GUI evidence is not physical-monitor acceptance.
At most two small measured optimization experiments, keeping original baselines.
Reports and generated cases/media stay under outputs; source/tests/docs are
reviewed checkpoint commits, with no automatic push.

## Reproduce and launch

The study artifacts are generated under `outputs/fluid`; they are not checked-in
terrain assets or replacements for existing defaults. Preparation requires the
already-installed optional SynxFlow IO/NumPy environment. No command installs or
rebuilds the native extension. Use a fresh output directory for each campaign.

The named launcher itself uses ordinary Python and validates the prepared case
before starting either process. For the current study:

```sh
rtk proxy python3 projects/fluid/fluid_25d/run_terrain_rain_demo.py live \
  --study outputs/fluid/live-perf-terrain-20261010-Dd6K3h \
  --site alpine --configuration patch-1024-16m \
  --app build/release/projects/fluid/fluid_25d/fluid_25d --print-command
```

Remove `--print-command` only when a GUI display is available. Live starts dry
and paused; Space resumes the actual worker. Escape closes/detaches the viewer;
the foreground launcher still owns the worker. Ctrl-C stops its owned processes.
These study fixtures have a 72000-physical-second solver horizon; they are not
unbounded production runs. Their primary measurements stop at 7200 physical
seconds or the explicit wall cap, without imposing that two-hour limit on the
named interactive launch.
`--app` selects an existing renderer artifact explicitly, without changing the
project's default binary or silently building/falling back. Sites are `mountain`,
`alpine`, and `rolling`; configurations are the five named sizes above.

`replay` requires an explicit `--recording`; it is never substituted for live.
The campaign's `static-recording/recording.json` files contain just two exact
captured states, not a smooth recorded timeline. The launcher holds the final
state for static presentation/isolation controls.

The default named launch keeps B-spline 2x display tessellation. The optional
`--surface-subdivision 1` retains B-spline reconstruction and changes only the
existing display tessellation, not the native simulation grid, terrain or fields.
It is a labelled performance experiment, not an automatic promotion.
`--camera runoff` and `--camera collection` select existing scene camera presets;
neither changes the prepared crop or solver state.

## Reading the measurements

- Requested 60x pace is a cap, not a measurement of the solver's maximum capacity.
- Private-X11 frame-loop time includes host work, submission and presentation.
  GPU render-pass timing is a separate measurement; neither proves monitor FPS.
  The host frame-ending span also includes GPU-owner dispatch: its components
  are not separately timed, so its delay cannot be attributed to X11 alone.
  CPU-labelled spans measure elapsed wall time, including waits, not CPU cycles.
- Worker-only includes download/conversion, hashing, filesystem publication and
  pacing. Callback interval statistics are not isolated CUDA kernel timing.
- Worker publication age is not displayed-field age. Whitewater source age also
  includes its asynchronous classification pipeline; it is not the same clock.
- Warm acknowledgement tests exercise the worker control API, outside primary
  timing windows. They do not measure a human GUI click-to-feedback round trip.
- Static renderer-only and renderer-plus-worker controls use the identical held
  water field. The latter adds computation/publication contention, not just the
  bridge. Report repeated p95 values as ranges, not confidence intervals.
- 8 m target cells interpolate the same source-native 30 m terrain. Smaller or
  larger footprints change the catchment and boundary context, not just cost.
- Milestone PNG captures remain inside the instrumented live timing window;
  final captures, videos and warm command probes are outside it. This study does
  not lock GPU clocks or isolate the desktop, and must not imply it does.

The report is served by the existing reporting server at
`http://starship:8801/fluid/live-perf-terrain-20261010-Dd6K3h/` with a link back to
the reporting index. Trial `result.json`, phase `protocol.json`, binary captures,
sampled CSV profiles and worker completion receipts remain separate evidence.
Human visual acceptance and physical-monitor performance are deferred.

## Measured findings, 2026-10-10

On Starship (Ryzen 9 7900X / RTX 5070 Ti), the broad Debug matrix retained all
30 worker/live trials: 25 reached two physical hours and five hit the 600-second
wall cap. No broad-coverage solver/viewer errors or input mutations occurred.
Nine of fifteen live cases reached the >=54x pace target. None met the complete
16.7ms frame-loop target; successful solver progress is not 60FPS acceptance.

The build-only experiment is the useful optimization from this pass. These are
ranges across three Release live repetitions, not pooled frames:

| Same mountain patch | Physical/wall pace | Frame p95 | GPU pass p95 | Field age p95 | Whitewater source age p95 |
| --- | --- | --- | --- | --- | --- |
| 1024² / 16m | 59.87–59.88x | 53.0–53.8ms | 6.03–6.26ms | 0.154–0.156s | 0.197–0.206s |
| 2048² / 8m | 7.94–8.13x | 88.7–90.6ms | 18.51–19.79ms | 0.287–0.289s | 0.429–0.433s |

Release reduced 1k whitewater classification p95 from about 590ms to 32ms, and
2k classification from multiple seconds to about 105ms. The native extension,
numerical inputs, shaders and material settings did not change. Release still
does not solve the frame-loop budget or the 2k/8m accelerated-simulation pace.

The matched held-state 2k controls measured about +23% GPU-pass p95 and +21%
frame-loop p95 with a computing/publishing worker present. The 1k controls did
not show a consistent slowdown. Neither is a universal bridge-overhead figure;
the contention worker starts dry while the rendered field remains held.

The two 600-wall-second Release 1k soaks advanced about ten physical hours at
60x. Mountain preserves pronounced runoff channels; rolling develops a larger
connected pool. The 1024mm/h stress and 1440p/4K trials completed, but larger
output sizes increased frame p95 to 88ms/180ms while GPU passes were 6.7ms/7.7ms.
The host frame-ending scope needs finer attribution before assigning a cause.

Practical starting choice: explicitly launch the Release artifact at
`patch-1024-16m`, retaining B-spline 2x. Mountain is the runoff reference, alpine
an alternative sharp landform, and rolling the broader pooling example. The
2k/8m case is a finer water-grid inspection option at a much slower measured
physical pace; `large-2048-16m` is a larger catchment, not a free resolution gain.
These are recommendations for opt-in launches, not changes to existing defaults.

The display-only experiment is mixed: held 2k overview GPU p95 was 12.38–12.40ms
at 2x versus 13.75–13.80ms at 1x (three repetitions each). A single 1x live trial
instead measured 11.07ms GPU p95 / 79.0ms frame p95 / 8.74x physical pace, compared
with the 2x live ranges above. Its field and whitewater source ages remained
about 0.29s / 0.43s. The same-field overview and closer captures look very similar
in automated visual review, but this does not establish a universal speedup or
human quality acceptance. Retain the optional density control; do not promote it.

Soak early/late windows exclude the first startup minute. Worker RSS medians were
stable near 620–622MiB; viewer medians changed -4.6MiB (mountain) and +1.6MiB
(rolling). Displayed-field and whitewater source ages did not grow between those
windows. This is bounded-window evidence, not a leak-free/unlimited-runtime
claim. Session storage ended near 166MB with the existing three-slot publication
policy; filesystem growth was not continuously sampled.

Next: profile owner-thread dispatch, submit/wait and presentation separately,
then compare the same held/live case on the actual desktop path and private X11.
Keep native timestep/publication costs separate from renderer costs. A solver
port, CUDA interop or numerical-method rewrite is not justified by this study.

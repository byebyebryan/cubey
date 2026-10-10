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

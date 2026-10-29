# SDF optimization opportunities after shader selection

Date: 2026-10-29 UTC. Baseline: 651b9074. Private diagnostic work through
3bb17e9d, local sdf-tracing-comparison. No production adoption or push.

## Main result

The strongest concrete next trial is skipping shadow traces whose direct-light
contribution is zero, using the final bump-adjusted normal. The private opaque
one-material probe has byte-identical 1080p captures in all three Guardian views.
It reduces rear GPU p95 from about 20.9 ms to **17.09–17.17 ms**. It still misses
16.7 ms, and broader material/history/optical qualification remains necessary.

The proposed [execution plan](sdf-source-query-optimization-plan.md) bounds the
next loop to that qualification and an independent prepared-cone trial. It
defines reference controls, numerical/publication gates and stop conditions;
planning does not adopt either optimization.

## Controlled shadow experiment

Ranges of three per-run whole-scene GPU p95s in ms, 1920×1080, supersample 1,
analytic/pruned A64 root-far, unchanged scene and cameras, RTX 5070 Ti / 610.57.04.

| View | Current selected | Reorder only, always trace | Final-normal zero-light culling |
| --- | ---: | ---: | ---: |
| preset | 21.39–21.56 | 21.01–21.25 | 19.27–19.61 |
| side | 22.93–23.04 | 22.74–22.94 | 21.61–21.77 |
| rear | 20.80–20.97 | 20.67–20.69 | 17.09–17.17 |

The original shader traces before computing the material's bumped normal and
direct-light cosine. The culling probe moves that work earlier and traces only
when the final cosine is positive. Shadow origins retain the geometric normal.
The order-only control makes the same move but always traces. Both private
controls use 80 registers / about 202 KB executable, versus 96 / 257 KB for the
current path. Ordering alone gives a small gain; most of the measured culling
benefit remains against that control. This is not exact hardware-stall attribution.

Current, fixed-full, reorder-only and culling captures have the same PNG hash
per view. The scope is static, one material, shortcut enabled, opaque atmosphere,
debug zero, no AO/ground/clouds. A production trial must cover appearance changes,
undo/retry, bump toggles, light motion, clay, ground, multiple materials and glass.
It must settle whether exhaustion of a zero-contribution shadow should remain
observable as a lit magenta diagnostic; the tested scenes have no such exhaustion.

## Remaining cost

| Control | Preset | Side | Rear |
| --- | ---: | ---: | ---: |
| Current selected | 21.47–21.66 | 22.90–22.98 | 20.85–20.92 |
| Camera tracing only | 14.82–14.85 | 20.19–20.60 | 15.64–15.71 |
| Camera plus normal | 15.13–15.28 | 20.37–20.55 | 15.95–16.11 |
| Camera, normal and shadow | 20.30–20.50 | 22.39–22.55 | 20.31–20.42 |
| Shadows disabled | 15.97–16.04 | 20.87–20.96 | 16.47–16.59 |
| Compiled no-background | 21.32–21.63 | 22.88–23.03 | 20.49–20.59 |
| Pattern and bump disabled | 21.36–21.81 | 22.89–23.03 | 20.82–20.95 |

These are whole-program ablations, not additive stage timers. The observable
material-only diagnostic is slower than full shading; some normal/primary
ranges overlap or invert. Compiler context and observable outputs differ, so
subtracting rows into a component-cost pie chart would be misleading.

The side camera-only diagnostic already costs 20.19–20.60 ms. Turning shadows
off there still costs 20.87–20.96 ms: primary queries need improvement. Sky and
executed pattern/bump removal offer little useful headroom in the selected path.
The older 51 ms shader's sky ablation does not predict the same gain here.

Actual single-material specialization and the fixed-full proxy both report
96 registers / 256,896 executable bytes. No-sky stays at 96 registers. The driver
local-memory statistic remains invalid and excluded. Live occupancy, divergence
and stalls are unmeasured; Nsight/RenderDoc tools are not installed.
[NVIDIA profiler reference](https://docs.nvidia.com/nsight-graphics/UserGuide/shader-profiler.html).

## Ranked trials and gates

1. Qualify zero-contribution shadow culling for the eligible opaque lit path.
   It has measured benefit and exact static parity. Keep the final bump-normal
   test, geometric shadow origins and original/general/debug reference paths.
2. Precompute rounded-cone invariants in a disposable geometry-derived GPU cache.
   Guardian has 996 rounded cones; each query recomputes shape-only divisions,
   slope and radial square root. Measure extra loads against saved arithmetic.
   Test cap/body transitions, degeneracy and CPU/GPU rounding without changing
   authored geometry, CSG, source/history or tolerances. No gain is measured yet.
3. Compare guarded fixed/adaptive larger steps in camera and shadow tracing.
   Preserve overlap/backtracking, inside exits, root endpoints, iteration limits
   and exact-hit/normal contracts. Reset learned state across far advances;
   discontinuous certificates are not an exact-field slope input. Measure
   retries and GPU time, not only steps.
   [2023 paper](https://diglib.eg.org/items/56b3a5fa-9c27-4036-a21f-7287dd83af11),
   [pinned author code](https://github.com/Bundas102/auto-relaxed-trace/blob/2e9ca0c9d4e4ba9b4ae375457016d27a289a8965/Shaders/trace.slang).
4. Survey a finer exact near-surface pruning level before changing the renderer.
   Historical hit-program mean/p95 is 78/195 preset, 37/137 side and 25/95 rear.
   A dense 256³ table of 16-byte headers alone is 256 MiB. Forecast active
   references, scratch, peak live allocations, lookup and build/publication
   costs first. Exact hierarchical refinement is supported by the
   [pruning paper](https://wbrbr.org/publications/LipschitzPruning/documents/LipschitzPruning_submitted_to_EG25.pdf);
   a budgeted Cubey hierarchy remains an experiment.
5. Consider an optional lower-resolution motion preview as an image-quality
   tradeoff. Native 720p is 18.39–18.47 ms preset, 15.04–16.78 ms side and
   14.83–14.87 ms rear. Native 540p is 8.16–12.48 ms. Scaling is not proportional
   to pixel count. These are smaller outputs, not qualified 1080p upscaling.
6. Treat startup caching separately. Engine graphics pipeline creation supplies
   no application-managed cache; opaque driver reuse is not a controlled policy.
   Start with creation feedback and persistent cache invalidation/fallback.
   This targets startup, not steady FPS.
   [Cache guide](https://docs.vulkan.org/guide/latest/pipeline_cache.html),
   [creation feedback](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCreationFeedbackCreateInfo.html).

Do not add predicted gains from independent trials. Proposed promotion requires
repeatable GPU improvement beyond run variation, no meaningful worst-view/Robot
regression, unchanged geometry/normal/transport contracts and reported memory,
setup and publication costs. The end target is 16.7 ms in all three full-quality
views. GPU construction targets edit latency; normal-stencil reduction and
larger wavefront/proxy changes are not the first opportunities justified here.

## Evidence and preservation

153 serial runs retain 22,950 completed GPU samples: 90 ablations, 18 resolution,
18 preliminary shadow and 27 controlled shadow runs. Each has 180 frames, 30
warmup and 150 measured samples. No owned build/test/capture overlaps timing.
All 306 encoder boundary observations are 0%; these are shared-host snapshots.

All 108 existing SPIR-V binaries and 138 content files match baseline. Eight
private shaders are additive. Historical 320p workload counters use a hash-matched
kernel, not current 1080p timing. Geometric back-facing rear hits account for 79%
of their shadow CSG visits; that is neither a final bump-normal mask nor a GPU
time share.

No normal-route shader bytes, format, tolerance, representation or default
changed. The first negative-test matcher expected different wording for an
already-rejected mode; admission was not weakened. Robot/window/physical
interaction were not requalified for the private shadow candidate.

See [scope](sdf-optimization-investigation.md), [receipts](../evidence/sdf-optimization-investigation-2026-10/README.md)
and [preceding source selection result](sdf-shader-selection-results.md).


[Interactive HTML report](http://starship:8787/reports/27a4641b1f78f6ee417cbd3ba2d834259f66b91601be1b9588034111196e47f2/):
30 files / 4,771,198 HTTP-verified bytes, desktop/mobile controls checked.
[Focused checks](../evidence/sdf-optimization-investigation-2026-10/checks.json)
pass twelve SDF/documentation/build groups.

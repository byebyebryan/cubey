# SDF source shader selection results

Date: 2026-10-29 UTC (2026-10-28 local). Implementation: `9b610900`;
completed-frame capture: `dcdd86eb`; qualification harness: `375bcc5d`.
Local branch: `sdf-tracing-comparison`. No push or default tracer switch.

## Outcome

The opt-in source renderer now automatically selects a prebuilt lit shader.
Guardian rear 1080p whole-scene GPU p95 falls from **51.29–51.34 ms to
20.81–20.92 ms**, with byte-identical general/automatic captures. The three
Guardian views remain above the 16.7 ms target. This adopts the qualified
shader selection within source tracing; the default stays sampled/full.

## What changed

Each source renderer prepares three compatible trace pipelines at startup:
original general/debug, lit/general-material and lit/one-material. The lit
wrapper compiles out debug branches. A core Vulkan specialization constant
makes the material shortcut a compile-time choice, while the general evaluator
remains available. No shared engine API or authored scene/history format changed.

Selection reads the **published registered-material count**, current debug
view and the existing material-fast-path setting. Debug views use the original
shader. Exactly one registered material plus an enabled shortcut selects the
one-material lit shader; all other lit scenes use general materials. An unused
second registered material conservatively disables the shortcut. Material tables,
patterns, domains, bump and optical interface/provenance queries keep their
existing semantics. Geometry CSG still determines hits, normals and transport.

All three pipelines survive for the renderer lifetime. Appearance changes,
undo/redo and debug toggles perform no pipeline creation. Extent or lighting
family changes retain their existing renderer-recreation boundary. Sampled and
retained diagnostic routes create only the original pipeline.
`--source-shaders general` retains the original source shader as an explicit
comparison/fallback control. The retained frame-profile runner now requests it.

## Measurements

RTX 5070 Ti, driver 610.57.04; analytic/pruned A64 root-far, unchanged presets,
1920×1080, supersample 1. Each cell is the range of three per-run GPU p95s in ms.

| Scene / view | Original general | Automatic |
| --- | ---: | ---: |
| Guardian / preset | 53.47–53.87 | 21.43–21.52 |
| Guardian / side | 50.73–51.10 | 22.85–23.05 |
| Guardian / rear | 51.29–51.34 | 20.81–20.92 |
| Robot / preset | 6.33–6.37 | 5.11–5.14 |
| Robot / side | 4.04–4.12 | 3.42–3.44 |
| Robot / rear | 4.12–4.14 | 3.62–3.66 |

There are 36 interleaved serial runs, each with 180 frames, 30 warmup and
150 completed GPU samples: 5,400 measured whole-scene timestamps. No owned
build/test/capture jobs overlapped timing. All 72 boundary NVENC observations
were 0%; boundary samples do not establish exclusive host/GPU use throughout.
These are whole-scene measurements, not additive CSG/normal/material timers.

Pipeline setup is separate from frame time. In these cached timing runs,
automatic pools took 32.54–36.88 ms, compared with 3.68–5.05 ms for general.
An earlier first windowed material-source pool took **32,476 ms**; subsequent
recreations were about 39 ms. Driver-cache state was not controlled, so this is
an observed startup risk, not a reproducible cold-cache benchmark. Pipeline
private memory, occupancy, cache stalls and spills remain unmeasured.

## Qualification

- 141 native jobs on the final binary: all 20 new SPIR-V families, one and
  multiple material surfaces, Guardian and Robot at three 1080p views,
  material-study/furnace/atmosphere, glass and clouded glass, AO/clay/ground/
  raster occlusion/soft shadows, shortcut disabled, and debug views.
- 60 static general/automatic image pairs and 54 completed-frame action pairs
  are byte-identical. Original history replay runs 20 actions in each source
  mode. New 21-action eligibility replay covers unused material registration,
  undo/redo/branching, debug/shortcut changes, failed publication and retry.
  Six further appearance actions per mode change patterns, domains and bump,
  undo and redo; rendered pixels actually change while caches remain reusable.
- The unchanged independent 155-check geometry contract passes through all
  ten source configurations. Tolerances were not relaxed.
- Native Xvfb source rendering passes two resizes, orbit, zoom, pause/reset,
  a roughness edit and clean exit. Recreated pools contain three pipelines;
  source mode allocates no sampled field and the appearance edit rebuilds none.
  Physical desktop and human visual acceptance remain separate.
- All 226 protected baseline files match: 88 original SPIR-V binaries plus
  138 content files. Twenty new lit SPIR-V binaries are additive.

The capture harness initially treated `--frames` as multi-frame PNG capture.
The host actually renders one PNG frame. Those prefix images were rejected;
the action driver now optionally queues a readback after each frame's submission
and before host-slot teardown, including its tail. It rejects multi-frame action
studies in ordinary PNG mode. A first late-tail readback exposed an expired
image; its validation log is retained and the accepted runs use the corrected
queued lifetime. Capture action completion includes optional readback and is
not an edit-latency benchmark. Other rejected setup receipts remain inspectable.

## Decision boundary

Keep automatic selection on already opt-in source paths, with the original
shader control available. Guardian's remaining 21–23 ms needs a fresh profile
of the actual selected shader before choosing finer regional programs,
traversal changes or another split. Shader setup latency needs a separately
bounded cache/creation investigation if this route becomes an interactive
front door. The prior region-builder/edit latency and native A64/root-far
boundary differences remain; this loop does not resolve them, build on the GPU,
add an editor or change the default representation.

See the [scope](sdf-shader-selection-loop.md),
[receipts](../evidence/sdf-shader-selection-2026-10/README.md), and
[preceding shader investigation](sdf-guardian-frame-profile-results.md).


Published [interactive report](http://starship:8787/reports/726d08d3e9412206ad5c96ca3c2f5e97d73ae38f18490b8874c3cf6415afce09/):
43 files / 3,110,545 HTTP-verified bytes, desktop and 390×844 mobile interactions
checked. [Focused checks](../evidence/sdf-shader-selection-2026-10/checks.json)
pass eight SDF tests plus shader build rules and three documentation checks.

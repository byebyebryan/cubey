# Dragon demo closeout

2026-11-17. The dragon is a bounded real-time ray-tracing demonstration under
Cubey's shared clear atmosphere. The closeout preserves the accepted optical
model, convex marble slab, glass recipe, smooth normals and existing transport.
It adds launch and presentation tools, matched evidence and a dedicated
site entry. No new transport, antialiasing or reconstruction is introduced.

The [interactive closeout report and prepared site snapshot](http://starship:8787/reports/db121962f7a3f6de71e151d48f9a868987ea457ca170d76ded224c58b8e7a03d/)
contain all three camera views, normal-speed native motion and current GPU
costs. The [public dragon showcase](https://dev.byebyebryan.com/cubey/demos/raytracing/)
uses the same three whole-view recordings; the
[deployment metadata](https://dev.byebyebryan.com/cubey/metadata/deployment.json)
identifies the published source revision. The report retains the prepublication
snapshot and its original capture scope.

## Three modes to keep

| Mode | Purpose | Remaining limit |
| --- | --- | --- |
| Forward hybrid | Cheap raster approximation with visible marble through glass and outgoing sky elsewhere. No RT capability or AS allocation. | Screen data and fixed thickness cannot reveal hidden surfaces, repeated glass intervals or internal reflection. |
| Fixed-quality RT | Default showcase: four spatial samples every frame, sharp Fresnel/Snell/TIR branches and actual scene intersections. | Fine motion shimmer, policy pruning and bounded work remain. Two samples per frame is an explicit cost/edge-quality option. |
| Sampled RT | Progressive diagnostic comparison; target 256 samples when stationary, restarted during camera motion. | Moving views receive one fresh primary sample per frame. Noise, solar speckles, the cap's energy loss and interface limitations remain. |

The sampled route is not an optical ground truth, and the table does not
compare equal antialiasing policies. The [forward visibility audit](raytracing-forward-transparency.md)
establishes that local objects actually show through the forward glass. The
[RT results](raytracing-dragon-demo-results.md) explain its deliberately bounded
branch policy. Both renderers reuse shared sky and material inputs; neither
claims GI or focused caustics.

## Launch and reproduce

Build and prepare the model using the [project guide](../../projects/raytracing/README.md#build-and-run).
The new launcher defaults to fixed RT and exposes three cameras across all modes:

```sh
python3 projects/raytracing/tools/dragon_demo.py --mode rt --view whole
python3 projects/raytracing/tools/dragon_demo.py --mode forward --view detail
python3 projects/raytracing/tools/dragon_demo.py --mode sampled --view front
python3 projects/raytracing/tools/dragon_demo.py --mode rt --rt-samples 2
python3 projects/raytracing/tools/dragon_demo.py -- --no-controls
```

All choices use FOV 45°, pitch 0.35 and disabled shadows. Whole: yaw 0.45,
distance 1.75, target (0, 0.32, 0). Detail: yaw 0.45, distance 1.25, target
(0.1, 0.25, 0). Front: yaw 1.10, distance 1.75, target (0, 0.32, 0).
Each launch starts the selected backend explicitly, preserving a genuinely
RT-disabled forward path. This is not a live renderer-switching layer.
Legacy presets remain unchanged; notably the original sampled-glass preset's
shadows remain enabled when invoked directly. The comparison launcher disables
them consistently. Extra native flags can intentionally override the comparison.

`--no-controls` is a native viewport option, visible controls remain the default,
and saved views retain the choice. Additional launcher arguments after `--`
resolve relative paths against the repository root. Use `--print-command` to
inspect a launch without starting the renderer.

Timing is a separate serial, deterministic GPU sweep:

```sh
python3 projects/raytracing/tools/benchmark_dragon_demo.py --output outputs/raytracing/dragon-demo-costs
```

It measures 151 matching moving poses after 30 warmup frames at 1920×1080,
with recording and validation disabled. These are unpaced headless project GPU
timestamps, not physical desktop FPS. The sampled moving route is measured
with one fresh sample, not 256 samples per frame. Do not compare these costs
directly to historical validated or recorded runs.

Native motion uses `record_dragon_demo.py --video --validate --hide-controls`
on an explicitly owned Xvfb display. Each clip records a five-second outward
orbit, a twelve-second stop, a five-second return and another twelve-second
stop, plus preroll/gesture overhead. Video is normal wall-clock speed at 30 fps;
displayed frames may repeat. These recordings demonstrate native behavior,
not physical desktop cadence. The full commands and hashes live in the
[closeout evidence](../evidence/raytracing-dragon-demo-closeout-2026-11/README.md).

## Accepted boundary

Motion shimmer remains visible in sharp, high-frequency glass. TAA, temporal
reconstruction and DLSS would require a broader renderer/foundation contract;
they are deferred rather than prerequisites for this demo. FXAA is not added.
Reopen this work for a concrete demo regression or a separately chosen shared
AA/reconstruction consumer, not for further optical perfection.

The site page uses the existing media system and a dedicated
`/demos/raytracing/` route, preserving the twelve-material workshop. Model
attribution and Stanford's separate usage notice accompany the media. Publication
uses the existing GitHub Pages workflow; capture qualification and deployed
revision remain separate records.

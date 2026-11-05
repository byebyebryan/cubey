# Ray tracing

`raytracing` is Cubey's dedicated Vulkan KHR hardware ray-tracing project.
It renders static triangle scenes with sharp scene reflections, closed clear-glass
refraction and Cubey's shared clear-sky lighting. The original opaque first-pass
route remains the default. Diffuse GI and the matched mesh/SDF comparison are
separate follow-up work.

## Build and run

The application requires a Vulkan 1.3 GPU with acceleration structures,
ray-tracing pipelines and buffer device addresses. It reports missing
capabilities rather than switching to another renderer. Existing Cubey
applications do not request these features.

```sh
rtk cmake --preset dev
rtk cmake --build --preset dev --target cubey_project_raytracing
rtk proxy build/dev/projects/raytracing/raytracing
rtk proxy build/dev/projects/raytracing/raytracing --headless --width 1280 --height 720 --output outputs/raytracing/fixture.png
```

The default generated fixture needs no downloaded data. `--scene triangle`
selects the original reference primitive; `--scene fixture` includes transformed
instances and a floor. These scenes are diagnostic controls rather than demos
of general asset compatibility.

## Stanford dragon

Prepare the pinned source explicitly from the repository root:

```sh
rtk proxy uv run projects/raytracing/tools/prepare_dragon.py
rtk proxy build/dev/projects/raytracing/raytracing --scene dragon --model cache/raytracing/stanford-dragon/dragon.glb --distance 1.6 --yaw .7 --pitch .25
```

Preparation downloads only when needed, verifies archive/member checksums and
writes a GLB and manifest into the ignored cache. It uses a pinned NumPy
dependency and records the normalization, area-weighted normal policy and
float encoding error. Triangle connectivity is preserved. The selected archive
member has **437,645 vertices and 871,414 triangles**; use these actual counts
instead of the larger count listed on the repository landing page. The source
contains 108 zero-area triangles, which remain in the preserved index stream.

Model source: Stanford Computer Graphics Laboratory,
[Stanford 3D Scanning Repository](https://graphics.stanford.edu/data/3Dscanrep/).
Its research/noncommercial usage and attribution terms remain separate from
Cubey's code license. Neither the original model nor the prepared GLB is
bundled into ordinary builds/tests. The input path is admitted as a normalized,
single-node, single-primitive untextured static GLB; this is not a general glTF
viewer. Missing dragon input reports the preparation command.

## Shared sky, mirrors and glass

After preparing the dragon, run a frozen optical view from the repository root:

```sh
rtk proxy build/dev/projects/raytracing/raytracing --config projects/raytracing/presets/optics/reflection.json
rtk proxy build/dev/projects/raytracing/raytracing --config projects/raytracing/presets/optics/curved-mirror.json
rtk proxy build/dev/projects/raytracing/raytracing --config projects/raytracing/presets/optics/refraction.json
```

`reflection-fixture`, `refraction-fixture` and `slab` are generated controls that
need no downloaded model. Add `--environment atmosphere --target-y .55` for a
shared-sky fixture. The dragon stays opaque; the separate prism is a consistently
wound, closed dielectric. One scene unit is one metre, and the local floor is y=0.
The camera must remain outside the glass.

`--reflection-source environment|scene` and `--transmission-source environment|scene`
select paired comparisons with identical geometry, light and camera. Scene rays
reach neighboring objects and off-screen markers. The environment comparison
samples the sky after the applicable interfaces and ignores neighboring objects
for that lobe. Each lobe is counted once; ideal mirrors/glass do not add cached
specular IBL on top of their traced result. Rough opaque surfaces retain shared
prefiltered environment specular.

One `AtmosphereEnvironmentRuntime` supplies the sun, clear sky, diffuse SH and
prefiltered sky. The RT adapter converts both angle units and azimuth convention:
legacy RT azimuth starts at +Z, shared azimuth at −Z. `--intensity 3` uses the
shared lighting's nominal gain; other values scale sun and environment together.
`--ambient` applies only to the legacy route. Exposure is applied once in shared
HDR postprocessing. The finite scene floor occludes real rays; the far-field
probe uses the shared `SkyOnlyNoGroundOcclusion` policy. This is a directional
sky cache, without per-segment aerial perspective or cloud composition.

Glass uses `--ior` (1–2.5), `--attenuation-red`, `--attenuation-green`,
`--attenuation-blue` (positive transmittance up to 1), and
`--attenuation-distance` (metres). It supports exact dielectric Fresnel, Snell
bending, TIR and Beer absorption. Straight sun-shadow transmittance through glass
uses Fresnel and absorption but does not bend the shadow path or form caustics.

Transport is deterministic, using sequential trace calls from ray generation
and pipeline recursion depth one. Defaults are `--max-events 8`,
`--max-path-rays 32`, `--max-total-rays 128`, `--throughput-cutoff .0001`.
The qualification control uses 12, 64, 256 and 0 respectively. There are at most
eight shadow interfaces. Exhausted/unsupported paths discard their remaining
throughput and expose a status; they do not silently replace it with sky.
Repeated mirror reflections can therefore show a small dark termination region.

## Controls and reproducibility

Drag to orbit, scroll to zoom and press R to reset. The panel edits light,
color, roughness, exposure, shadows, diagnostic view and optical controls. Appearance and camera
edits reuse the acceleration structures. Resizing changes target resources.

`--view` accepts `final`, `geometric-normal`, `shading-normal`, `depth`,
`instance-id`, `barycentric`, `secondary-id`, `path-class`, `path-events`,
`transport-limit` and `environment`. Debug views bypass tone mapping. Geometric
normals control ray offsets; interpolated vertex normals control shading.
`--fov` is the vertical field of view in degrees; yaw/pitch/light angles use
radians. Sampling is fixed at one centered primary ray per output pixel.

`--config` and `--set` use the normal typed-config conventions. Pass
`--save-config path.json` to expose the panel's **Save view** button; headless
runs write that configuration after their completed frame. The saved camera
includes orbit/zoom, the optical routes, IOR and environment settings. R resets
to the loaded preset. Headless movies use deterministic `--orbit-speed` and
`--sun-speed` in radians per second; saved views clear those animation speeds. `--metrics path.json` writes preparation/setup
observations, requested resource sizes, peak simultaneous project requests and
per-frame GPU timestamps after GPU completion. The project peak includes RT
buffers/images, build scratch, TLAS inputs and resize overlap; it excludes host
targets, upload staging, allocation padding and opaque driver internals. Profiling through `--profile-output` also uses the normal host path.

```sh
rtk proxy build/dev/projects/raytracing/raytracing --scene dragon --model cache/raytracing/stanford-dragon/dragon.glb --distance 1.6 --headless --frames 180 --headless-render-only --width 1920 --height 1080 --metrics outputs/raytracing/dragon-metrics.json
rtk proxy uv run projects/raytracing/tools/qualify_geometry.py --output outputs/raytracing/geometry-check
```

The geometry tool compares native GPU hit/depth/barycentric/normal/shadow
observations against independent double-precision rays. Its raw output is a
bounded diagnostic path, separate from normal-render timing. `--query-output`
requires one headless PNG frame and at most 32 MiB of geometry data.

## Scope and provenance

The renderer builds static BLAS/TLAS structures on the GPU, issues primary and
shadow rays sequentially from ray generation, then feeds an HDR result into
Cubey's postprocessing. GPU submission completion governs resource reuse and
destruction. There are no temporal histories, AS refits or background rebuilds.

This pass excludes diffuse GI, caustics, area-light sampling, rough scene
reflections, frosted transmission, dispersion, nested/overlapping media, textured
assets, geometry animation, accumulation, denoising and an SDF loader. Centered
single-sample primary rays retain silhouette aliasing. Legacy ambient fill and
straight glass shadows are explicit approximations. A native capture and GPU timestamp
do not establish physical interaction latency or human visual acceptance.

The RT setup follows Khronos Vulkan Samples `ray_tracing_basic`; the optical
scheduling reference also uses `ray_tracing_reflection`, both pinned at
`56a72bd1c43268781a75d73a520e4458b6cac766`. Adapted files retain Sascha Willems's
Apache-2.0 attribution, and optical scheduling retains NVIDIA's Apache-2.0 notice;
see [NOTICE](NOTICE) and [donor license](third_party/LICENSE.Apache-2.0).
The [implementation plan](../../docs/notes/raytracing-first-pass-plan.md)
records the initial contracts and acceptance boundaries.
The [first-pass results](../../docs/notes/raytracing-first-pass-results.md) and
[evidence index](../../docs/evidence/raytracing-first-pass-2026-11/README.md)
record actual runtime qualification, costs and remaining acceptance limits.

Collect the serial 1080p timing/capture packet with an optimized binary:

```sh
rtk cmake --preset release
rtk cmake --build --preset release --target cubey_project_raytracing
rtk proxy python3 -B projects/raytracing/tools/profile_first_pass.py --binary build/release/projects/raytracing/raytracing --output outputs/raytracing/review-packet
```

The dated evidence used a `build/dev` directory explicitly configured as
RelWithDebInfo (`-O2 -g -DNDEBUG`); an ordinary Debug build has different costs.

Run the native control check only in an owned virtual display:

```sh
rtk proxy xvfb-run -a -s '-screen 0 1280x960x24' env CUBEY_RT_PRIVATE_XVFB=1 python3 projects/raytracing/tools/check_window.py --output outputs/raytracing/window-check
```

## Optical qualification

The [optics results](../../docs/notes/raytracing-optics-results.md) and
[evidence index](../../docs/evidence/raytracing-optics-2026-11/README.md) record
shared-sky integration, numerical/lifecycle checks, convergence and matched costs.
The [plan](../../docs/notes/raytracing-optics-plan.md) retains the fixed gates.

```sh
rtk proxy uv run projects/raytracing/tools/qualify_optics.py --output outputs/raytracing/optics-check
rtk proxy uv run projects/raytracing/tools/check_sky_lookup.py --output outputs/raytracing/sky-check
rtk proxy uv run projects/raytracing/tools/inspect_optics.py --output outputs/raytracing/optics-residuals
rtk proxy uv run projects/raytracing/tools/profile_optics.py --binary build/dev/projects/raytracing/raytracing --mode quality --output outputs/raytracing/quality
rtk proxy uv run projects/raytracing/tools/profile_optics.py --binary build/dev/projects/raytracing/raytracing --mode timing --output outputs/raytracing/timing
rtk proxy xvfb-run -a -s '-screen 0 1280x960x24' env CUBEY_RT_PRIVATE_XVFB=1 python3 projects/raytracing/tools/check_window.py --config projects/raytracing/presets/optics/refraction.json --optics --output outputs/raytracing/window-optics
rtk proxy build/dev/projects/raytracing/raytracing --config projects/raytracing/presets/optics/reflection.json --headless --capture video --frames 120 --fps 30 --width 1280 --height 720 --orbit-speed .12 --output outputs/raytracing/reflection-orbit.mp4
```

Use an optimized build for timings. The timing tool runs serial repetitions,
retains 150 completed samples after 30 warmup frames, disables validation and
raw diagnostics, and separates frozen sky from moving-sun refresh. Do not run
other owned GPU work concurrently. Its source/binary/SPIR-V/preset/model hashes
and boundary GPU observations accompany the results; those observations do not
prove uninterrupted exclusivity on a shared host.

Raw diagnostic outputs are little-endian float32, row-major, top-to-bottom,
one record per pixel. `--query-output` preserves the first-pass 64-byte ABI.
`--optics-output` uses 256 bytes (16 vec4s), at most 32 MiB;
`--hdr-output` uses 16 bytes (linear RGB plus integer status encoded as float),
at most 64 MiB. Each requires one headless PNG frame, no video/render-only,
and dimensions no greater than 2048. `--scene-output` exports the actual CPU
geometry and material identities for the independent oracle.

| Optical vec4 | Meaning |
| --- | --- |
| 0–3 | Primary hit t/instance/primitive/visibility, geometric normal/material kind, shading normal/TIR count, barycentrics |
| 4–6 | First boundary incident/medium, reflected direction/Fresnel, transmitted direction/TIR |
| 7–9 | First transmitted and reflected hit position/instance, first in-medium attenuation/distance |
| 10–12 | Linear RGB/total calls, discarded RGB throughput/status, trunk opaque shadow transmittance/shadow calls |
| 13–15 | First outward boundary normal/instance, trunk exit direction/final opaque ID, radiance calls/max depth/stack peak/shadow calls |

Status bits are 1 total-call budget, 2 event budget, 4 radiance-call budget,
8 throughput cutoff, 16 invalid/ambiguous medium transition, 32 shadow-interface
budget. Flags may combine. Material kinds are 0 opaque, 1 mirror, 2 glass;
instance IDs are zero-based. Missing hit IDs use −1; exit diagnostics use −2 for
the environment comparison and −3 when unset. `--optics-contract` exercises the
production interface helper on a generated angle/thickness grid and requires
`--optics-output`; it does not claim full scene traversal coverage.

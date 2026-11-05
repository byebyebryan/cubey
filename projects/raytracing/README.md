# Ray tracing

`raytracing` is Cubey's dedicated hardware ray-tracing project. Its first pass
renders static opaque triangles through Vulkan's KHR ray-tracing pipeline,
with direct diffuse/GGX lighting, a constant ambient fill and hard shadows.
SDF comparison and progressive light transport are follow-up work.

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

## Controls and reproducibility

Drag to orbit, scroll to zoom and press R to reset. The panel edits light,
color, roughness, exposure, shadows and diagnostic view. Appearance and camera
edits reuse the acceleration structures. Resizing changes target resources.

`--view` accepts `final`, `geometric-normal`, `shading-normal`, `depth`,
`instance-id` and `barycentric`. Debug views bypass tone mapping. Geometric
normals control ray offsets; interpolated vertex normals control shading.
`--fov` is the vertical field of view in degrees; yaw/pitch/light angles use
radians. Sampling is fixed at one centered primary ray per output pixel.

`--config` and `--set` use the normal typed-config conventions. Pass
`--save-config path.json` to expose the panel's **Save view** button; headless
runs write that configuration after their completed frame. The saved camera
includes orbit and zoom edits. `--metrics path.json` writes preparation/setup
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

There is no GI, environment reflection, soft-light integration, transmission,
texture mapping, animation, denoising or SDF loader in this first pass. The
ambient fill is explicitly an approximation. A native capture and GPU timestamp
do not establish physical interaction latency or human visual acceptance.

The RT setup follows Khronos Vulkan Samples `ray_tracing_basic`, pinned at
`56a72bd1c43268781a75d73a520e4458b6cac766`. Adapted files retain Sascha Willems's
Apache-2.0 attribution; see [NOTICE](NOTICE) and [donor license](third_party/LICENSE.Apache-2.0).
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

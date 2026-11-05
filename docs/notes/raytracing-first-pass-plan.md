# Ray tracing first pass plan

Date: 2026-11-04. Status: proposed implementation contract; planning only.
Source audit: `0fd2147f`, `sdf-tracing-comparison`.

## Direction and first result

Create a dedicated `projects/raytracing` application, executable `raytracing`.
Hardware ray tracing is the first subject. SDF rendering remains a planned
comparison path, supported by the
[existing SDF foundation](../../projects/sdf/README.md).

The first pass delivers a static Stanford dragon above a floor, rendered with
hardware triangle intersections, direct lighting and hard ray-traced shadows.
It includes a native orbiting view, reproducible headless captures, geometry
diagnostics and separate startup/frame/memory measurements. This is the first
hardware renderer milestone, before the two-representation comparison demo.

Use the full `VK_KHR_ray_tracing_pipeline` interface: acceleration structures,
shader binding table, ray generation, closest hit and miss shaders. Its explicit
pipeline is part of the learning goal. Keep C++20, Vulkan 1.3 and build-time
GLSL compilation. Ray queries are a future integration option rather than a
second first-pass implementation.

| First-pass contract | Boundary |
| --- | --- |
| Geometry | Static opaque indexed triangles; one dragon and a triangulated floor |
| Tracing | GPU-built BLAS/TLAS; all camera and shadow visibility uses hardware RT |
| Lighting | One directional light, diffuse and GGX direct response, explicit constant ambient fill |
| Sampling | One centered primary ray per output pixel; at most one shadow ray per lit hit |
| Appearance | Constant material per instance; fixed exposure and shared HDR postprocessing |
| Interaction | Orbit, zoom, reset, light direction, material color/roughness, shadows and diagnostic views |
| Delivery | Native application, saved presets, captures, measurements and reproduction notes |

The ambient term is an approximation, not indirect illumination. Environment
reflections, GI/path tracing, soft lights, transmission, textures, animation,
denoising, SDF loading and the public site are later milestones. The first pass
does not change existing SDF defaults, models, history or private studies.

## Reference and asset

Follow the [reference-first workflow](../architecture/rendering-feature-workflow.md).
The primary donor is Khronos Vulkan Samples
[`ray_tracing_basic`](https://github.com/KhronosGroup/Vulkan-Samples/tree/56a72bd1c43268781a75d73a520e4458b6cac766/samples/extensions/ray_tracing_basic),
pinned at `56a72bd1c43268781a75d73a520e4458b6cac766`. Its implementation and
GLSL shaders carry Apache-2.0 notices. Preserve those notices for adapted code.
Port the small RT core rather than importing its application framework.

The planning audit fetched the pinned C++ and ray-generation/closest-hit
sources. The C++ file SHA-256 is
`3010a657acfd6d893bd4c33504a9e07d394b49c7f7721c5b3e266eb1642cef18`.
Running and capturing the donor remains milestone M0; source inspection alone
does not establish visual parity. First reproduce its triangle and barycentric
output, then add Cubey lighting and the dragon as separate changes.

Use the original [Stanford Dragon reconstruction](https://graphics.stanford.edu/data/3Dscanrep/),
listed as 566,098 vertices and 1,132,830 triangles with small holes. Hardware
triangle rendering does not require a closed solid, so preserve its geometry
for this pass. Future SDF work must decide and record solid preparation; a
prepared comparison mesh and the original reference remain distinct identities.

M0 selects the exact archive member and records archive/source SHA-256, bounds,
counts, source URL and Stanford attribution/usage terms. Keep the asset's terms
separate from Cubey and donor-code licensing. Prepare an untextured GLB offline
with positions, indices and an explicitly recorded smooth-normal policy.
Record the normalization transform and verify that preparation preserves the
triangle surface, allowing only the declared transform and float encoding.
No decimation, hole filling, welding or topology repair is part of this pass.

Use a narrow project preparation script and Cubey's existing CPU glTF loader;
do not build a native PLY importer or a general asset-conversion service.
Large source/prepared files live in ignored local asset storage. Track the
small manifest, preparation recipe and terms. Ordinary builds/tests do not
download the dragon. Missing input produces an actionable preparation error.
Generated diagnostic scenes remain available without external data.

## Source audit and ownership

Current hosts create a device before project callbacks. RT therefore needs a
host-to-device capability request; enabling extensions inside the renderer
would be too late. Current buffer allocations do not request device addresses,
and the shader builder invokes glslang without an explicit target environment.
The graph validates graphics and compute shader stages and rejects RT shader
stage masks. These are real integration tasks, not existing RT support.

| Owner | Planned responsibility |
| --- | --- |
| `cubey::host` configs and device construction | Forward an explicit RT requirement in windowed and headless paths |
| `cubey::vulkan::Device` | Capability-based selection, feature enablement, RT properties and device extension dispatch |
| `cubey::vulkan::Buffer` | Address-capable allocation and address retrieval where requested |
| `cmake/CubeyShaders.cmake` | Per-target Vulkan shader environment with existing compiler depfiles retained |
| `projects/raytracing` | Scene admission, BLAS/TLAS assembly, pipeline/SBT policy, descriptors, shaders, controls and metrics |
| Existing render/host helpers | HDR presentation, frame slots, GPU ownership, capture, profiling and UI |

No shared renderer service or generic backend interface is required before a
working image. Extension dispatch and small raw Vulkan owners may be shared;
scene and rendering policy stay project-local. Do not route this through the
forward-PBR resident importer or extend that renderer to support RT.

### Device and build contract

RT is requested only by the new application. Existing applications keep their
feature requests and device-selection behavior. The new app requests one queue
family supporting graphics and compute, with presentation for the windowed
path. Report missing capabilities before creating project resources; do not
silently fall back to rasterization or confuse exposed and enabled features.

On the existing Vulkan 1.3 baseline, request acceleration structures,
ray-tracing pipelines and deferred-host-operations extensions, plus the
`accelerationStructure`, `rayTracingPipeline` and `bufferDeviceAddress`
features. Resolve promoted dependencies against the actual API version.
No bindless descriptor features or ray-query feature are needed for the fixed
scene table. The reference's formatless output is replaced by an explicit
supported HDR storage-image format, without requiring formatless image access.

Read device limits for scratch alignment, shader-group handle/record alignment,
SBT base alignment/stride and supported recursion. Addressable allocations
must use the required memory-allocation flags. Load KHR commands through the
selected device; reject missing entry points. Keep every new default opt-in.

Add a per-target shader environment and compile only the RT shader package for
Vulkan 1.3. Existing shader commands retain their environment. Check the new
`.rgen`, `.rchit` and `.rmiss` stages, included GLSL dependencies and a clean
rebuild; no Slang migration or global Vulkan-version bump is required.

### Rendering and synchronization contract

Build static BLASes and a TLAS once on the GPU owner through the existing queue.
The floor is actual triangle geometry in the acceleration structure. Retain
vertex/index buffers for shading. Camera/light/material edits and resize do
not rebuild the acceleration structures.

Ray generation issues a primary ray, consumes hit attributes, then issues an
opaque shadow ray. Use separate primary/shadow miss records, first-hit shadow
termination and skipped shadow closest-hit work. Sequential calls allow
pipeline recursion depth one. Keep the primary hit payload small; reconstruct
surface attributes from geometry/instance IDs and barycentric coordinates.
No any-hit, callable or custom-intersection shaders are needed for this scope.

Keep geometric and shading normals distinct. Transform normals correctly,
orient smooth normals consistently, and derive ray-origin offsets from scene
scale and geometric information. Check transformed instances, grazing rays and
self-shadowing before tuning the hero image.

Record initial builds and RT tracing explicitly. The first pass does not
pretend these are ordinary graphics/compute shader accesses in the existing
graph. Import the per-slot HDR trace output into the postprocessing graph with
its actual RT-write image layout, stage and access state; let that graph own
the transition to its sampled read and the host output. Check the reverse
transition on slot reuse. Acceleration-structure build dependencies and their
write-to-trace-read handoff stay explicit outside the graph.

Scratch survives build completion. Acceleration structures, SBTs, shading
buffers and descriptors survive their last consuming submission. Per-slot
uniforms and output images are reused only after that slot completes. Resize
replaces target resources while retaining geometry/pipeline resources that
remain compatible. Partial creation failure and retry must not leak handles
or expose an incomplete scene. Use the established owner completion boundary
for shutdown. AS compaction, refits, streaming and split queues remain deferred.

## Implementation milestones

Implement in a dedicated sibling worktree. Preserve the existing dirty horse
work, SDF README edit and retained binaries/captures. Each milestone should be
reviewable independently; a runtime milestone does not authorize publication.

| Milestone | Work | Acceptance before advancing |
| --- | --- | --- |
| M0 Reference and fixture | Run/capture pinned donor; freeze dragon member, preparation and camera/light conventions | Known-good reference image; verified inputs/terms; reproducible prepared GLB |
| M1 First Cubey RT image | Opt-in device/host support, addresses, shader build, pipeline/SBT and generated triangle | Headless primary hit/miss and barycentric output agree with the reference; capability failures are explicit |
| M2 Static geometry and shading | Multiple transformed fixture instances, dragon/floor BLAS/TLAS, normals, direct light and hard shadows | Independent fixture checks pass; dragon diagnostics are credible; no runtime source-mesh CPU query |
| M3 Native application | Windowed controls, typed config, presets, HDR post, capture and lifecycle handling | Headless/windowed camera parity; resize/recreate, edits, teardown and failure/retry pass without AS rebuilds |
| M4 Qualification and closeout | Fixed captures, timing/memory packet, visual review and operational guide | Measured costs and open limits are explicit; focused/full integration checks pass; first-pass review is complete |

For M1 reference parity, retain donor pixel generation/output separately from
later Cubey tone mapping. M2 establishes the rendering contract; M3 should not
introduce new transport effects while qualifying host integration.

## Controls and acceptance

The project config should cover scene selection, orbit camera, base color,
roughness, light direction/intensity, ambient fill, fixed exposure, shadows and
view selection. Diagnostic views are final color, geometric normal, shading
normal, ray depth and instance ID. Keep camera/light/material state stable
across view changes. Save/reload reproduces a capture. The default generated
fixture runs without the dragon; the explicit dragon preset fails clearly
when its prepared asset is missing.

Freeze three dragon views: hero, opposite side and close detail. Keep transforms,
projection, framing, light, materials, normal policy and display transform in
tracked presets. These become the later SDF comparison inputs; their exact
numbers are selected from native images in M2, not guessed from a thumbnail.

Validation covers:

- Independent triangle/plane ray cases: hit/miss, depth, barycentrics,
  transformed instances, normal transforms and known shadow occlusion. Avoid
  ambiguous edge hits when applying strict equality tolerances.
- Capability rejection and ordinary non-RT selection; address allocation,
  SBT alignment/region bounds and explicit shader target/dependency behavior.
- Vulkan and synchronization validation for build, trace, RT-to-post handoff,
  repeated frame-slot reuse, captures, resize and shutdown. Exercise an owned
  partial-creation failure followed by retry.
- Existing foundation-layer, public-header, shader-build and documentation
  checks, focused host/buffer/graph checks and the normal dev suite. Compare
  representative existing SDF and mesh-PBR captures after shared changes.
- Native image inspection for orientation, missing geometry, faceting,
  self-shadow acne, detached shadows, clipped highlights and diagnostic output.
  Virtual-window automation, physical interaction and human visual acceptance
  retain separate receipts.

Set numerical tolerances before collecting scored fixture results. Start from
analytic ray truth and scene scale; do not tune tolerances to the observed RT
output. Runtime checks must execute the new native binary and built shaders.
Reference compilation alone does not qualify Cubey integration.

Measure normal shader routes with timestamp profiling only at 1920x1080,
using the three frozen views and shadows on/off. Use three serial repetitions per configuration,
180 frames with 30 warmup and 150 completed GPU samples. Collect trace-pass
and whole-scene p50/p95 separately; collect geometry diagnostics outside timing.
Record source/binary/shader/config identities, GPU/driver, host-load observations
and whether Vulkan validation was enabled. Do not overlap owned build, test,
capture or encoding work with the final timing batch.

Report CPU preparation/upload, GPU AS build, pipeline setup and time to first
completed image separately from steady frame cost. Separate requested geometry,
AS, SBT, scratch and target bytes, peak simultaneous allocations and observed
device-memory usage. No startup cost is hidden inside or removed from an
interactive-FPS claim. Native present/UI and physical display latency remain
separate from headless GPU timing.

Use 16.7 ms whole-scene GPU p95 as a proposed 1080p performance screen on the
observed RTX 5070 Ti, not a guaranteed result or permission to reduce resolution.
A miss requires a bounded diagnosis and an explicit result; it does not justify
an unbounded optimization loop or adding effects before the cause is understood.

Closeout requires the runnable app/guide, frozen presets, native captures and a
small evidence index joining commands, reference/asset identities, validation,
costs and remaining limits. First-pass implementation can close while physical
or artistic acceptance remains explicitly open; a polished public-demo claim
requires that visual review. Source publication and site promotion are separate.

## Follow-up direction

After this hardware baseline, bring a mesh-derived sampled SDF into the native
comparison using the same scene, material, lighting and camera contracts.
Separate source preparation, representation error and shading-normal policy.
Reuse existing SDF mechanisms where they fit rather than migrating its whole
authoring application into the new project.

Then develop progressive light transport on hardware triangles and apply
equivalent material/sampling logic to SDF intersections. Hardware-accelerated
SDF traversal and RT adoption by other renderers remain subsequent choices.
These stages preserve the software-marching versus dedicated-hardware contrast
without making SDF conversion a prerequisite for the first hardware image.

## Primary API references

- [Khronos RT sample overview](https://docs.vulkan.org/samples/latest/samples/extensions/ray_tracing_basic/README.html)
- [Vulkan ray-tracing interfaces and synchronization](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html)
- [Acceleration-structure requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_acceleration_structure.html)
- [Ray-tracing pipeline requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_tracing_pipeline.html)
- [Cubey renderer ownership and lifecycle](../architecture/renderer-foundation.md)

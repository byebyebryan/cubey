# Vulkan Abstraction Map

This document maps Cubey's current Vulkan foundation and the concrete triggers
for expanding it. It is a planning guide, not a promise to abstract every
Vulkan concept. Add code to
`cubey::vulkan` when it creates a deliberate foundation contract, fixes a real
lifetime or synchronization hazard, removes repeated setup, or gives projects a
clearer vocabulary without hiding the constraints that matter.

## Direction

Cubey is Vulkan-first. The reusable layer should make examples and projects
shorter, but it should still feel like native Vulkan: resource ownership,
layout transitions, queue constraints, descriptor choices, and frame boundaries
must remain visible where they affect behavior.

The current boundary is:

- `cubey::vulkan` owns Vulkan object lifetime and common create-info
  construction.
- `cubey::render` owns renderer-facing vocabulary above Vulkan: target views,
  resource wrappers, material/pass metadata, graph declaration/execution, and
  small draw helpers.
- `cubey::engine` owns the scoped Engine root, project runtime services and GPU
  bridge, render-resource identity, and shared renderer policy and instance
  lifetime through `RendererService`.
- Hosts own platform and device setup plus the host-visible `GpuRuntime`.
  Examples and projects retain product render intent: which views and passes to
  submit, authored asset/resource lifetime, project settings, and interaction
  policy. App/window hosting lives outside `cubey::vulkan`.
- Higher-level renderer, material, and render-graph concepts should be designed
  from established graphics terminology and clear Cubey contracts. They should
  remain narrow, with current authoritative detail in the
  [renderer foundation](renderer-foundation.md) and
  [render graph direction](render-graph.md).

## Foundation Rules

- Prefer narrow RAII wrappers and create-info helpers before policy-heavy
  abstractions.
- Add shared code after repeated call sites, after one call site exposes a real
  correctness hazard, or when an established graphics concept has a clear
  durable contract.
- Search for precedent before naming or shaping new concepts. Start with the
  [Vulkan specification/guide](https://docs.vulkan.org/),
  [Filament](https://github.com/google/filament),
  [Godot](https://docs.godotengine.org/en/stable/),
  [Unity](https://docs.unity.cn/Manual/),
  [Unreal](https://dev.epicgames.com/documentation/en-us/unreal-engine), and
  mature graphics papers or engine notes relevant to the feature.
- Keep synchronization and image layouts explicit unless the helper name makes
  the exact transition obvious.
- Keep GLFW/platform code out of the low-level Vulkan library. If it becomes
  reusable, split it into a platform/host layer rather than mixing it into
  `cubey::vulkan`.
- Keep threading and async work shaped by
  [threading and async design](threading-and-async.md): one GPU owner first,
  CPU jobs behind Cubey APIs, queued upload/capture requests, and explicit
  contracts for parallel command recording or split queues.
- Do not invent portability-layer vocabulary unless Cubey code has naturally
  arrived there. Descriptors may eventually look like bind groups, but the
  Vulkan ownership model comes first.

## Layer Map

### 1. Device, Queues, And Capabilities

Current state:

- `Instance` owns validation/debug-utils setup.
- `Device` owns physical-device selection, logical-device lifetime, one queue
  family, queue access, feature checks, and memory-type selection.
- `choose_depth_format` centralizes the current supported depth format probe.
- The current submission model has a strict GPU owner: `GpuRuntime` accepts
  queued owner-thread work, runs threaded by default in hosts, and keeps inline
  execution explicit for tests, while `SubmissionCoordinator` serializes the
  actual queue submissions.

Expansion triggers:

- Add shared capability helpers when multiple consumers need the same optional
  format or feature query.
- Extend the queue-family model when a measured workload benefits from split
  graphics, compute, or present queues.
- Extend submission-ticket completion into a broader fence/timeline contract
  only when project-runtime readback or destruction needs completion semantics
  beyond the current binary-fence watermark.

Defer:

- Sophisticated multi-GPU selection.
- Full queue abstraction, split queues, and timeline-semaphore scheduling until
  the ownership model and scheduling contract are clear.

### 2. Frame And Presentation

Current state:

- `Swapchain` owns swapchain images and views. Windowed swapchains prefer
  `VK_PRESENT_MODE_MAILBOX_KHR` for interactive demos, fall back to
  `VK_PRESENT_MODE_IMMEDIATE_KHR` when mailbox is unavailable, and finally fall
  back to required FIFO support. Explicit non-default requests still fail if the
  surface cannot support them.
- `FrameResources` owns per-frame-slot command buffers, acquire semaphores, and
  fences, per-slot submitted GPU tickets, plus per-image present-ready
  semaphores.
- `RenderContext` owns the surface-backed `begin_frame` / `end_frame`
  acquire, submit, present, frame-slot advance, and recreate result path.
- `SwapchainRecreateTracker` owns bounded consecutive recreate-attempt tracking.
- The host layer exposes the active render frame slot to callbacks. Frame slot,
  frame index, and swapchain image index are separate concepts.

Expansion triggers:

- Add a reusable swapchain-sized resource coordinator only if multiple hosts or
  projects converge on the same rebuild ordering.
- Extend frame-ticket APIs for nonblocking readback/capture polling only when an
  interactive capture workflow needs it; per-slot submission tickets already
  support current deferred retirement.

Defer:

- Generalized renderer/material/scene abstractions beyond the current
  `cubey::render` and `cubey::engine` contracts.

### 3. Commands And Submission

Current state:

- `CommandPool` owns command-pool lifetime and primary command-buffer
  allocation.
- `ImmediateCommands` owns one-shot setup command recording and submits through
  `SubmissionCoordinator`.
- `begin_command_buffer` removes repeated begin boilerplate.
- `end_command_buffer` removes repeated end/check boilerplate.
- `CommandRecorder` wraps a non-owning `VkCommandBuffer` for common recording
  calls: begin/end, dynamic rendering boundaries, image layout transitions,
  pipeline barriers, pipeline and descriptor binding, push constants, draws,
  indexed draws, and dispatches.
- `QueueSubmit`, `submit_to_queue`, and `submit_to_device_queue` centralize the
  current binary-semaphore `VkSubmitInfo` shape used by frame submit and
  immediate commands.
- `SubmissionCoordinator` serializes queue submission and issues monotonic GPU
  submission tickets. `RenderContext` routes frame submission/present through
  `GpuRuntime`, which uses the coordinator on the GPU owner thread and marks
  frame-slot tickets completed after the matching fence wait.
- `GpuRuntime` is the host-owned GPU work queue and owner-thread context.
  Windowed/headless hosts expose it through their contexts and run it threaded
  by default, with explicit drain/wait calls at host-owned synchronization
  points. It is the public boundary for host/project setup-time GPU work;
  `ImmediateCommands` remains the low-level one-shot command helper used inside
  owner-context callbacks and transfer helpers.
- Move-only `GpuRuntimeOwnerCleanup` registrations let asynchronous products
  retain an owner-only cleanup action without keeping a raw runtime pointer.
  A request is nonblocking; if it cannot be admitted before shutdown, the
  runtime consumes the retained action after queue idle and before staging-pool
  teardown.
- `copy_buffer`, `upload_device_buffer`, and bounded device-buffer batches cover
  current setup-time transfers into device-local buffers. A batch validates all
  requests before allocation, creates destination buffers transactionally, and
  packs source bytes into staging chunks capped at 32 MiB. Each chunk records
  all of its buffer copies into one immediate command buffer and performs one
  synchronous transfer submission. Empty batches are no-ops and the single
  upload helper delegates to a one-request batch.
- `copy_buffer_to_image` and `copy_image_to_buffer` cover current one-shot
  image transfer/readback copies.
- Command recording is single-threaded. Examples and projects own pass order,
  barriers, descriptor binding policy, and render intent, while using
  `CommandRecorder` for the repeated Vulkan call surface.

Expansion triggers:

- Debug-label helpers once marker scope becomes useful during capture/debugging.
- Add higher-level one-shot compute/transfer vocabulary only when a second
  consumer repeats a stable shape.
- Add another queued transfer or capture facade only when a project-facing
  workflow needs a contract beyond `GpuRuntime` and `ProjectGpuServices`.
- Per-frame/per-thread command-pool sharding before any parallel command
  recording.

Defer:

- Parallel command recording and secondary command buffers until profiling or a
  concrete renderer contract makes command recording cost worth addressing.
- A general queue class until split queue families force the shape.

### 4. Resources And Memory

Current state:

- `Buffer`, `Image`, and `Sampler` own basic Vulkan resource lifetime.
- `DepthAttachment` owns the swapchain-sized depth image/view path.
- Shared buffer helpers cover staging config, device-local config, and
  setup-time uploads for vertex/index data.
- Shared image helpers cover generated sampled image config, uploaded sampled
  image config, and buffer-image copy regions.
- Shared buffer helpers cover readback buffer config and host-visible coherent
  download.
- Shared image and image-transition helpers now cover the first offscreen color
  render target and color-attachment-to-readback transition used by headless
  output.
- `cubey::host::HeadlessCaptureHost` owns the repeated no-window offscreen
  target, capture transition, runtime-queued capture recording,
  project-GPU readback ticket, PNG artifact write path, and optional MP4 video
  capture path. `HeadlessPngHost` remains as the legacy compatibility name.
- `cubey::render::Texture2D` and `TextureCube` now own the current generated,
  uploaded, and cubemap sampled texture image shapes above the raw Vulkan
  `Image` and optional `Sampler`.
- `cubey::render::DepthTexture` owns sampled depth image setup for shadow maps
  and other depth-as-texture paths above the raw Vulkan `Image` and optional
  `Sampler`.
- `SamplerConfig` exposes border color, compare enable/op, mipmap mode, LOD
  bounds, and LOD bias so shadow, edge-clamped, and prefiltered cubemap
  sampling policy can be explicit.
- Examples still own some resource policy, including when transfers and
  readback are used.
- Low-level upload/readback helpers remain synchronous building blocks.
  Batching reduces owner-thread round trips and queue submissions; it does not
  introduce a split transfer queue. The host-visible capture-record path now
  goes through `GpuRuntime`, and
  project-facing RGBA8 image readback goes through `ProjectGpuServices`
  tickets.
- A host-configured `GpuRuntime` owns a persistently mapped bounded staging
  pool. It starts at 32 MiB, grows in 32 MiB blocks to a 128 MiB cap, reclaims
  ranges and transfer resources by completed submission ticket, and reports
  bounded-pool backpressure. Current glTF installation keeps physical steps
  within 32 MiB, individual buffer or block-row-aligned texture copies within
  2 MiB, and a soft 2 ms owner-CPU target.

Expansion triggers:

- Add GPU capture polling and broader ticket handoff once interactive capture
  becomes a real workflow.
- Add another staging policy or transfer path only if measured install stalls
  exceed the current bounded owner-side contract.

Defer:

- VMA or another allocator until manual memory allocation becomes the limiting
  cost.
- Transfer overlap and buffer suballocation until measured setup-time
  allocation or copy cost justifies their lifetime model.
- General multi-asset streaming and partial residency until a product supplies
  measured pressure.
- Prefiltered KTX/KTX2 environment import and offline filtering until direct
  HDR IBL resources expose enough quality and runtime-pressure evidence.

### 5. Descriptors And Bindings

Current state:

- `DescriptorSetLayout` and `DescriptorPool` own basic layout/pool lifetime.
- Descriptor allocation is owned by `DescriptorPool`.
- `DescriptorSetInfo` owns layout-binding and pool-size create-info storage for
  the common one-layout/one-pool shape.
- `DescriptorSetBundle` owns a descriptor set layout, pool, and one allocated
  descriptor set for examples that do not need a custom descriptor allocator.
- `DescriptorSetArray` owns one layout, one pool, and multiple allocated sets
  for per-frame binding data while preserving Vulkan descriptor terminology.
- Descriptor helper functions cover layout bindings, pool sizes, uniform-buffer
  writes, storage-buffer writes, storage-image writes, combined image sampler
  writes, and descriptor set updates.
- `DescriptorWriteBatch` stages mixed descriptor writes in append order and
  owns the backing buffer/image info storage until the Vulkan update call.

Expansion triggers:

- Add a sampled-image descriptor write helper if a sampled image without
  sampler gets a concrete use case.
- Add resettable descriptor-pool helpers once descriptor reuse needs a stronger
  lifetime contract than the current owned bundle/array shapes.

Defer:

- A full bind-group abstraction until descriptor repetition stabilizes across
  more examples/projects.

### 6. Shaders And Pipelines

Current state:

- `ShaderModule` owns shader module lifetime.
- CMake compiles GLSL to SPIR-V with shared include support.
- `read_spirv_file` loads compiled SPIR-V bytecode into aligned 32-bit words
  through the Vulkan `shader_bytecode` helper for shader module creation.
- `PipelineLayout`, `GraphicsPipeline`, and `ComputePipeline` own pipeline
  lifetime.
- `DynamicGraphicsPipelineInfo` builds the current dynamic graphics pipeline
  create-info shape for color or depth-only dynamic-rendering targets, with
  optional depth and blending where applicable.
- `PipelineLayoutInfo` builds pipeline layout create-info for descriptor set
  layouts and push constants.
- `ComputePipelineInfo` builds the current compute pipeline create-info shape.
- Render-level `MaterialPassInfo`, pipeline recipe helpers, and the canonical
  PBR material/pass contracts sit above these Vulkan pipeline primitives; their
  ownership and policy are documented in the
  [renderer foundation](renderer-foundation.md).

Expansion triggers:

- Optional graphics-state knobs only when examples need them.

Defer:

- Shader reflection, hot reload, pipeline cache, and pipeline libraries until a
  concrete project needs them.
- Broader material-asset policy beyond the current
  `cubey::render::MaterialPassInfo`, PBR descriptor contract, and
  `ForwardPbrRenderer3D` ownership boundary.

### 7. Render Attachments And Render Targets

Current state:

- Dynamic rendering is the primary path.
- `image_transitions` covers current color/depth/storage/transfer layout
  transitions; `dynamic_rendering` covers attachment-info construction.
- `DepthAttachment` covers the current reusable depth target ownership path.
- `image_transitions` also covers current storage-image, transfer-destination,
  sampled-image readback, color-attachment readback, and sampling transition
  paths.
- `cubey::render::ColorTargetView`, `DepthTargetView`, `RenderTargetView`, and
  `RenderTargetRenderingInfo` provide target vocabulary and dynamic-rendering
  setup above Vulkan without owning layout transitions.
- `cubey::render::DepthOnlyRenderingInfo` provides depth-only dynamic-rendering
  setup for sampled depth targets while preserving explicit command-buffer
  scope and layout ownership.
- `image_transitions` covers depth-attachment-to-sampled and
  sampled-depth-to-depth-attachment transitions for repeated shadow-map writes.
- `CompiledRenderGraph` precomputes immutable allocation requirements from
  declared graph accesses. `RenderGraphResourceSet` compares those
  requirements and can allocate simple non-aliased transient color targets;
  `shadow_cube` uses that path for its scene color target before a fullscreen
  present pass samples it into the swapchain.
- `RenderGraphBuilder` and `RenderGraphFrameExecutor` now provide the shared
  declaration, validation, per-frame resource-set reuse, and graph-owned
  boundary synchronization used by the forward-PBR and other multi-pass
  consumers. The [render graph direction](render-graph.md) remains the
  authoritative detail.

Expansion triggers:

- Add clear/load/store options if a project stops clearing every frame.
- Add another target shape only when a concrete project needs multiple color
  targets or resolve attachments.

Defer:

- Classic render pass abstraction.
- Render-graph scheduling, culling, aliasing, descriptor ownership, and async
  execution beyond the current declaration/sync/resource-resolution boundary.

### 8. Mesh, Texture, And Scene Convenience

Current state:

- Cube examples use shared primitive mesh data, vertex input layouts,
  `cubey::render::Mesh`, `DrawItem`, and `ForwardScenePass3D` for indexed
  forward rendering while keeping shader-specific push constants local.
- `examples/textured_cube` uses `cubey::render::Texture2D` and
  `create_compute_generated_texture_2d` for its compute-generated sampled
  texture ownership.
- `examples/instanced_cubes` uses `InstanceBuffer<T>` and a multi-binding vertex
  layout for real instance-rate cube attributes.
- `examples/material_cubes` uses multiple material handles and per-packet
  material instance binding.
- `examples/shadow_cube` uses `cubey::render::DepthTexture`,
  `DepthOnlyRenderingInfo`, depth-only pipeline setup, graph-derived
  sampled-depth/scene-color/backbuffer sync, and a graph-created transient
  scene target for a directional shadow map plus fullscreen present pass.
- Cube examples use the shared GLM-backed `cubey::math` wrapper for MVP/model
  matrices and Vulkan clip-space projection conventions.
- `examples/particle_cubes` still defines particle storage-buffer layout,
  seeding, simulation parameters, cube instance interpretation, and compute
  barrier policy locally.
- `cubey::render::MaterialPassInfo`, the canonical PBR descriptor/material
  schema, pooled `PbrMaterialTable` residency, and material instances provide
  the current shared material/pass vocabulary. `ForwardPbrRenderer3D` owns the
  reusable forward-PBR policy; projects still own authored textures and render
  intent. See the [renderer foundation](renderer-foundation.md) for the full
  descriptor and environment contract.
- `cubey::scene::RenderResourceRegistry`, `ResourceTable`, renderable packets,
  and material/pass metadata provide the current CPU scene/resource identity
  boundary. They do not own Vulkan resource lifetime.
- `GltfSceneImporter` and `GltfSceneUploadSession` provide the current
  CPU-prepare, GPU-owner residency, and frame-boundary activation path. The
  closed glTF Viewer V1 scope keeps whole-generation activation and bounded
  single-asset staging; general streaming and partial residency remain
  deferred. See the [glTF asset direction](gltf-assets.md) and [Viewer V1
  closure](../notes/gltf-viewer-v1-closure.md).

Expansion triggers:

- Small geometry helpers once repeated examples or a clear primitive contract
  justify them.
- Add storage-buffer or billboard helpers once the data layout and render
  contract are clear enough to avoid baking in one particle demo's policy.

Defer:

- General scene graph, material asset graph, and asset database policy.
- Generalized multi-asset streaming or partial residency; the current glTF
  importer and renderer integration are complete for the closed V1 scope.

### 9. App And Project Runtime

Current state:

- `cubey_host` owns GLFW/window/surface hosting, key/pointer input dispatch, the
  shared windowed loop, frame timing/stats hooks, and swapchain recreation for
  all current windowed examples.
- Windowed examples still own shaders, pipelines, descriptors, swapchain-sized
  render resources, command recording, and example behavior.
- `cubey::host::HeadlessCaptureHost` owns the repeated no-window Vulkan setup,
  offscreen RGBA render target, capture transitions, readback buffer copy, PNG
  writing, and optional MP4 writing for current headless examples/projects.
  `HeadlessPngHost` remains as the legacy compatibility name.
- Shared non-platform helpers cover frame timing, frame stats, and orbit
  control.
- `Engine` owns project runtime services, the CPU-side render-resource registry,
  created scenes, and `RendererService`/`ForwardPbrRenderer3D` instances. It
  does not own platform or device setup.
- `ProjectContext`, `ProjectRuntimeAdapter`, and `ProjectGpuServices` provide
  the current project lifecycle, queued CPU/GPU work, and ticket handoff
  vocabulary. Projects retain render intent and authored resource policy.

Expansion triggers:

- Higher-level host lifecycle only if a second project repeats project-level
  setup/update/render/resize/shutdown structure beyond the current
  `ProjectRuntimeAdapter` boundary.
- Additional `ProjectContext` services only when a concrete project needs a
  stable cross-project contract.

Defer:

- Pulling GLFW into `cubey::vulkan`.
- UI layer or ImGui until the render/runtime boundary is clearer.

### 10. Threading And Async

Current state:

- The design is captured in [threading and async design](threading-and-async.md).
- Current Vulkan work runs through narrow windowed/headless hosts plus
  runnable-owned command recording.
- `cubey::jobs::JobSystem`, `InlineExecutor`, and `JobHandle` provide the first
  CPU job facade.
- `CaptureQueue`, `CaptureTicket`, and `CaptureBacklog` provide job-backed PNG
  encoding for completed RGBA pixel buffers plus bounded multi-file export
  draining.
- `UploadQueue`, `UploadTicket`, and `QueuedUpload` provide the first CPU-owned
  upload request queue.
- `GpuSubmissionTicketIssuer`, `GpuSubmissionTicket`, and
  `DeferredGpuDestructionQueue` provide GPU submission retirement vocabulary.
- `SubmissionCoordinator` provides GPU submission tickets and serialized queue
  submission for frames and immediate work.
- `GpuRuntime` provides the host-owned GPU work queue and owner-context boundary
  with a threaded default and explicit inline mode.
- `Engine` is the scoped root owner for project runtime services, render
  resource identity, scene creation, and renderer instance lifetime; it does
  not own host/device setup.
- `ProjectContext`, `ProjectFrame`, `ProjectExtent`, `RenderPacket`, and
  `ProjectRuntimeServices`, `ProjectRuntimeAdapter`, and `ProjectLike` provide
  the first async-ready project runtime vocabulary, service ownership bundle,
  and thin host bridge.
- `ProjectGpuServices` provides the optional project-facing GPU bridge for
  upload draining, upload ticket status, RGBA8 image readback tickets, and
  queue-idle waits. `GpuRuntime` owns deferred retirement by completed GPU
  submission ticket.
- `ImmediateCommands`, readback helpers, and PNG output are still synchronous
  where they wait for immediate GPU work or process completed pixels.

Expansion triggers:

- GPU readback/capture polling APIs beyond explicit drain/take handoff when an
  interactive workflow needs nonblocking completion.
- Broader fence/timeline integration when binary frame-slot completion stops
  being sufficient for asynchronous readback/capture readiness.
- Vulkan timeline-semaphore integration only when that completion pressure is
  measured.

Defer:

- Dedicated render thread.
- Parallel command recording.
- Split graphics/compute/transfer queue scheduling.
- Public task-graph dependency types.

### 11. Debugging And Instrumentation

Current state:

- Validation is easy to require from examples.
- CTest covers no-display boundary behavior plus headless PNG artifact and
  optional MP4 artifact creation.
- Shared CMake smoke helpers keep windowed no-display checks and headless
  capture validation consistent across examples.
- `GpuTimestampProfiler` provides optional per-pass GPU timestamp queries when
  the device exposes them, and the shared performance UI can display the latest
  timings.
- Renderer, render-graph, upload, and staged-resource paths expose optional
  caller-owned CPU/GPU metrics without changing their ownership or execution
  contracts.
- The isolated `dev-gltf-conformance` preset runs the pinned Khronos
  compatibility/conformance and headless evidence lane; the ordinary `dev`
  suite remains independent of fetched external assets.

Expansion triggers:

- Debug names and labels once marker scope becomes useful during capture or
  debugging.
- Additional timestamp/query integration only when a consumer needs GPU timing
  beyond the current `GpuTimestampProfiler` path.
- Screenshot/readback comparisons when deterministic visual comparison becomes
  a required validation workflow.

Defer:

- Heavy profiling UI.

## Historical Implementation Record

The batches below are retained as an implementation history. They describe
completed slices and are not a current backlog or authority for foundation
work. Use the layer-map `Current state`, `Expansion triggers`, and `Defer`
sections above, together with the [roadmap](../roadmap.md), for current
direction.

### Batch 1: Resource And Attachment Cleanup

Goal: normalize resource setup that is already repeated without changing the
example frame loop.

- Status: initial pass complete on `main`.
- Added shared depth format selection and `DepthAttachment` setup using
  `Image`.
- Added buffer copy and device-local buffer upload helpers around
  `ImmediateCommands`.
- Moved `spinning_cube` and `textured_cube` onto the shared depth attachment and
  buffer upload helpers.
- Kept command recording sequence and resize policy example-local.

Remaining resource work, especially image upload and readback, belongs in Batch
3 after descriptor and compute helper names prove themselves.

### Batch 2: Descriptor And Compute Setup

Goal: make `textured_cube`'s compute texture path less bespoke while preserving
explicit descriptor contracts.

- Status: initial pass complete on `main`.
- Added descriptor binding, pool-size, descriptor-write, and descriptor-update
  helpers for current uniform-buffer, storage-buffer, storage-image, and
  combined image sampler paths.
- Added descriptor set info and bundle helpers for the repeated single-set
  descriptor layout/pool/allocation path.
- Added pipeline-layout and compute-pipeline create-info helpers.
- Moved `textured_cube`'s graphics descriptors and setup-time compute texture
  path onto the shared helpers while keeping layout and dispatch choices
  explicit. `particle_cubes` and `textured_cube` now use descriptor bundles.

Remaining descriptor work should be driven by the next concrete resource path
rather than a general bind-group abstraction.

### Batch 3: Transfer, Texture, And Readback Path

Goal: support generated/uploaded textures and future headless artifacts.

- Status: initial pass complete on `main`.
- Added image transition helpers for storage, transfer destination, transfer
  sampled-image readback, and sampling paths.
- Added generated/uploaded sampled image config helpers and buffer-image copy
  region setup.
- Added buffer-to-image and image-to-buffer copy helpers.
- Added readback buffer config and host-visible coherent buffer download.

Batch 5 consumed these copy/readback pieces for the first explicit offscreen
render target and artifact-writing smoke.

### Batch 4: Frame Loop And Swapchain-Sized Resource Rebuild

Goal: reduce repeated resize/recreate code after the lower-level resources are
stable.

- Status: narrow pass complete on `main`.
- Added `SwapchainRecreateTracker` for the repeated consecutive recreate-attempt
  guard used by all current windowed examples.
- Moved windowed examples onto the shared tracker while keeping their
  swapchain-sized resource rebuild order explicit.
- Deferred a generic rebuild callback/coordinator because the actual resource
  creation/destruction order still differs by example.

This batch stayed platform-light. GLFW should now move into a separate host
layer rather than into `cubey::vulkan`.

### Batch 5: Headless Artifact Path

Goal: prove no-window rendering and artifact readback before abstracting the
host layer.

- Status: initial pass complete on `main`.
- Added `examples/headless_cube` as an explicit no-window target.
- Added `stb_image_write` for PNG output, keeping dependency wiring isolated
  from the Vulkan layer behind `cubey::write_png_rgba8`.
- Created an offscreen color target and rendered into it with dynamic rendering.
- Added the missing transition helper for the exact render-target readback path.
- Copied the image into a readback buffer and wrote a deterministic PNG artifact.
- Added CTest coverage for artifact creation in no-display terminal sessions.
- Follow-up extraction added `cubey::host::HeadlessCaptureHost` and migrated
  `headless_cube`, `fractal_2d --headless`, and `smoke_2d --headless` onto the
  shared no-GLFW host. `HeadlessPngHost` remains as the original source name.

Keep this batch intentionally small. It should pressure render-target/readback
vocabulary, not create a general host shell.

Implemented work loop:

1. Dependency slice: vendor `stb_image_write.h`, add a license note, and add a
   tiny PNG output wrapper with byte-level tests.
2. Vulkan helper slice: add the offscreen color image config and any explicitly
   named color-render-target readback transition helpers, with unit tests for the
   create-info and transition structs.
3. Example slice: add `examples/headless_cube` using a deterministic cube
   render. Keep it no-window and no-GLFW.
4. Artifact test slice: add CTest coverage that runs the example with validation
   enabled when available, writes into the build tree, and checks PNG signature.
5. Review checkpoint: complete for the cube-based PNG smoke.

### Batch 6: Fractal Project

Goal: prove fullscreen rendering and headless artifact reuse with a small
project.

- Status: initial pass complete on `main`.
- Added `projects/fractal_2d` with a fullscreen Mandelbrot-style fragment
  shader.
- Kept windowed setup, command recording sequence, and controls example-local.
- Added a headless PNG path through the shared no-GLFW host.
- Added project-local view math for drag pan, wheel zoom, reset, and push
  constants.
- Did not extract a fullscreen helper; the new code stayed clearer as explicit
  project code for now.

This is still project-local work. It should not create a reusable project
interface around setup, update, render, resize, or shutdown.

### Batch 6.5: Particle Cube Example

Goal: carry forward the original Cubey particle feel as an example and exercise
compute-to-graphics storage-buffer use without promoting a project host.

- Status: initial pass complete on `main`.
- Added `examples/particle_cubes` with deterministic attractor-style cube
  particles.
- Used compute to update a storage buffer and graphics to read the same buffer
  as indexed cube instances.
- Kept storage-buffer descriptor use as narrow reusable pressure from the
  example.
- Kept particle seeding, simulation constants, cube interpretation, command
  recording, and controls example-local.
- Deferred particle-system helpers, indirect draw, and host/engine host work.

This is still example work. A future particle system should move under
`projects/` only if it needs longer-lived state, headless capture, indirect
draw, asset/resource policy, or UI/runtime pressure.

### Batch 7: Threading And Async Runtime Boundary

Goal: prepare project code for non-stalling GPU workflows without introducing a
full threaded renderer.

- Status: initial runtime, queued-work, and staged-resource passes complete;
  see [threading and async design](threading-and-async.md).
- Added a small CPU job facade behind Cubey APIs.
- Added queued upload/capture/readback shapes and a strict threaded GPU owner.
- Kept examples direct while projects adopted the async-ready boundary.
- Added the first project runtime vocabulary and lifecycle concept.

Later staged-resource work extended this boundary with typed GPU results,
generation-safe activation, and deferred retirement without adding a full
threaded renderer.

### Batch 8: First Project And Runtime Pressure

Goal: let a real project define the host/engine seam.

- Start with fluid simulation, marching cubes, SDF sculpting, or a larger
  particle system only if it grows beyond the current example-sized attractor
  demo.
- Use the headless artifact path for deterministic smoke output.
- Extract shared lifecycle or host code only when both windowed and headless
  paths repeat the same shape in real project code.
- Keep examples as reference programs; move longer-lived creative work under
  `projects/`.

This is the point where a project interface around setup, update, render,
resize, and shutdown may become worthwhile.

Status: complete for the current project portfolio. Smoke, water, pyro,
atmosphere, ocean, terrain, Planet, and glTF/PBR projects now provide varied
windowed/headless, compute/render, staged-resource, and renderer-policy
pressure. That breadth has still not justified a generic project lifecycle
host; projects retain setup, simulation, render intent, and shutdown policy.

### Batch 9: Frame Overlap Runtime

Goal: make the frame-slot contract real before building higher-level render
systems on top.

- Status: initial pass complete on `main`.
- Added slot-based `FrameResources` with per-slot command buffers, acquire
  semaphores, and fences.
- Kept present-ready semaphores per swapchain image and added per-image
  in-flight fence tracking.
- Moved `RenderContext` onto active frame slots and advanced the slot ring after
  submit/present.
- Updated the windowed host to default to two frame slots and to pass the actual
  active slot through `WindowedRenderFrame`.

This batch keeps binary semaphores, one queue family, and one command pool.
Submission-ticket deferred destruction has since landed; timeline semaphores,
split queues, and parallel command recording remain separate slices.

### Batch 10: Transform v2 And Entity-Backed Managers

Goal: make transforms a durable foundation before scene/entity/renderable
systems depend on them.

- Status: transform value types are complete; the standalone hierarchy was
  superseded by entity-backed transform managers.
- `Transform2D` now uses `translation`, scalar-radian rotation, and `scale`;
  `Transform3D` now uses `translation`, quaternion `rotation`, `scale`, and an
  optional explicit affine matrix override for imported node matrices.
- Both transform types expose `affine_matrix()` instead of model-matrix
  terminology.
- `TransformManager2D` and `TransformManager3D` provide entity-backed transform
  components, parent links, cached local-to-world affine matrices, scene edit
  queues, epoch-local read-view snapshots, and strict validation for
  self-parenting, cycles, stale entities, and direct destruction of transforms
  with children.

### Batch 11: Multipass Shadow Foundation

Goal: establish the smallest useful foundation for multi-view, multi-pass
rendering without introducing a render graph.

- Status: initial pass complete on `main`.
- Added `FrameRenderPlan3D`, `RenderPassPlan3D`, and `RenderPassKind3D` as a
  CPU pass-list contract over existing `RenderFramePlan3D` values.
- Added `build_render_frame_plans_3d` for building multiple view plans from one
  committed `SceneReadView`.
- Centralized current queue submission create-info setup through
  `QueueSubmit`.
- Added sampled depth target support through `DepthTexture`,
  `DepthOnlyRenderingInfo`, shadow-friendly sampler config, and sampled-depth
  layout transitions.
- Added orthographic `Camera3D` support for light views.
- Added `examples/shadow_cube` as the reference: depth-only shadow pass into a
  sampled depth texture, then a color pass that samples it.

At this batch, render-graph scheduling, graph-derived barriers, shadow policy,
and renderer policy were intentionally deferred. The minimal render graph and
graph-derived declared-boundary synchronization have since landed, and
`ForwardPbrRenderer3D` now owns one reusable renderer-policy path. Shadow
atlases/cascades/filtering and general renderer-owned material/light policy
remain deferred.

## Current Recommendation

The foundation layers and the historical batches above are now implemented for
the current product scope. Further Vulkan or renderer abstraction should follow
measured consumer pressure: extend graph/resource setup only for a repeated
project need, and reopen glTF or staging work only from a measured product
requirement. Do not default to split queues, automatic scheduling, descriptor
abstraction, generalized streaming, or a generic project host.

# Vulkan Abstraction Map

This document maps the remaining Vulkan framework work for Cubey. It is a
planning guide, not a promise to abstract every Vulkan concept. Promote code
into `cubey::vulkan` only when it removes repeated setup, fixes a real lifetime
or synchronization hazard, or gives projects a clearer vocabulary without
hiding the constraints that matter.

## Direction

Cubey is Vulkan-first. The reusable layer should make examples and projects
shorter, but it should still feel like native Vulkan: resource ownership,
layout transitions, queue constraints, descriptor choices, and frame boundaries
must remain visible where they affect behavior.

The current boundary is:

- `cubey::vulkan` owns Vulkan object lifetime and common create-info
  construction.
- Examples and projects own rendering intent: shaders, meshes, descriptors,
  command recording, resize policy, and user interaction.
- Higher-level renderer, material, render-graph, and app-host concepts should
  wait until multiple projects create real pressure for them.

## Promotion Rules

- Prefer narrow RAII wrappers and create-info helpers before policy-heavy
  abstractions.
- Promote repeated code after at least two call sites or after one call site
  exposes a real correctness hazard.
- Keep synchronization and image layouts explicit unless the helper name makes
  the exact transition obvious.
- Keep GLFW/platform code out of the low-level Vulkan library. If it becomes
  reusable, split it into a platform/app layer rather than mixing it into
  `cubey::vulkan`.
- Keep threading and async work shaped by
  [threading and async design](threading-and-async.md): one GPU owner first,
  CPU jobs behind Cubey APIs, queued upload/capture requests, and explicit
  promotion gates for parallel command recording or split queues.
- Do not invent WebGPU-style vocabulary unless Cubey code has naturally arrived
  there. Descriptors may eventually look like bind groups, but the Vulkan
  ownership model comes first.

## Layer Map

### 1. Device, Queues, And Capabilities

Current state:

- `Instance` owns validation/debug-utils setup.
- `Device` owns physical-device selection, logical-device lifetime, one queue
  family, queue access, feature checks, and memory-type selection.
- `choose_depth_format` centralizes the current supported depth format probe.
- The current submission model assumes one GPU owner that serializes queue
  submission.

Needed next:

- Capability helpers for formats and optional features.
- Queue-family model that can represent split graphics, compute, and present
  queues.
- Submission coordinator only after queue submission paths repeat.

Defer:

- Sophisticated multi-GPU selection.
- Full queue abstraction, split queues, and timeline-semaphore scheduling until
  project evidence justifies them.

### 2. Frame And Presentation

Current state:

- `Swapchain` owns swapchain images and views.
- `FrameResources` owns one command buffer, acquire semaphore, per-image
  present semaphores, and fence.
- `RenderContext` owns the surface-backed `begin_frame` / `end_frame`
  acquire, submit, present, and recreate result path.
- `SwapchainRecreateTracker` owns bounded consecutive recreate-attempt tracking.

Needed next:

- N-frames-in-flight support after the single-frame path is stable.
- Frame tickets for deferred destruction and delayed readback/capture readiness.
- A way to rebuild swapchain-sized resources consistently.
- A reusable resize/recreate coordinator if example loops keep repeating.

Defer:

- Full app shell until we decide where platform ownership belongs.

### 3. Commands And Submission

Current state:

- `CommandPool` owns command-pool lifetime and primary command-buffer
  allocation.
- `ImmediateCommands` owns one-shot setup command submission.
- `begin_command_buffer` removes repeated begin boilerplate.
- `copy_buffer` and `upload_device_buffer` cover current setup-time transfers
  into device-local buffers.
- `copy_buffer_to_image` and `copy_image_to_buffer` cover current one-shot
  image transfer/readback copies.
- Command recording is single-threaded and owned by the current frame loop.

Needed next:

- One-shot compute/transfer helper vocabulary.
- Queue submit wrappers once more submission paths repeat.
- Per-frame/per-thread command-pool sharding before any parallel command
  recording.

Defer:

- Parallel command recording and secondary command buffers until profiling or a
  project proves command recording cost.
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
- Shared image/rendering helpers now cover the first offscreen color render
  target and color-attachment-to-readback transition used by headless output.
- Examples still own some resource policy, including when transfers and
  readback are used.
- Upload and capture behavior is still direct/blocking at the example level.

Needed next:

- Queue-shaped upload and capture requests that can execute synchronously at
  first, while keeping project code independent from blocking implementation
  details.
- Reuse the headless output path from a real project so repeated host shape is
  visible before deeper abstraction.

Defer:

- VMA or another allocator until manual memory allocation becomes the limiting
  cost.
- Asset import until generated resources no longer provide enough signal.

### 5. Descriptors And Bindings

Current state:

- `DescriptorSetLayout` and `DescriptorPool` own basic layout/pool lifetime.
- Descriptor allocation is owned by `DescriptorPool`.
- Descriptor helper functions cover layout bindings, pool sizes, uniform-buffer
  writes, storage-image writes, combined image sampler writes, and descriptor
  set updates.

Needed next:

- Sampled-image descriptor write helper if a sampled image without sampler gets
  a concrete use case.

Defer:

- A full bind-group abstraction until descriptor repetition stabilizes across
  more examples/projects.

### 6. Shaders And Pipelines

Current state:

- `ShaderModule` owns shader module lifetime.
- CMake compiles GLSL to SPIR-V with shared include support.
- `read_spirv_file` loads compiled SPIR-V bytecode into aligned 32-bit words
  for shader module creation.
- `PipelineLayout`, `GraphicsPipeline`, and `ComputePipeline` own pipeline
  lifetime.
- `DynamicGraphicsPipelineInfo` builds the current single-color-attachment
  dynamic graphics pipeline create-info shape.
- `PipelineLayoutInfo` builds pipeline layout create-info for descriptor set
  layouts and push constants.
- `ComputePipelineInfo` builds the current compute pipeline create-info shape.

Needed next:

- Optional graphics-state knobs only when examples need them.

Defer:

- Shader reflection, hot reload, pipeline cache, materials, and pipeline
  libraries until real projects create enough pressure.

### 7. Render Attachments And Render Targets

Current state:

- Dynamic rendering is the primary path.
- Rendering helpers cover current color/depth transitions and attachment-info
  construction.
- `DepthAttachment` covers the current reusable depth target ownership path.
- Rendering helpers also cover current storage-image, transfer-destination,
  sampled-image readback, color-attachment readback, and sampling transition
  paths.

Needed next:

- Render-target bundle for extent, format, color view, and optional depth.
- Clear/load/store options if examples stop clearing every frame.

Defer:

- Classic render pass abstraction.
- Render graph until multiple dependent passes make manual ordering painful.

### 8. Mesh, Texture, And Scene Convenience

Current state:

- Cube examples still define vertex data, index data, vertex descriptions, and
  upload behavior locally.

Needed later:

- Mesh upload helper for vertex/index buffers.
- Texture object/helper for generated or uploaded sampled images.
- Small geometry helpers only if repeated examples need the same primitives.

Defer:

- Scene graph, material metadata, glTF import, and asset database.

### 9. App And Project Runtime

Current state:

- Windowed examples own GLFW, surfaces, frame loop, input callbacks, resize
  policy, and command recording. The headless example owns its no-window
  render/readback loop.
- Shared non-platform helpers cover frame timing, frame stats, and orbit
  control.

Needed later:

- Project runtime vocabulary: setup, update, render packet, resize, shutdown.
- `ProjectContext` services for CPU jobs, uploads, capture requests, timing,
  and eventually UI hooks.
- Optional windowed host outside the low-level Vulkan layer.
- Headless host that can share project render code and write inspectable
  artifacts.

Defer:

- Pulling GLFW into `cubey::vulkan`.
- UI layer or ImGui until the render/runtime boundary is clearer.

### 10. Threading And Async

Current state:

- The design is captured in [threading and async design](threading-and-async.md).
- All current Vulkan work runs through direct example loops.
- `ImmediateCommands`, readback helpers, and PNG output are synchronous.

Needed next:

- `cubey::jobs` facade over a selected CPU executor.
- Deterministic inline executor for tests.
- Queue-shaped upload and capture APIs.
- Explicit GPU-owner vocabulary for serialized queue submission and GPU
  lifetime decisions.

Defer:

- Dedicated render thread.
- Parallel command recording.
- Split graphics/compute/transfer queue scheduling.
- Public task-graph dependency types.

### 11. Debugging And Instrumentation

Current state:

- Validation is easy to require from examples.
- CTest covers no-display boundary behavior and headless PNG artifact creation.
- Shared CMake smoke helpers keep windowed no-display checks and headless PNG
  validation consistent across examples.

Needed later:

- Debug names and labels.
- Timestamp queries and GPU timing.
- Screenshot/readback comparisons built on the headless output path.

Defer:

- Heavy profiling UI.

## Recommended Next Batches

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
- Kept command recording and resize policy example-local.

Remaining resource work, especially image upload and readback, belongs in Batch
3 after descriptor and compute helper names prove themselves.

### Batch 2: Descriptor And Compute Setup

Goal: make `textured_cube`'s compute texture path less bespoke while preserving
explicit descriptor contracts.

- Status: initial pass complete on `main`.
- Added descriptor binding, pool-size, descriptor-write, and descriptor-update
  helpers for current uniform-buffer, storage-image, and combined image sampler
  paths.
- Added pipeline-layout and compute-pipeline create-info helpers.
- Moved `textured_cube`'s graphics descriptors and setup-time compute texture
  path onto the shared helpers while keeping layout and dispatch choices
  explicit.

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

This batch should remain platform-light. GLFW should still live in examples or
in a separate future host layer.

### Batch 5: Headless Artifact Path

Goal: prove no-window rendering and artifact readback before abstracting the app
host.

- Status: initial pass complete on `main`.
- Added `examples/headless_render` as an explicit no-window target.
- Added `stb_image_write` for PNG output, keeping dependency wiring isolated
  from the Vulkan layer behind `cubey::write_png_rgba8`.
- Created an offscreen color target and rendered into it with dynamic rendering.
- Added the missing transition helper for the exact render-target readback path.
- Copied the image into a readback buffer and wrote a deterministic PNG artifact.
- Added CTest coverage for artifact creation in no-display terminal sessions.

Keep this batch intentionally small. It should pressure render-target/readback
vocabulary, not create a general app shell.

Implemented work loop:

1. Dependency slice: vendor `stb_image_write.h`, add a license note, and add a
   tiny PNG output wrapper with byte-level tests.
2. Vulkan helper slice: add the offscreen color image config and any explicitly
   named color-render-target readback transition helpers, with unit tests for the
   create-info and transition structs.
3. Example slice: add `examples/headless_render` using a deterministic clear
   first. Keep it no-window and no-GLFW.
4. Artifact test slice: add CTest coverage that runs the example with validation
   enabled when available, writes into the build tree, and checks PNG signature.
5. Review checkpoint: complete for the clear-based PNG smoke.

### Batch 6: Fractal Example

Goal: prove fullscreen rendering and headless artifact reuse with an example,
not a project runtime.

- Status: initial pass complete on `main`.
- Added `examples/fractal` with a fullscreen Mandelbrot-style fragment shader.
- Kept windowed setup, command recording, and controls example-local.
- Added a headless PNG path through the existing offscreen render-target/readback
  helpers.
- Added example-local view math for drag pan, wheel zoom, reset, and push
  constants.
- Did not extract a fullscreen helper; the new code stayed clearer as explicit
  example code for now.

This is still example work. It should not create a project interface around
setup, update, render, resize, or shutdown.

### Batch 7: Threading And Async Runtime Boundary

Goal: prepare project code for non-stalling GPU workflows without introducing a
full threaded renderer.

- Status: design captured in [threading and async design](threading-and-async.md).
- Add a small CPU job facade behind Cubey APIs.
- Decide whether Taskflow or `BS::thread_pool` best fits the first slice.
- Introduce queued upload/capture/readback shapes, initially processed
  synchronously by the GPU owner.
- Keep examples direct; make the first project use the async-ready boundary.

This batch should create design pressure before the first real project grows
around blocking helper calls.

### Batch 8: First Project And Runtime Pressure

Goal: let a real project define the app/runtime seam.

- Start with particles, fluid simulation, or marching cubes after the
  async-ready runtime boundary has a first pass.
- Use the headless artifact path for deterministic smoke output.
- Extract shared lifecycle or host code only when both windowed and headless
  paths repeat the same shape in real project code.
- Keep examples as reference programs; move longer-lived creative work under
  `projects/`.

This is the point where a project interface around setup, update, render,
resize, and shutdown may become worthwhile.

## Near-Term Recommendation

Batch 1 through Batch 6 have their first passes on `main`. Start Batch 7 next:
the threading/async runtime boundary. Then use Batch 8, the first real project,
to decide how much app/runtime host to extract.

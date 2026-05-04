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
- Do not invent WebGPU-style vocabulary unless Cubey code has naturally arrived
  there. Descriptors may eventually look like bind groups, but the Vulkan
  ownership model comes first.

## Layer Map

### 1. Device, Queues, And Capabilities

Current state:

- `Instance` owns validation/debug-utils setup.
- `Device` owns physical-device selection, logical-device lifetime, one queue
  family, queue access, feature checks, and memory-type selection.

Needed next:

- Capability helpers for formats and optional features.
- Depth format selection helper.
- Queue-family model that can represent split graphics, compute, and present
  queues.

Defer:

- Sophisticated multi-GPU selection.
- Full queue abstraction until split queues are implemented.

### 2. Frame And Presentation

Current state:

- `Swapchain` owns swapchain images and views.
- `FrameResources` owns one command buffer, acquire semaphore, per-image
  present semaphores, and fence.
- `RenderContext` owns the surface-backed `begin_frame` / `end_frame`
  acquire, submit, present, and recreate result path.

Needed next:

- N-frames-in-flight support after the single-frame path is stable.
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

Needed next:

- Copy helpers for buffer-to-buffer and buffer-to-image transfers.
- One-shot compute/transfer helper vocabulary.
- Queue submit wrappers once more submission paths repeat.

Defer:

- A general queue class until split queue families force the shape.

### 4. Resources And Memory

Current state:

- `Buffer`, `Image`, and `Sampler` own basic Vulkan resource lifetime.
- Examples still own some resource policy, including depth attachment setup,
  staging upload patterns, texture transitions, and readback absence.

Needed next:

- Depth attachment helper using `Image`.
- Staging upload helpers for buffers and images.
- Texture creation and transition helpers for generated and sampled images.
- Readback path for future headless image output and tests.

Defer:

- VMA or another allocator until manual memory allocation becomes the limiting
  cost.
- Asset import until generated resources no longer provide enough signal.

### 5. Descriptors And Bindings

Current state:

- `DescriptorSetLayout` and `DescriptorPool` own basic layout/pool lifetime.
- Descriptor writes and allocation are still mostly example-local.

Needed next:

- Descriptor set allocation helper.
- Descriptor write helpers for uniform buffers, storage images, sampled
  images, and combined image samplers.
- Pipeline layout helper that assembles descriptor set layouts and push
  constants without hiding the layout contract.

Defer:

- A full bind-group abstraction until descriptor repetition stabilizes across
  more examples/projects.

### 6. Shaders And Pipelines

Current state:

- `ShaderModule` owns shader module lifetime.
- CMake compiles GLSL to SPIR-V with shared include support.
- `PipelineLayout`, `GraphicsPipeline`, and `ComputePipeline` own pipeline
  lifetime.
- `DynamicGraphicsPipelineInfo` builds the current single-color-attachment
  dynamic graphics pipeline create-info shape.

Needed next:

- Compute pipeline create-info helper.
- Pipeline layout helper for descriptor sets and push constants.
- Optional graphics-state knobs only when examples need them.

Defer:

- Shader reflection, hot reload, pipeline cache, materials, and pipeline
  libraries until real projects create enough pressure.

### 7. Render Attachments And Render Targets

Current state:

- Dynamic rendering is the primary path.
- Rendering helpers cover current color/depth transitions and attachment-info
  construction.

Needed next:

- Depth attachment helper.
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

- Examples own GLFW, surfaces, frame loop, input callbacks, resize policy, and
  command recording.
- Shared non-platform helpers cover frame timing, frame stats, and orbit
  control.

Needed later:

- Project runtime vocabulary: setup, update, render, resize, shutdown.
- Optional windowed host outside the low-level Vulkan layer.
- Headless host that can share project render code and write inspectable
  artifacts.

Defer:

- Pulling GLFW into `cubey::vulkan`.
- UI layer or ImGui until the render/runtime boundary is clearer.

### 10. Debugging And Instrumentation

Current state:

- Validation is easy to require from examples.
- CTest covers no-display boundary behavior.

Needed later:

- Debug names and labels.
- Timestamp queries and GPU timing.
- Screenshot/readback comparisons once headless output exists.

Defer:

- Heavy profiling UI.

## Recommended Next Batches

### Batch 1: Resource And Attachment Cleanup

Goal: normalize resource setup that is already repeated without changing the
example frame loop.

- Add depth format selection.
- Add depth attachment setup using `Image`.
- Add buffer copy and buffer upload helpers around `ImmediateCommands`.
- Move `spinning_cube` depth ownership to `Image`, matching `textured_cube`.
- Keep command recording and resize policy example-local.

This batch is the lowest-risk next step because it removes repeated resource
code and resolves the mismatch between the two cube examples.

### Batch 2: Descriptor And Compute Setup

Goal: make `textured_cube`'s compute texture path less bespoke while preserving
explicit descriptor contracts.

- Add descriptor allocation/write helpers.
- Add compute pipeline create-info helper.
- Add pipeline layout helper for descriptor set layouts and push constants.
- Migrate the setup-time compute texture path.

This batch should happen before adding more compute examples, so the next
project does not copy a one-off descriptor and compute setup block.

### Batch 3: Transfer, Texture, And Readback Path

Goal: support generated/uploaded textures and future headless artifacts.

- Add image transition helpers for storage, transfer, sampling, and readback
  paths.
- Add buffer-to-image copy helper.
- Add readback buffer/image path.
- Use the readback path for a first inspectable smoke artifact when headless
  work resumes.

This batch bridges visible window demos and automated graphics verification.

### Batch 4: Frame Loop And Swapchain-Sized Resource Rebuild

Goal: reduce repeated resize/recreate code after the lower-level resources are
stable.

- Add a helper for rebuilding swapchain-sized resources.
- Consider N-frames-in-flight.
- Consider a small resize/recreate coordinator if all examples still carry the
  same loop shape.

This batch should remain platform-light. GLFW should still live in examples or
in a separate future host layer.

### Batch 5: Project Runtime And Headless Host

Goal: make real projects concise and testable.

- Define a small project interface around setup, update, render, resize, and
  shutdown.
- Add a windowed host if the example loops have clearly converged.
- Add a headless host that can share project render code and produce output
  artifacts.

This is the point where `examples/` can stay minimal and `projects/` can become
first-class experiments.

## Near-Term Recommendation

Start with Batch 1, then Batch 2. Together they complete the resource,
attachment, descriptor, and compute setup foundation needed before returning to
headless output or starting the first real project.

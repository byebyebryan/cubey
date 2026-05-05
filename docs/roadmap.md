# Roadmap

This is the living roadmap for Cubey 2.0. It should reflect the current plan,
not a frozen promise. When implementation changes the plan, update this file
before the old assumptions become tribal knowledge.

## Current Direction

Cubey 2.0 is a native Vulkan-first GPU workbench for procedural graphics
experiments. WebGPU/Dawn remains useful as an optional future presentation path,
but it is not the architecture driver for the main Vulkan layer.

See [Vulkan abstraction map](vulkan-abstractions.md) for the planned framework
layers, promotion rules, and next implementation batches.

## Phase 0: Repo Foundation

Status: complete for the first implementation slice.

Goal: make `main` a clean place to build from before porting spike code.

- CMake presets for development, release, sanitizer, and clang-tidy builds.
- Formatting, linting, editor, Git text-normalization, and warning defaults.
- License, changelog/release-note source, roadmap, and working notes.
- C++ style guide for formatting, naming, ownership, and Vulkan structure.
- Explicit monorepo shape: `cubey` as the primary library, `examples/` for
  focused reference programs, and `projects/` for first-class graphics work.

Exit criteria:

- A new contributor or future agent can configure, build, and understand the
  project direction from the repo docs.
- Empty-project build and tooling presets work cleanly.

## Phase 1: Vulkan Runtime Skeleton

Status: in progress.

Goal: reshape the successful Vulkan spike into maintainable mainline modules.

- Primary `cubey` static library with public headers under `include/cubey/`.
- First runnable example under `examples/window_clear/`, not a generic `cubey`
  executable.
- CLI/config surface for windowed and headless modes.
- GLFW window creation and Vulkan surface ownership.
- Vulkan instance, physical device, logical device, queues, validation layers,
  and debug messenger setup.
- Swapchain acquisition, presentation, out-of-date handling, and compositor
  resize recovery.
- Frame resources: command buffers, semaphores, fences, and future
  N-frames-in-flight.
- Build-time GLSL to SPIR-V shader flow.

Exit criteria:

- `cubey` builds as the primary library target and examples link against it.
- `window_clear` opens a window and renders a simple GPU result.
- Headless smoke renders and writes an inspectable image.
- Validation-layer smoke can be required from the command line.
- Resize and swapchain recreation remain first-class tested behavior.

Current checkpoint:

- `cubey` static library target exists.
- Reusable `cubey::vulkan::Instance`, `Device`, `Buffer`, `Image`, `Sampler`,
  `Swapchain`, `CommandPool`, and `FrameResources` components now own
  validation/debug-utils setup,
  physical-device selection, logical-device lifetime, queue access, swapchain
  image/view ownership, buffer/image allocation, sampler ownership,
  command-pool ownership, command-buffer allocation, and single-frame
  command/sync resources.
- Reusable `cubey::FrameClock` and `cubey::OrbitController` cover basic frame
  timing and mouse-driven view control.
- Reusable `cubey::FrameStats` covers lightweight FPS/frame-time/extent/triangle
  telemetry formatting for windowed examples.
- Reusable `cubey::vulkan::ShaderModule` exists, and CMake can compile GLSL to
  SPIR-V with `glslangValidator` for example targets, including shared include
  directories and dependency tracking.
- Reusable `cubey::vulkan::ImmediateCommands` plus buffer helpers support
  one-shot setup uploads into device-local buffers.
- Reusable `cubey::vulkan::DepthAttachment` owns depth image/view setup and
  shared depth format selection.
- Reusable `cubey::vulkan` transfer/readback helpers cover readback buffers,
  sampled image configs, buffer-image copy regions, buffer-to-image copies,
  image-to-buffer copies, and current storage/transfer/sampling image layout
  transitions.
- Reusable `cubey::vulkan::PipelineLayout`, `GraphicsPipeline`,
  `ComputePipeline`, `DescriptorSetLayout`, and `DescriptorPool` own basic
  pipeline and descriptor lifetimes while still taking raw Vulkan create-info
  structs.
- Reusable `cubey::vulkan` descriptor helpers build layout bindings, pool
  sizes, descriptor writes, and descriptor updates for current uniform-buffer,
  storage-image, and combined image sampler paths.
- Reusable `cubey::vulkan::PipelineLayoutInfo` and `ComputePipelineInfo` build
  the current pipeline-layout and compute-pipeline create-info shapes.
- Reusable `cubey::vulkan::DynamicGraphicsPipelineInfo` builds the current
  dynamic-rendering graphics pipeline create-info shape for one color
  attachment plus optional depth, while examples still choose shaders, layouts,
  vertex input, descriptors, and depth usage explicitly.
- Reusable `cubey::vulkan` rendering helpers build the current color/depth
  attachment transitions and dynamic-rendering attachment descriptors without
  owning render policy.
- Reusable `cubey::vulkan::RenderContext` exposes explicit `begin_frame` and
  `end_frame` calls for the common surface-backed acquire, command reset,
  submit, present, and out-of-date result path used by all current windowed
  examples.
- `examples/window_clear` links against `cubey` and clears/presents a swapchain
  image through Vulkan/GLFW using dynamic rendering.
- `examples/triangle` links against `cubey`, compiles vertex/fragment shaders
  at build time, creates a dynamic-rendering graphics pipeline through the
  shared pipeline helper, and draws a `gl_VertexIndex` triangle without a render
  pass or framebuffer.
- `examples/spinning_cube` links against `cubey`, compiles vertex/fragment
  shaders at build time, draws an indexed cube from device-local vertex/index
  buffers, updates an MVP matrix through push constants, and uses dynamic
  rendering with a shared depth attachment helper.
- `examples/textured_cube` links against `cubey`, generates a texture with a
  compute shader writing a storage image, transitions it for shader sampling,
  binds scene uniforms plus a combined image sampler descriptor through shared
  descriptor/compute helpers, and draws an interactive shaded textured indexed
  cube through dynamic rendering with per-face normals and shared GLSL Lambert
  lighting.
- The current device model intentionally selects one queue family for required
  graphics, compute, and present capabilities. Split queue-family support is a
  future framework slice, not part of the current example-local compute path.
- The remaining windowed app implementation is intentionally example-local: GLFW
  window setup, surface creation, pipeline layout choices, command recording,
  and resize policy.
- Dev CTest covers the target in both graphical and no-display terminal
  sessions.
- Headless rendering, frame overlap, split graphics/compute/present queue-family
  support, and external asset loading remain future slices.

Next implementation batch: frame-loop and swapchain-sized resource rebuild
cleanup, especially shared rebuild helpers if the repeated example code is clear
enough to promote.

## Phase 2: Resource Layer and App API

Goal: make examples and projects concise without hiding important Vulkan
constraints.

- RAII wrappers for buffers, images, image views, shader modules, pipelines, and
  frame-owned synchronization.
- Staging upload and readback paths for meshes, textures, uniforms, and compute
  outputs.
- A small app interface for setup, update, render, UI, and input hooks.
- Practical rendering vocabulary for buffers, textures, pipelines, bind groups,
  dispatch, draw, submit, present, and readback.

Exit criteria:

- Project code can focus on shader/data behavior instead of raw setup boilerplate.
- Vulkan synchronization and image layout requirements remain visible where
  they matter.
- Headless and windowed runs share the same project code path where practical.

## Phase 3: First Real Project

Goal: prove the framework with one non-trivial procedural graphics project.

Candidate projects:

- Fractal renderer for the fastest end-to-end visual loop.
- GPU particle system for compute plus graphics pipeline pressure.
- Fluid simulation rewrite for the strongest connection to the original cubey.

Exit criteria:

- The project runs interactively with a window.
- The same project can run headlessly for a fixed number of frames and produce a
  deterministic output artifact.
- README contains the exact commands for local smoke testing.

## Later

- ImGui debug controls.
- Orbit camera and common interaction helpers.
- Ports of original cubey projects: fluid simulation, particles, marching cubes,
  fractals, and camera/shadow tests.
- SDF sculpting experiments from `projectR` if the resource model holds up.
- WebGPU/browser revisit only after a concrete browser-facing project earns the
  extra shader and platform surface area.

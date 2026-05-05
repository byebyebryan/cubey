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

## Phase 1: Windowed Vulkan Runtime Skeleton

Status: windowed framework checkpoint complete; headless output remains open.

Goal: reshape the successful Vulkan spike into maintainable mainline modules
and prove visible desktop rendering without turning the library into a generic
app engine.

- Primary `cubey` static library with public headers under `include/cubey/`.
- First runnable example under `examples/window_clear/`, not a generic `cubey`
  executable.
- CLI/config surface for windowed examples and future headless modes.
- GLFW window creation and Vulkan surface ownership.
- Vulkan instance, physical device, logical device, queues, validation layers,
  and debug messenger setup.
- Swapchain acquisition, presentation, out-of-date handling, and compositor
  resize recovery.
- Frame resources: command buffers, semaphores, fences, and the current
  single-frame synchronization path.
- Build-time GLSL to SPIR-V shader flow.

Completed criteria:

- `cubey` builds as the primary library target and examples link against it.
- `window_clear`, `triangle`, `spinning_cube`, and `textured_cube` open windows
  and render through dynamic rendering.
- Validation-layer smoke can be required from the command line.
- Resize and swapchain recreation remain first-class tested behavior.

Open criteria:

- Headless smoke renders and writes an inspectable image.
- N-frames-in-flight remains a future optimization, not a blocker for the first
  visible framework checkpoint.

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
  image-to-buffer copies, and current storage, transfer, sampled-image readback,
  and sampling image layout transitions.
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
- Reusable `cubey::vulkan::SwapchainRecreateTracker` guards repeated
  out-of-date/suboptimal recreate loops across all current windowed examples.
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
  window setup, surface creation, swapchain-sized resource rebuild order,
  pipeline layout choices, command recording, and resize policy.
- Dev CTest covers the target in both graphical and no-display terminal
  sessions.
- Headless rendering, frame overlap, split graphics/compute/present queue-family
  support, and external asset loading remain future slices.

Alignment: the windowed Vulkan layer is now far enough along that the next
framework signal should come from a no-window artifact path, not another
windowed example and not a broad app/runtime layer. The remaining rebuild code is
still example-specific enough that a broader host should wait for either
headless output or the first real project.

## Phase 2: Headless Output And Runtime Boundary

Status: next.

Goal: prove that Cubey can produce inspectable GPU artifacts without a desktop
surface while keeping the runtime boundary concrete.

- Add a minimal `examples/headless_render` target or equivalent explicit
  no-window smoke.
- Add `stb_image_write` as a small third-party dependency for PNG artifact
  output.
- Create an offscreen color target through the existing Vulkan resource model.
- Add only the layout transitions and copy helpers needed for that concrete
  render-target readback path.
- Write a simple deterministic PNG artifact.
- Add CTest coverage for the no-display success path, including output-file
  existence and basic PNG signature/size checks.
- Keep `window_clear`, `triangle`, `spinning_cube`, and `textured_cube`
  windowed-example loops explicit unless the headless path reveals reusable host
  shape.

Exit criteria:

- A no-window command can run with validation enabled and produce an inspectable
  PNG artifact in a terminal session.
- The offscreen path reuses `cubey` resource/readback helpers and exposes any
  new layout assumptions by name.
- The docs identify what still belongs to examples versus a future runtime host.

## Phase 3: First Real Project

Status: after the headless artifact path.

Goal: prove the framework with one non-trivial procedural graphics project and
let repeated project needs shape the app/runtime API.

Candidate projects:

- Fractal renderer for the fastest end-to-end visual loop and the simplest
  headless/windowed comparison.
- GPU particle system for compute plus graphics pipeline pressure.
- Fluid simulation rewrite for the strongest connection to the original cubey,
  after the runtime has more evidence.

Exit criteria:

- The project runs interactively with a window.
- The same project can run headlessly for a fixed number of frames and produce a
  deterministic output artifact.
- README contains the exact commands for local smoke testing.

## Phase 4: Runtime Extraction

Status: defer until Phase 2 and Phase 3 provide real pressure.

Goal: extract only the host concepts that repeated windowed/headless project code
has proven useful.

- Project lifecycle vocabulary: setup, update, render, resize, shutdown.
- Optional windowed host outside `cubey::vulkan`.
- Optional headless host that shares project render code where practical.
- Input/UI hooks only after a project needs them.

Exit criteria:

- At least one real project gets shorter without hiding Vulkan synchronization,
  layout, descriptor, or resize constraints.
- Examples remain useful as small reference programs rather than becoming hidden
  framework tests.

## Later

- ImGui debug controls.
- Orbit camera and common interaction helpers.
- Ports of original cubey projects: fluid simulation, particles, marching cubes,
  fractals, and camera/shadow tests.
- SDF sculpting experiments from `projectR` if the resource model holds up.
- WebGPU/browser revisit only after a concrete browser-facing project earns the
  extra shader and platform surface area.

# Roadmap

This is the living roadmap for Cubey 2.0. It should reflect the current plan,
not a frozen promise. When implementation changes the plan, update this file
before the old assumptions become tribal knowledge.

## Current Direction

Cubey 2.0 is a native Vulkan desktop GPU workbench for procedural graphics
experiments. Vulkan is the architecture driver for the shared runtime and
graphics foundation.

See [Vulkan abstraction map](vulkan-abstractions.md) for the planned framework
layers and foundation rules, [renderer foundation](renderer-foundation.md) for
the first `cubey::render` contracts above Vulkan, [app runtime](app-runtime.md)
for the GLFW/windowed host extraction path, and
[threading and async design](threading-and-async.md) for the async-ready runtime
boundary.

## Phase 0: Repo Foundation

Status: complete for the first implementation slice.

Goal: make `main` a clean place to build from before building the new native
runtime foundation.

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

Status: windowed framework checkpoint and first headless artifact checkpoint
complete; frame overlap remains open.

Goal: build maintainable mainline Vulkan modules and prove visible desktop
rendering without turning the library into a generic app engine.

- Primary `cubey` static library with public headers under `include/cubey/`.
- First runnable example under `examples/window_clear/`, not a generic `cubey`
  executable.
- CLI/config surface for windowed examples and future headless modes.
- GLFW window creation and Vulkan surface ownership.
- Vulkan instance, physical device, logical device, queues, validation layers,
  and debug messenger setup.
- Swapchain acquisition, presentation, out-of-date handling, and compositor
  resize recovery.
- Frame resources: command buffers, semaphores, fences, and the frame-slot
  synchronization path.
- Build-time GLSL to SPIR-V shader flow.

Completed criteria:

- `cubey` builds as the primary library target and examples link against it.
- `window_clear`, `triangle`, `spinning_cube`, `textured_cube`, `fractal`, and
  `particles` open windows and render through dynamic rendering.
- Validation-layer smoke can be required from the command line.
- Resize and swapchain recreation remain first-class tested behavior.
- Headless smoke renders and writes an inspectable PNG.

Open criteria:

- Frame overlap is now part of the runtime foundation. Timeline semaphores,
  split queues, and parallel command recording remain future optimizations.

Current checkpoint:

- `cubey` static library target exists.
- Reusable `cubey::vulkan::Instance`, `Device`, `Buffer`, `Image`, `Sampler`,
  `Swapchain`, `CommandPool`, and `FrameResources` components now own
  validation/debug-utils setup,
  physical-device selection, logical-device lifetime, queue access, swapchain
  image/view ownership, buffer/image allocation, sampler ownership,
  command-pool ownership, command-buffer allocation, per-frame-slot
  command/sync resources, and per-image present synchronization.
- Reusable `cubey::FrameClock`, `cubey::Transform2D`, `cubey::Transform3D`,
  `cubey::TransformHierarchy2D`, `cubey::TransformHierarchy3D`,
  `cubey::Camera2D`, `cubey::OrbitCamera3D`, and `cubey::OrbitController`
  cover basic frame timing, explicit 2D/3D affine transform boundaries,
  quaternion-backed 3D rotation, cached parent/child world transforms, shared
  2D/3D camera state, and mouse-driven orbit input.
- Reusable `cubey::input::InputState`/`InputFrame` provide per-frame keyboard
  and mouse polling over the GLFW callback stream, with shared pointer-drag,
  camera-backed 2D pan/zoom, and input-aware orbit-control helpers.
- Reusable `cubey::FrameStats` covers lightweight FPS/frame-time/extent/triangle
  telemetry formatting for windowed examples.
- Reusable `cubey::math` wraps GLM matrix/vector/quaternion types and the
  current Vulkan transform/projection conventions used by the shared
  transform/camera helpers and cube examples.
- Reusable `cubey::vulkan::ShaderModule` exists, and CMake can compile GLSL to
  SPIR-V with `glslangValidator` for example targets, including shared include
  directories and dependency tracking. Reusable `cubey::read_spirv_file` loads
  compiled shader bytecode through the `spirv_io` layer for shader-backed
  examples and future projects.
- Reusable `cubey::vulkan::ImmediateCommands` plus buffer helpers support
  one-shot setup uploads into device-local buffers.
- Reusable `cubey::vulkan::DepthAttachment` owns depth image/view setup and
  shared depth format selection.
- Reusable `cubey::vulkan` transfer/readback helpers cover readback buffers,
  sampled image configs, buffer-image copy regions, buffer-to-image copies,
  image-to-buffer copies, and current storage, transfer, sampled-image readback,
  and sampling image layout transitions.
  The headless path also uses an explicit offscreen color render-target config
  and a color-attachment-to-readback transition.
- Reusable `cubey::read_binary_file` and `cubey::write_binary_file` cover
  generic byte I/O; `cubey::write_png_rgba8` wraps the vendored
  `stb_image_write` PNG path behind the `image_io` API.
- Reusable `cubey::jobs::JobSystem`, `InlineExecutor`, and `JobHandle` provide
  the first CPU job facade behind Cubey APIs without exposing a third-party
  executor.
- Reusable `cubey::CaptureQueue` and `CaptureTicket` provide the first
  async-shaped PNG encoding path over completed RGBA pixel buffers.
- Reusable `cubey::UploadQueue`, `UploadTicket`, and `QueuedUpload` provide the
  first CPU-owned upload request queue for future GPU-owner draining.
- Reusable `cubey::FrameTicketIssuer`, `FrameTicket`, and
  `DeferredDestructionQueue` provide the first frame-ticket retirement
  vocabulary.
- Reusable `cubey::ProjectContext`, `ProjectFrame`, `ProjectExtent`,
  `RenderPacket`, `ProjectRuntimeServices`, `ProjectRuntimeAdapter`, and
  `ProjectLike` provide the first async-ready project runtime vocabulary,
  service ownership bundle, and thin host bridge.
- Reusable `cubey::render` target, texture, mesh, and draw-item contracts now
  sit above `cubey::vulkan`: windowed/headless color target views,
  dynamic-rendering target setup, generated/uploaded sampled texture ownership,
  indexed mesh upload, minimal indexed draw recording, explicit frame-slot
  identity, and per-frame uniform buffers.
- Reusable `cubey::vulkan::PipelineLayout`, `GraphicsPipeline`,
  `ComputePipeline`, `DescriptorSetLayout`, and `DescriptorPool` own basic
  pipeline and descriptor lifetimes while still taking raw Vulkan create-info
  structs.
- Reusable `cubey::vulkan` descriptor helpers build layout bindings, pool
  sizes, descriptor writes, and descriptor updates for current uniform-buffer,
  storage-buffer, storage-image, and combined image sampler paths.
- Reusable `cubey::vulkan::DescriptorSetInfo` and `DescriptorSetBundle` cover
  the current one-layout/one-pool/one-set descriptor shape used by examples.
- Reusable `cubey::vulkan::DescriptorSetArray` covers the same owned
  layout/pool shape for multiple sets, primarily for per-frame bindings.
- Reusable `cubey::vulkan::PipelineLayoutInfo` and `ComputePipelineInfo` build
  the current pipeline-layout and compute-pipeline create-info shapes.
- Reusable `cubey::vulkan::DynamicGraphicsPipelineInfo` builds the current
  dynamic-rendering graphics pipeline create-info shape for one color
  attachment plus optional depth and blending, while examples still choose
  shaders, layouts, vertex input, descriptors, and depth usage explicitly.
- Reusable `cubey::vulkan` image-transition helpers build the current
  color/depth/storage/transfer layout transitions, while dynamic-rendering
  helpers build color/depth attachment descriptors without owning render
  policy.
- Reusable `cubey::vulkan::RenderContext` exposes explicit `begin_frame` and
  `end_frame` calls for the common surface-backed acquire, command reset,
  submit, present, frame-slot advance, per-image in-flight fence wait, and
  out-of-date result path used by all current windowed examples.
- Reusable `cubey::vulkan::SwapchainRecreateTracker` guards repeated
  out-of-date/suboptimal recreate loops across all current windowed examples.
- Optional `cubey_app` target owns the first GLFW-backed app/runtime layer:
  window lifetime, surface creation/destruction, key/pointer dispatch, windowed
  frame loop, frame timing, optional frame stats, and swapchain recreate
  orchestration. Render callbacks receive `WindowedRenderFrame`, including the
  active frame slot and swapchain color target view. The windowed host defaults
  to two frame slots.
- `examples/window_clear` links against `cubey` and clears/presents a swapchain
  image through the shared app host using dynamic rendering.
- `examples/triangle` links against `cubey`, compiles vertex/fragment shaders
  at build time, creates a dynamic-rendering graphics pipeline through the
  shared pipeline helper, and draws a `gl_VertexIndex` triangle without a render
  pass or framebuffer through the shared app host.
- `examples/spinning_cube` links against `cubey`, compiles vertex/fragment
  shaders at build time, draws an indexed cube through `cubey::render::Mesh`
  and `DrawItem`, updates an MVP matrix from shared transform, camera, and math
  helpers through push constants, and uses dynamic rendering with a shared depth
  attachment helper through the shared app host.
- `examples/textured_cube` links against `cubey`, generates a `Texture2D` with a
  compute shader writing a storage image, transitions it for shader sampling,
  binds per-frame scene uniforms plus a combined image sampler descriptor
  through shared descriptor/compute helpers, and draws an interactive shaded textured indexed
  cube through `cubey::render::Mesh`, `DrawItem`, dynamic rendering, per-face
  normals, shared GLSL Lambert lighting, shared transform/model matrices,
  shared camera projection, and shared math helpers through the shared app host.
- `examples/headless_render` links against `cubey`, creates no GLFW window or
  surface, renders an offscreen color target through dynamic rendering, copies
  it into a readback buffer, and writes a PNG artifact.
- `examples/fractal` links against `cubey`, renders a fullscreen
  Mandelbrot-style fragment shader, supports camera-backed pan/zoom/reset
  navigation through the shared app host, and reuses the shared headless PNG
  host for no-window output.
- `examples/particles` links against `cubey`, updates a storage-buffer particle
  field with a per-frame compute shader, inserts an explicit compute-to-vertex
  memory barrier, and renders the result as instanced screen-facing quads with
  additive Gaussian splats through the shared app host.
- The current device model intentionally selects one queue family for required
  graphics, compute, and present capabilities. Split queue-family support is a
  future framework slice, not part of the current example-local compute path.
- Windowed example host mechanics now live in `cubey_app`; swapchain-sized
  render resources, pipeline layout choices, and command recording remain
  example-local.
- Dev CTest covers the target in graphical, no-display terminal, and headless
  artifact sessions through shared windowed and PNG smoke helpers.
- Split graphics/compute/present queue-family support, GPU capture polling,
  timeline semaphores, parallel command recording, and external asset loading
  remain future slices.

Alignment: the Vulkan layer now has visible windowed examples plus a minimal
headless PNG path. Cubey has the first async-ready runtime vocabulary: CPU jobs
behind Cubey APIs, queued upload/capture requests, frame tickets, deferred
cleanup, and project lifecycle concepts. Larger systems such as a threaded
renderer, split queues, or parallel command recording should be designed from
clear contracts and established graphics precedent before implementation; they
do not need duplicated project code as a prerequisite, but they must stay
narrow and testable.

## Phase 2: Headless Output And Runtime Boundary

Status: initial pass complete.

Goal: prove that Cubey can produce inspectable GPU artifacts without a desktop
surface while keeping the runtime boundary concrete.

- Added `examples/headless_render` as an explicit no-window smoke.
- Added `stb_image_write` as a small third-party dependency for PNG artifact
  output.
- Created an offscreen color target through the existing Vulkan resource model.
- Added only the layout transitions and copy helpers needed for that concrete
  render-target readback path.
- Wrote a simple deterministic PNG artifact.
- Added CTest coverage for the no-display success path, including output-file
  existence and PNG signature checks.
- Follow-up extraction added the shared no-GLFW `HeadlessPngHost` once repeated
  examples and the first project revealed a concrete reusable shape.

Exit criteria:

- A no-window command can run with validation enabled and produce an inspectable
  PNG artifact in a terminal session.
- The offscreen path reuses `cubey` resource/readback helpers and exposes any
  new layout assumptions by name.
- The docs identify what belongs to examples/projects versus reusable host
  plumbing.

## Phase 3: Fractal Example

Status: initial pass complete.

Goal: add a small fractal example that proves fullscreen rendering and reuses
the headless artifact path without pretending to justify a project runtime.

- Added `examples/fractal` as a fullscreen Mandelbrot-style shader smoke.
- Kept windowed setup, command recording, and navigation example-local.
- Reused existing dynamic graphics pipeline helpers.
- Reused the shared headless PNG host for deterministic output.
- Did not promote fullscreen helpers; no durable contract was clearer than the
  current explicit example code yet.

Exit criteria:

- The example runs interactively with a window.
- The same example can run headlessly and produce a deterministic PNG artifact.
- README contains the exact commands for local smoke testing.

## Phase 3.5: Particle Example

Status: initial pass complete.

Goal: add a compact particle example that carries forward the original Cubey
particle feel while staying below the threshold for a `projects/` runtime.

- Added `examples/particles` with a deterministic attractor-style particle
  field.
- Rendered particles as instanced screen-facing quads, not points or geometry
  shader billboards.
- Used a procedural Gaussian splat in the fragment shader with additive
  blending.
- Updated particle position/velocity in a compute shader over a storage buffer,
  then rendered the same buffer in the graphics pass.
- Kept controls example-local: Space pauses updates, `R` resets the field, and
  Escape closes the window.
- Did not promote particle simulation, billboard, or app-host abstractions; this
  remains reference example code until a real project repeats the shape.

Exit criteria:

- The example runs interactively with validation enabled.
- The compute-to-graphics storage-buffer synchronization is explicit in command
  recording.
- The docs identify why this is still an example and not the first project.

## Phase 4: Threading And Async Runtime Boundary

Status: initial pass complete.

Goal: make the first real project fit a non-stalling Vulkan model without
building a full threaded renderer too early.

- Added a small `cubey::jobs` facade around a standard-library worker executor.
- Keep third-party task/executor types out of public Cubey APIs.
- Added job-backed PNG capture encoding as the first queued work consumer.
- Added CPU-owned upload requests that can be drained by the GPU owner.
- Added frame tickets and deferred destruction helpers for future in-flight GPU
  lifetime tracking.
- Added project runtime vocabulary for setup, update, render-packet, resize,
  and shutdown contracts.
- Added `ProjectRuntimeServices` and moved `fluid_2d` simulation timing onto
  `ProjectFrame` while keeping command recording project-local.
- Added `ProjectRuntimeAdapter` and moved `fluid_2d` onto it while keeping host
  callbacks and command recording project-local.
- Shape GPU readbacks and deeper capture polling as queued work.
- Keep GPU ownership and `VkQueue` submission serialized through one owner.
- Keep examples direct; require longer-lived `projects/` to use the
  async-ready structure once it exists.

Exit criteria:

- Docs identify the app thread, GPU owner, worker executor, frame packet,
  upload request, capture request, and frame ticket concepts.
- The job facade can run CPU work, propagate errors, shut down cleanly, and
  provide deterministic test behavior.
- Upload/capture APIs can start synchronous internally while exposing queued
  semantics to project code.
- A project-like type can be checked at compile time against the setup, update,
  render-packet, resize, and shutdown contract.

## Phase 5: First Real Project

Status: active; first `fluid_2d` solver checkpoints complete.

Goal: prove the framework with one non-trivial procedural graphics project and
let repeated project needs shape the app/runtime API.

Current project:

- `projects/fluid_2d` starts the fluid simulation rewrite as a smaller 2D
  dye-and-velocity field. The current checkpoint has compute
  injection/advection, pressure projection, pointer injection, pause/reset,
  debug render modes, fullscreen rendering, a windowed smoke, and
  deterministic headless PNG output. Simulation timing now flows through
  `ProjectFrame` in both windowed and headless modes.

Candidate follow-ups:

- Solver tuning for `fluid_2d` now that velocity/divergence/pressure views are
  inspectable.
- Marching cubes for compute-generated geometry and indirect draw pressure.
- SDF sculpting if the sparse resource model becomes the more interesting
  framework driver.
- A larger particle system only if it grows beyond the current example-sized
  attractor demo.

Exit criteria:

- The project runs interactively with a window.
- The same project can run headlessly for a fixed number of frames and produce a
  deterministic output artifact.
- README contains the exact commands for local smoke testing. Status: complete
  for the current `fluid_2d` checkpoint.

## Phase 6: Runtime Extraction

Status: active.

Goal: extract only the host concepts that repeated windowed/headless project code
has proven useful.

- GLFW-backed window host outside `cubey::vulkan`. Status: complete.
- Initial windowed host outside `cubey::vulkan`. Status: complete for current
  windowed examples.
- Project lifecycle vocabulary: setup, update, render packet, resize, shutdown.
- Project runtime services for jobs, uploads, captures, frame tickets, deferred
  destruction, and `ProjectFrame` creation. Status: complete.
- Thin project runtime adapter for one project frame per host frame, context
  access, and deferred destruction retirement. Status: complete.
- Narrow no-GLFW headless PNG host that shares no-window instance/device,
  offscreen target, capture transitions, readback, and artifact writing. Status:
  complete for current headless examples and `fluid_2d`.
- Input/UI hooks once the contract is clear enough to keep project code cleaner
  without becoming a generic editor or UI framework.

Exit criteria:

- `window_clear` and `triangle` get shorter without hiding Vulkan
  synchronization, layout, descriptor, or resize constraints.
- `particles` proves the host can support update/input/compute-plus-graphics
  without becoming a particle system or renderer.
- Examples remain useful as small reference programs rather than becoming hidden
  framework tests.

## Later

- ImGui debug controls.
- Orbit camera and common interaction helpers.
- Ports of original Cubey projects: fluid simulation, marching cubes, fractals,
  and camera/shadow tests. Particle work should stay in `examples/particles`
  unless it grows into a larger project.
- SDF sculpting experiments from `projectR` if the resource model holds up.
- Browser or alternate-backend work only after a concrete project earns the
  extra shader and platform surface area.

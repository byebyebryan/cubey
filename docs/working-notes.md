# Working Notes

This is a living scratchpad for progress, hiccups, learnings, and gotchas that
are useful but not polished enough for the design document yet. Promote durable
decisions into `docs/DESIGN.md`, `docs/roadmap.md`, or
`docs/spike-findings.md` when they stabilize.

## Current Checkpoint

As of 2026-05-06:

- `main` has moved from docs/tooling-only to the first implementation slice.
- The primary target is `cubey`, a static library with public headers under
  `include/cubey/`.
- `examples/window_clear` is the first runnable. It creates a GLFW/Vulkan
  windowed surface, clears a swapchain image, presents it, and handles
  out-of-date/resize recreation through dynamic rendering.
- `window_clear` is example code, not library API. Keep named example behavior
  under `examples/`; promote only reusable components into `cubey`.
- Promoted the first reusable Vulkan components into `cubey`:
  `cubey::vulkan::Instance` owns validation/debug-utils setup, and
  `cubey::vulkan::Device` owns physical-device selection, logical-device
  lifetime, queue access, and memory-type selection.
- Promoted `cubey::vulkan::Swapchain` for swapchain/image-view ownership,
  `cubey::vulkan::CommandPool` for command-pool ownership and primary
  command-buffer allocation, and `cubey::vulkan::FrameResources` for one frame
  command buffer, image-available semaphore, per-swapchain-image present-ready
  semaphores, and fence.
- Promoted `cubey::vulkan::Buffer` for Vulkan buffer/memory ownership and
  host-visible coherent upload, plus `cubey::vulkan::ImmediateCommands` for
  one-shot setup copies.
- Promoted `cubey::vulkan::Image` and `cubey::vulkan::Sampler` for basic 2D
  image/view/memory ownership and sampler ownership.
- Promoted `cubey::vulkan::RenderContext` for explicit `begin_frame` /
  `end_frame` handling around surface-backed acquire, per-frame command reset,
  submit, present, and out-of-date/suboptimal reporting.
- Promoted `cubey::FrameClock` and `cubey::OrbitController` for deterministic
  frame timing, auto-rotation, pause/reset, and mouse-drag rotation.
- Promoted `cubey::FrameStats` for lightweight FPS, frame-time, extent,
  triangle-count, and pixel-rate telemetry.
- Promoted `cubey::math` as a narrow GLM-backed math wrapper. It exposes the
  current `Mat4`/`Vec4` types plus transform helpers and a Vulkan projection
  helper with depth-zero-to-one and framebuffer-Y conventions.
- Promoted `cubey::vulkan::ShaderModule` and added CMake GLSL-to-SPIR-V shader
  compilation with `glslangValidator`, including shared shader include
  directories and dependency tracking.
- Promoted `cubey::file_io` for generic binary reads/writes and
  `cubey::spirv_io` for tested SPIR-V word loading. Shader-backed examples
  share `cubey::read_spirv_file` instead of carrying local file readers.
- Promoted `cubey::jobs::JobSystem`, `InlineExecutor`, and `JobHandle` as the
  first CPU job facade. The implementation is standard-library-only for now,
  keeping Taskflow or `BS::thread_pool` swappable behind Cubey APIs later.
- Promoted `cubey::CaptureQueue` and `CaptureTicket` as the first concrete
  async-shaped consumer: completed RGBA pixels can be queued for PNG encoding
  through `cubey::jobs`, with blocking waits made explicit through
  `CaptureTicket::finish()`.
- Promoted `cubey::UploadQueue`, `UploadTicket`, and `QueuedUpload` as the
  first upload-side request queue. It owns submitted CPU bytes and can be
  drained by the future GPU owner without exposing Vulkan staging yet.
- Promoted `cubey::FrameTicketIssuer`, `FrameTicket`, and
  `DeferredDestructionQueue` as CPU-side frame-ticket vocabulary for deferred
  cleanup. They are not tied to Vulkan fences yet.
- Promoted `cubey::ProjectContext`, `ProjectFrame`, `ProjectExtent`,
  `RenderPacket`, and `ProjectLike` as the first async-ready project runtime
  vocabulary. Existing examples remain direct; future `projects/` should use
  the new boundary.
- Promoted `cubey::ProjectRuntimeServices` as the first project-facing owner for
  jobs, uploads, captures, frame tickets, deferred destruction, and
  `ProjectFrame` creation from frame timing.
- Promoted `cubey::ProjectRuntimeAdapter` as the thin reusable bridge between
  concrete hosts and project frame vocabulary. It does not own window/headless
  hosting, Vulkan command recording, or project lifecycle callbacks.
- Promoted the first pipeline/descriptor ownership components:
  `PipelineLayout`, `GraphicsPipeline`, `ComputePipeline`,
  `DescriptorSetLayout`, and `DescriptorPool`. These wrappers own lifetimes but
  intentionally keep create-info construction visible in example/project code.
- Promoted `DynamicGraphicsPipelineInfo` and `shader_stage` for the common
  dynamic-rendering graphics pipeline shape: one color attachment, optional
  depth, optional blend state, fixed viewport/scissor from swapchain extent,
  single-sample raster state, and explicit shader/layout/vertex-input choices
  from the caller.
- Promoted narrow Vulkan helpers for image layout transitions and
  dynamic-rendering color/depth attachment setup. They reduce repeated Vulkan
  struct boilerplate without owning command recording or render policy, and now
  live in separate `image_transitions` and `dynamic_rendering` modules.
- Promoted resource helpers for staging buffer config, device-local buffer
  upload/copy, depth format selection, depth image config, and `DepthAttachment`
  ownership. The cube examples now share this setup instead of carrying local
  staging-copy and depth-image code.
- Promoted descriptor and compute setup helpers for descriptor bindings, pools,
  writes, descriptor updates, pipeline layouts, and compute pipeline create-info.
  Current descriptor writes cover uniform buffers, storage buffers, storage
  images, and combined image samplers. `textured_cube` and `particles` now share
  this setup while keeping descriptor layout, shader, and dispatch choices
  explicit.
- Promoted `DescriptorSetInfo` and `DescriptorSetBundle` for the repeated
  single descriptor layout/pool/set shape. This removes layout/pool/allocation
  member boilerplate from examples without turning descriptors into bind groups.
- Promoted transfer/readback helpers for readback buffers, generated/uploaded
  sampled image configs, buffer-image copy regions, buffer-to-image and
  image-to-buffer copies, and named storage, transfer, sampled-image readback,
  and sampling transitions.
  `textured_cube` now uses the shared generated-texture config and storage
  transition helpers.
- Promoted the narrow pieces needed for the first headless artifact path:
  offscreen color render-target config, color-attachment-to-readback transition,
  and `cubey::image_io` PNG writing backed by vendored `stb_image_write`.
- Promoted `cubey::HeadlessPngHost` after `headless_render`,
  `fractal --headless`, and `fluid_2d --headless` repeated the same no-window
  host mechanics. It owns instance/device setup, offscreen target creation,
  capture transitions, readback, and PNG writing without pulling GLFW into the
  base `cubey` target.
- Promoted `SwapchainRecreateTracker` for the one clearly repeated frame-loop
  policy: bounding consecutive out-of-date/suboptimal recreate attempts. The
  actual swapchain-sized resource rebuild order remains example-local.
- Tightened the Vulkan helper contracts after review: descriptor write helpers
  now reject temporary wrapper objects at compile time, and the sampled-image
  readback transition name now exposes its layout assumptions.
- Added `examples/triangle` as the first shader-backed graphics pipeline smoke.
- Converted `examples/triangle` to dynamic rendering as the first render-pass
  direction spike. This removes triangle's render pass/framebuffer ownership and
  points the windowed runtime path toward dynamic-rendering attachments.
- Added `examples/spinning_cube` to exercise push constants, per-frame MVP
  animation, device-local vertex/index buffers, staging upload, dynamic
  rendering, and shared depth attachment setup.
- Added `examples/textured_cube` to exercise compute-generated texture data,
  image layout transitions, storage-image descriptors, a descriptor-backed scene
  uniform buffer, combined image sampler descriptors, normals, shared GLSL
  directional lighting, dynamic rendering, and shader sampling. It is also the
  first interactive example: left-drag rotates, Space pauses, `R` resets, and
  Escape exits.
- Added `examples/headless_render` to exercise no-window Vulkan instance/device
  creation, offscreen dynamic rendering, image-to-buffer readback, and PNG
  artifact writing.
- Added `examples/fractal` to exercise fullscreen fragment rendering, push
  constants, example-local drag/zoom navigation, and reuse of the headless PNG
  artifact path.
- Added `examples/particles` to exercise a graphics-plus-compute frame path:
  a storage-buffer particle field is updated by a compute shader and rendered as
  instanced screen-facing quads with procedural Gaussian splats and additive
  blending. It is intentionally still an example, not a project/runtime host.
- GLFW window setup, surface creation, command recording, acquire/present
  behavior, and resize policy remain example-local. Graphics pipeline creation,
  descriptor/compute setup, device-local cube-buffer uploads, depth attachments,
  and cube transform math now use shared helpers, while examples still own
  shader module, vertex-input, descriptor layout choices, dispatch choices, and
  render policy.
- CTest covers no-display terminal boundaries, graphical runs when a desktop
  window context is injected, and the headless PNG artifact path through shared
  CMake smoke helpers.
- Roadmap alignment: the next framework driver should be a real project. The
  lightweight fractal and particle work stayed under `examples/` and did not
  justify a broad app/runtime layer.
- Threading/async alignment: before the first real project grows, add an
  async-ready boundary rather than a full threaded renderer. The intended shape
  is CPU jobs behind Cubey APIs, queued upload/capture requests, explicit GPU
  ownership for queue submission and resource lifetime, and promotion gates for
  render threads, parallel command recording, and split queues.
- The current useful manual desktop smokes are:

```bash
env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:1 XDG_CURRENT_DESKTOP=niri \
  ./build/dev/examples/window_clear/window_clear --require-validation --frames 300 --width 1280 --height 720
env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:1 XDG_CURRENT_DESKTOP=niri \
  ./build/dev/examples/triangle/triangle --require-validation --frames 300 --width 1280 --height 720
env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:1 XDG_CURRENT_DESKTOP=niri \
  ./build/dev/examples/spinning_cube/spinning_cube --require-validation --frames 300 --width 1280 --height 720
env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:1 XDG_CURRENT_DESKTOP=niri \
  ./build/dev/examples/textured_cube/textured_cube --require-validation --frames 300 --width 1280 --height 720
env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:1 XDG_CURRENT_DESKTOP=niri \
  ./build/dev/examples/fractal/fractal --require-validation --frames 300 --width 1280 --height 720
env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:1 XDG_CURRENT_DESKTOP=niri \
  ./build/dev/examples/particles/particles --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/headless_render/headless_render --require-validation --width 640 --height 360 --output /tmp/cubey-headless.png
./build/dev/examples/fractal/fractal --headless --require-validation --width 640 --height 360 --output /tmp/cubey-fractal.png
./build/dev/projects/fluid_2d/fluid_2d --headless --require-validation --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-2d.png
```

As of 2026-04-28:

- `main` is intentionally lightweight: docs, repo setup, and build/tooling
  scaffolding only.
- `webgpu` and `vulkan` remain spike branches.
- The project direction is Vulkan-first for the main Vulkan layer.
- The primary CMake target should be the `cubey` library. Runnable programs
  should be explicit examples or projects, starting with
  `examples/window_clear`.
- The next implementation slice is to bring the windowed Vulkan clear/present
  path back into `main` as small modules instead of one large experiment file.

## Hiccups and Gotchas

### WebGPU / Dawn

- Dawn is a heavy dependency; first configure/build time is a real cost.
- The tested Linux environment needed Clang for Dawn because GCC 15 rejected
  Dawn's current standard-library interaction.
- Dawn FetchContent requires careful dependency and submodule settings.
- Wayland/X11 support needs explicit Dawn and `glfw3webgpu` configuration.
- Browser support is not friction-free. Chrome on Linux may require GPU/WebGPU
  flags for newer hardware; Firefox worked better on the tested setup.
- Browser WebGPU uses the user's local GPU, so it does not solve remote-dev GPU
  access when the browser is running on a laptop.

### Vulkan

- The compositor owns the real framebuffer extent. Under niri, a requested
  `1280x720` Vulkan window was forced to `1280x1432`; the app must follow the
  swapchain/surface extent rather than assuming the requested size.
- `VK_ERROR_OUT_OF_DATE_KHR` and framebuffer resize events are normal runtime
  paths, not exceptional failures. The app should log them clearly and
  recreate the swapchain.
- Validation layers should stay easy to require from smoke commands, not just
  optionally enable during manual debugging.
- Windowed and headless rendering need to stay healthy together. Headless gives
  the project inspectable artifacts and better automated feedback.
- The headless PNG path does not need GLFW or desktop environment variables,
  but it still needs a Vulkan-capable GPU and driver visible to the process.
- The Codex terminal may not inherit the desktop Wayland variables even when a
  niri session is active. For graphical smoke tests from that shell, inject the
  window context explicitly:

```bash
env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:1 XDG_CURRENT_DESKTOP=niri \
  ctest --preset dev --output-on-failure
```

- Windowed runs launched from a non-interactive Codex shell can be heavily
  compositor-throttled even when `WAYLAND_DISPLAY=wayland-1` is injected.
  Short `--frames 1` to `--frames 5` smokes are useful for validation from that
  shell; longer visual smokes are better run from a normal desktop terminal.

- If every windowed example fails with
  `vkEnumeratePhysicalDevices count failed with VkResult -3` and
  `vulkaninfo --summary` also fails, check the host driver before debugging
  Cubey. On 2026-05-01 this was an NVIDIA userspace/kernel-module mismatch
  after a package upgrade: installed userspace was `595.71.05`, but the loaded
  kernel module was still `595.58.03`; rebooting into the upgraded kernel/module
  should be the first fix.
- Binary semaphores signaled into `vkQueuePresentKHR` cannot be reused just
  because the CPU waited on the next frame fence. The swapchain owns that
  wait-side lifetime until the presented image is reacquired. Keep present-ready
  semaphores indexed by acquired swapchain image, and recreate them with frame
  resources whenever the swapchain image count changes.

### Repo / Build

- Use CMake presets as the entrypoint instead of one-off configure commands.
- Keep build artifacts out of Git with ignored `build*/` directories.
- `CHANGELOG.md` is the release-note source. Avoid separate release-note files
  until real releases make that worthwhile.
- The `asan` preset can report DBus-owned allocations from Vulkan loader/driver
  initialization after successful headless PNG renders. The PNG CTest smoke
  helpers apply the narrow suppressions in `cmake/lsan.supp`; keep app-owned
  leak suppression out of that file.
- Keep `include/cubey/` as the public include surface for in-repo examples,
  projects, and tests. Defer install/export/package rules until external
  consumption becomes real.
- Use `examples/` for minimal reference programs and `projects/` for
  long-lived graphics experiments.
- Prefer adding CI once `main` has a real source target and smoke command to
  exercise. Empty CI would add maintenance without much signal.

## Learning Log

### 2026-04-28

- Preserved the original project's MIT license direction on `main`.
- Added repo-level setup before porting Vulkan code so future implementation
  work has formatting, warning, and doc conventions in place.
- Captured roadmap and working notes as living docs to keep short-term context
  close to the codebase.

### 2026-05-01

- Kept the Vulkan slice focused on reusable ownership boundaries: instance,
  device, swapchain images/views, and single-frame command/sync state belong in
  `cubey`; GLFW and the clear/present loop stay in `examples/window_clear`.
- Direct terminal smoke still reports `glfwInit failed` in this no-display
  session, while CTest treats that as an acceptable environment boundary.
- Added the first shader-backed example without promoting render pass or
  pipeline abstractions yet. The repeated code between `window_clear` and
  `triangle` is now useful evidence for the next promotion decision.
- Added a spinning cube without adding vertex/index buffers yet. The useful new
  signal is depth ownership and push constants; buffers, image uploads, and
  texture sampling remain separate future slices.

### 2026-05-02

- After rebooting into the matching NVIDIA userspace/kernel module, graphical
  Vulkan runs from the Codex shell worked by explicitly injecting the active
  niri Wayland environment.
- Validation-layer desktop smokes caught a real presentation-sync bug that the
  no-display CTest path could not see: one reusable present-ready semaphore was
  still possibly owned by the swapchain. `FrameResources` now tracks
  present-ready semaphores per swapchain image.
- The first resource-layer slice added `Buffer` and one-shot immediate commands.
  `spinning_cube` now uploads real vertex/index data through a staging buffer
  and draws with `vkCmdDrawIndexed`; image upload, descriptors, and sampling are
  the next resource milestone.
- The second resource-layer slice added `Image`, `Sampler`, and
  `examples/textured_cube`. The example intentionally uses a generated
  checkerboard texture before asset loading so the signal stays focused on
  transfer, layout transitions, descriptors, and shader sampling.
- The first interaction slice added unit-tested pure timing/control helpers and
  wired `textured_cube` to GLFW callbacks. Keeping GLFW calls example-local
  preserves the current library boundary while still moving reusable control
  policy into `cubey`.
- The telemetry slice updates the `textured_cube` window title rather than
  adding text rendering or ImGui yet. This gives immediate framework signal
  without expanding the rendering/UI surface.
- The shaded-cube slice initially kept lighting intentionally simple with
  per-face normals, a fixed directional light, and push constants for MVP plus
  model matrices. The follow-up `textured_cube` uniform slice moved scene
  matrices and light values into a descriptor-backed uniform buffer once
  descriptors had a concrete use case.
- The compute-texture slice replaced the CPU checkerboard upload with a
  setup-time compute dispatch into a storage image, followed by an explicit
  `GENERAL` to `SHADER_READ_ONLY_OPTIMAL` transition before the graphics pass
  samples it. This gives compute-plus-graphics signal without promoting
  descriptor or pipeline components into `cubey` prematurely.
- The compute-texture path still uses Cubey's current single-queue-family
  device model. This is acceptable for the current desktop target and keeps the
  example simple, but split graphics/compute/present queues should be handled
  before treating the device layer as a broader compatibility abstraction.
- The command abstraction is intentionally narrow: `CommandPool` owns pool
  lifetime and primary-buffer allocation, and `begin_command_buffer` removes
  repeated begin boilerplate. Queue submission still flows through `Device`,
  `RenderContext`, and `ImmediateCommands` until split graphics/compute/present
  queues create a real need for a queue abstraction.
- The dynamic-rendering slice raised the requested instance API version to
  Vulkan 1.3 and added an opt-in device feature requirement. Use this path for
  the next windowed runtime path instead of wrapping classic render passes first.
- The `RenderContext` component intentionally stops at the Vulkan frame
  boundary. It does not own GLFW or resize policy yet, which keeps the primary
  `cubey` target free of an unconditional GLFW dependency while still removing
  the most repeated acquire/submit/present code.
- All current windowed examples now use `RenderContext::begin_frame`, record
  commands directly, then call `RenderContext::end_frame` for submit, present,
  and out-of-date/suboptimal reporting. The examples still own their resize
  policy.
- The pipeline/descriptor component slice started at RAII ownership, not
  rendering policy. The descriptor/compute setup slice then promoted the narrow
  helper names that had become repetitive: descriptor bindings, pool sizes,
  descriptor writes, descriptor updates, pipeline-layout create info, and
  compute-pipeline create info.
- All current windowed examples now use dynamic rendering. The helper layer owns
  repeated image-transition and attachment-info construction, while examples
  still own frame command recording and resize policy.
- The first graphics pipeline helper slice intentionally covers the repeated
  dynamic-rendering create-info shape, not a material system or renderer.
  `triangle`, `spinning_cube`, and `textured_cube` now share shader-stage and
  graphics-pipeline setup while keeping their layout, vertex-input, descriptor,
  and depth decisions explicit. The descriptor/compute setup slice later moved
  `textured_cube`'s setup-time compute path onto shared create-info and
  descriptor-write helpers.
- The resource and attachment cleanup slice made `Buffer` movable so helper
  functions can return owned device-local buffers. It also moved the cube
  examples onto shared staging upload and `DepthAttachment` helpers. It kept the
  examples focused on scene data and command recording while leaving descriptor,
  compute, and texture transition policy visible for follow-up slices.
- The descriptor and compute setup slice deliberately stopped short of bind
  groups or material abstractions. The useful helper boundary is still Vulkan
  vocabulary: layout bindings, pool sizes, descriptor writes, pipeline layouts,
  compute pipeline create info, and explicit dispatch from the example.
- The transfer/readback slice added the missing low-level pieces for headless
  artifacts before the host shape was clear. Generated sampled images now
  include transfer-source usage so compute outputs can be copied into readback
  buffers once a headless smoke has a render target to inspect.
- The frame-loop cleanup slice only promoted recreate-attempt tracking. A
  generic swapchain resource rebuild callback would add indirection around code
  that still differs by example, so the rebuild steps stay visible until a host
  layer has a stronger reason to exist.
- The helper-contract review kept the layer honest: returning raw Vulkan
  descriptor writes is acceptable for now, but the API must make pointer
  lifetimes hard to misuse. Naming also matters for transition helpers; generic
  names are risky when old-layout and source-stage assumptions are specific.
- The no-window artifact path proved useful only after it stayed concrete:
  offscreen color target, explicit color-attachment-to-readback transition,
  image-to-buffer copy, `stb_image_write` PNG output, and a no-display CTest
  smoke. That is still not enough evidence to justify a broad app host.

### 2026-05-05

- The headless PNG slice landed in three small checkpoints: PNG byte-buffer
  output helper, Vulkan offscreen color/readback helpers, and
  `examples/headless_render`.
- `headless_render` intentionally renders a deterministic clear first. A
  fullscreen shader could add signal later, but the current value is proving
  the no-window Vulkan path and PNG artifact loop without creating an app host.
- CTest now includes `headless_render_writes_png`, which writes into the build
  tree and validates the PNG signature. The manual validation smoke produced a
  `128 x 72` RGBA PNG on the RTX 5070 Ti.
- The fractal example deliberately stayed in `examples/`, not `projects/`: it is
  useful fullscreen/headless signal, but still too small to create real project
  runtime pressure.
- `examples/fractal` now has an example-local `FractalView` for pan, cursor
  zoom, reset, and push constants. That kept input math testable without moving
  fractal-specific behavior into `cubey`.
- Repo-wide cleanup after the fractal slice promoted generic binary reads and
  writes into `cubey::file_io`, kept SPIR-V word loading in `cubey::spirv_io`,
  and renamed PNG artifact helpers to `cubey::image_io`.
- The repeated CTest shell snippets for windowed/no-display and headless PNG
  smoke checks now live in `cmake/CubeySmokeTests.cmake`. Individual examples
  declare only the target, test name, and expected success pattern or output
  path.
- Captured `docs/threading-and-async.md` so the first real project can start
  from an async-ready shape instead of growing around direct blocking
  upload/readback/PNG paths. The doc records Taskflow and `BS::thread_pool` as
  first candidates, but keeps either dependency hidden behind future
  `cubey::jobs` APIs.
- Batch 1 of the threading/async loop landed the job facade first, with tests
  for immediate inline execution, worker result delivery, exception propagation,
  accepted-job completion during shutdown, and submit rejection after shutdown.
- Batch 2 added the CPU-side capture encoding queue. It does not change Vulkan
  readback yet; it establishes the ticket/wait vocabulary that later GPU
  capture polling should reuse.
- Batch 3 added the CPU-side upload queue. It deliberately stops before Vulkan
  staging/copy integration so the first project can submit upload intent without
  forcing the final GPU scheduling shape.
- Batch 4 added frame tickets and deferred destruction. This gives future
  in-flight GPU lifetime work a vocabulary before N-frames-in-flight or timeline
  semaphore integration exists.
- Batch 5 added the project runtime vocabulary without migrating examples. This
  keeps reference examples readable while letting `fluid_2d` start using
  service-based access to jobs, uploads, captures, frame tickets, deferred
  cleanup, and project-frame timing.
- The particle slice stayed intentionally under `examples/particles`: the useful
  signal was graphics-plus-compute command recording, storage-buffer
  descriptors, additive blending, and GPU-updated billboard rendering. It does
  not yet justify a project host or shared particle system abstraction.
- For particles, screen-facing quads are generated in the vertex shader from
  `gl_VertexIndex` and `gl_InstanceIndex`; this avoids geometry shaders while
  keeping the particle storage buffer directly readable by both compute and
  graphics stages.
- The first app/runtime extraction landed as `cubey_app`, not as part of
  `cubey::vulkan`. `GlfwWindow` and `GlfwSurface` own the platform/surface
  handoff, and `WindowedHost` owns the repeated windowed loop. `window_clear`,
  `triangle`, and `particles` now use it while keeping Vulkan command recording
  and render resources example-local.
- The follow-up windowed-host migration moved `window_clear`, `spinning_cube`,
  `textured_cube`, and the windowed `fractal` path onto `WindowedHost` too.
  Direct GLFW use is now isolated to `cubey_app`; examples depend on
  `cubey::app` for window hosting and keep their own shader, pipeline,
  descriptor, render-resource, and command-recording policy. `headless_render`
  and `fractal --headless` remain explicit no-window paths.

### 2026-05-06

- The descriptor cleanup added `DescriptorSetInfo` and `DescriptorSetBundle`.
  `textured_cube` and `particles` now carry their descriptor layout choices
  locally but no longer own separate layout, pool, and allocation members.
- GLM is now a CMake-resolved dependency behind `cubey::math`. The wrapper keeps
  GLM's matrix/vector types available while centralizing Cubey's Vulkan
  projection convention: radians, depth zero-to-one, and framebuffer-Y flip.
- `spinning_cube` and `textured_cube` now use `cubey::math` for model, view,
  projection, and MVP matrices. This removes duplicate local matrix code but
  still leaves mesh data, descriptor layout, and render policy in the examples.
- GLM should stay behind Cubey's public wrapper in examples/projects unless a
  project has a clear need for broader GLM APIs. This keeps future math
  convention changes localized.
- `projects/fluid_2d` is now the first project target. The first checkpoint uses
  a 256x144 storage-buffer field with dye and velocity per cell, then records
  compute inject and advect/fade passes before rendering the dye field through a
  fullscreen graphics pass.
- The project has both windowed and headless paths. Headless uses fixed timing,
  a deterministic injector, the shared headless PNG host, and a PNG smoke test.
  This proves project-level headless output without making the host responsible
  for simulation or render policy.
- Pressure projection now runs project-local: divergence and pressure buffers
  are scalar storage buffers, Jacobi iterations ping-pong pressure A/B, and the
  gradient subtraction updates field A in place so the next frame and render
  path keep the existing source field.
- Interaction and debug views also stayed project-local. Windowed `fluid_2d`
  now has left-drag injection, pause/resume, reset, and dye/velocity/divergence/
  pressure render modes. Headless stays deterministic by using the procedural
  injector and fixed timing.
- `fluid_2d` now owns `ProjectRuntimeAdapter` and records simulation from
  `ProjectFrame` in both windowed and headless modes. This makes frame timing
  and frame tickets real project vocabulary without introducing a generic
  project host or moving Vulkan command recording out of the project.
- This still should not promote renderer, scene, or generic solver
  abstractions. The next useful pressure is solver tuning or a second project
  that repeats the same resource shape.
- The headless host extraction is intentionally narrow. It removed duplicate
  no-window setup/readback/PNG plumbing from `headless_render`, `fractal`, and
  `fluid_2d`, but capture command recording, resource setup, fixed-step
  simulation, and project shutdown remain local callbacks.

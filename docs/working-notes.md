# Working Notes

This is a living scratchpad for progress, hiccups, learnings, and gotchas that
are useful but not polished enough for the design document yet. Promote durable
decisions into `docs/DESIGN.md`, `docs/roadmap.md`, or
`docs/spike-findings.md` when they stabilize.

## Current Checkpoint

As of 2026-05-02:

- `main` has moved from docs/tooling-only to the first implementation slice.
- The primary target is `cubey`, a static library with public headers under
  `include/cubey/`.
- `examples/window_clear` is the first runnable. It creates a GLFW/Vulkan
  visible surface, clears a swapchain image, presents it, and handles
  out-of-date/resize recreation.
- `window_clear` is example code, not library API. Keep named example behavior
  under `examples/`; promote only reusable primitives into `cubey`.
- Promoted the first reusable Vulkan primitives into `cubey`:
  `cubey::vulkan::Instance` owns validation/debug-utils setup, and
  `cubey::vulkan::Device` owns physical-device selection, logical-device
  lifetime, queue access, and memory-type selection.
- Promoted `cubey::vulkan::Swapchain` for swapchain/image-view ownership and
  `cubey::vulkan::FrameResources` for a single command pool, command buffer,
  image-available semaphore, per-swapchain-image present-ready semaphores, and
  fence.
- Promoted `cubey::vulkan::Buffer` for Vulkan buffer/memory ownership and
  host-visible coherent upload, plus `cubey::vulkan::ImmediateCommands` for
  one-shot setup copies.
- Promoted `cubey::vulkan::Image` and `cubey::vulkan::Sampler` for basic 2D
  image/view/memory ownership and sampler ownership.
- Promoted `cubey::FrameClock` and `cubey::OrbitController` for deterministic
  frame timing, auto-rotation, pause/reset, and mouse-drag rotation.
- Promoted `cubey::FrameStats` for lightweight FPS, frame-time, extent,
  triangle-count, and pixel-rate telemetry.
- Promoted `cubey::vulkan::ShaderModule` and added CMake GLSL-to-SPIR-V shader
  compilation with `glslangValidator`.
- Added `examples/triangle` as the first shader-backed graphics pipeline smoke.
- Converted `examples/triangle` to dynamic rendering as the first render-pass
  direction spike. This removes triangle's render pass/framebuffer ownership and
  points the visible runtime shell toward dynamic-rendering attachments.
- Added `examples/spinning_cube` to exercise push constants, per-frame MVP
  animation, depth format selection, device-local vertex/index buffers, staging
  upload, and example-local depth image/view resources.
- Added `examples/textured_cube` to exercise compute-generated texture data,
  image layout transitions, storage-image descriptors, combined image sampler
  descriptors, normals, directional lighting, and shader sampling. It is also
  the first interactive example: left-drag rotates, Space pauses, `R` resets,
  and Escape exits.
- GLFW window setup, surface creation, render pass, graphics pipeline, depth
  resources, framebuffers, command recording, acquire/present behavior, and
  resize policy remain example-local.
- CTest covers both the no-display terminal boundary and graphical runs when a
  desktop window context is injected.
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
```

As of 2026-04-28:

- `main` is intentionally lightweight: docs, repo setup, and build/tooling
  scaffolding only.
- `webgpu` and `vulkan` remain spike branches.
- The project direction is Vulkan-first for the main renderer.
- The primary CMake target should be the `cubey` library. Runnable programs
  should be explicit examples or projects, starting with
  `examples/window_clear`.
- The next implementation slice is to bring the visible Vulkan clear/present
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
  `1280x720` Vulkan window was forced to `1280x1432`; the renderer must follow
  the swapchain/surface extent rather than assuming the requested size.
- `VK_ERROR_OUT_OF_DATE_KHR` and framebuffer resize events are normal runtime
  paths, not exceptional failures. The renderer should log them clearly and
  recreate the swapchain.
- Validation layers should stay easy to require from smoke commands, not just
  optionally enable during manual debugging.
- Visible and headless rendering need to stay healthy together. Headless gives
  the project inspectable artifacts and better automated feedback.
- The Codex terminal may not inherit the desktop Wayland variables even when a
  niri session is active. For graphical smoke tests from that shell, inject the
  window context explicitly:

```bash
env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:1 XDG_CURRENT_DESKTOP=niri \
  ctest --preset dev --output-on-failure
```

- If every visible example fails with
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
- Added repo-level setup before porting renderer code so future implementation
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
  without expanding the render pass/UI surface.
- The shaded-cube slice kept lighting intentionally simple: per-face normals,
  a fixed directional light, and push constants for MVP plus model matrices.
  This exercises vertex attribute growth without introducing uniform buffers
  yet.
- The compute-texture slice replaced the CPU checkerboard upload with a
  setup-time compute dispatch into a storage image, followed by an explicit
  `GENERAL` to `SHADER_READ_ONLY_OPTIMAL` transition before the graphics pass
  samples it. This gives compute-plus-graphics signal without promoting
  descriptor or pipeline helpers into `cubey` prematurely.
- The compute-texture path still uses Cubey's current single-queue-family
  device model. This is acceptable for the current desktop target and keeps the
  example simple, but split graphics/compute/present queues should be handled
  before treating the device layer as a broader compatibility abstraction.
- The dynamic-rendering slice raised the requested instance API version to
  Vulkan 1.3 and added an opt-in device feature requirement. Use this path for
  the next visible runtime shell instead of wrapping classic render passes first.

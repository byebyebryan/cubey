# Working Notes

This is a living scratchpad for progress, hiccups, learnings, and gotchas that
are useful but not polished enough for the design document yet. Promote durable
decisions into `docs/DESIGN.md`, `docs/roadmap.md`, or
`docs/spike-findings.md` when they stabilize.

## Current Checkpoint

As of 2026-05-01:

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
  lifetime, and queue access.
- Promoted `cubey::vulkan::Swapchain` for swapchain/image-view ownership and
  `cubey::vulkan::FrameResources` for a single command pool, command buffer,
  semaphores, and fence.
- Promoted `cubey::vulkan::ShaderModule` and added CMake GLSL-to-SPIR-V shader
  compilation with `glslangValidator`.
- Added `examples/triangle` as the first shader-backed graphics pipeline smoke.
- GLFW window setup, surface creation, render pass, graphics pipeline,
  framebuffers, command recording, acquire/present behavior, and resize policy
  remain example-local.
- The current terminal session has no display, so direct local execution reports
  `glfwInit failed`; CTest accepts that as the expected no-display smoke result.
- The next useful manual desktop smokes are:

```bash
./build/dev/examples/window_clear/window_clear --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/triangle/triangle --require-validation --frames 300 --width 1280 --height 720
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

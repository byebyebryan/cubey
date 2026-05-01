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
- GLFW window setup, surface creation, swapchain, render pass, framebuffers,
  command buffers, sync, and clear/present behavior remain example-local.
- The current terminal session has no display, so direct local execution reports
  `window_clear: glfwInit failed`; CTest accepts that as the expected no-display
  smoke result.
- The next useful manual desktop smoke is:

```bash
./build/dev/examples/window_clear/window_clear --require-validation --frames 300 --width 1280 --height 720
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

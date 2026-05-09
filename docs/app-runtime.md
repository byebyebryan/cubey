# App Runtime

Cubey has enough windowed examples to start extracting a real app/runtime core.
This layer should reduce repeated host code without becoming a renderer, a
backend abstraction, or a scene system.

## Direction

The app runtime is responsible for host flow:

- GLFW window lifetime and Vulkan surface handoff.
- Presentable framebuffer extent queries and minimized-window waiting.
- Input collection, per-frame input snapshots, and low-level event dispatch.
- Windowed frame loop, frame timing, frame stats, and frame limits.
- Swapchain resize/out-of-date recreation orchestration.
- Project lifecycle calls.

The app runtime is not responsible for rendering policy:

- No material system.
- No render graph.
- No scene graph.
- No particle system abstraction.
- No descriptor, pipeline, pass, or resource policy beyond host-owned
  surface/swapchain/frame services.
- No backend abstraction over Vulkan.

## Target Structure

Keep GLFW and app hosting outside `cubey::vulkan`.

```text
cubey
  core runtime, jobs, uploads, captures, headless PNG host,
  Vulkan ownership helpers

cubey_app
  GLFW window host and windowed app host

examples / projects
  project state, shaders, Vulkan resources, command recording
```

`cubey_app` may depend on `cubey`, GLFW, and Vulkan headers. The base `cubey`
target should not depend on GLFW.

## First Interfaces

### `cubey::app::GlfwWindow`

Owns the platform window and Vulkan surface handoff:

- Initialize/terminate GLFW for the process.
- Create a no-client-API window.
- Expose required Vulkan instance extensions.
- Create and destroy `VkSurfaceKHR` for a caller-owned `VkInstance`.
- Track framebuffer resize events.
- Report the current framebuffer extent.
- Wait until the framebuffer is presentable.
- Poll/wait events and expose close state.
- Dispatch key, mouse-button, cursor-position, and scroll events.

This type should not know about Cubey swapchains, frame resources, pipelines, or
project lifecycles.

### `cubey::app::WindowedHost`

Owns the common windowed loop once `GlfwWindow` is stable:

- Create `Instance`, surface, `Device`, `Swapchain`, and `FrameResources`.
- Drive `RenderContext::begin_frame` and `RenderContext::end_frame`.
- Recreate swapchain-sized resources when the window is resized or presentation
  reports out-of-date/suboptimal.
- Publish timing and stats to the window title when a project provides counts.

This host should call into project/example code for setup, swapchain-sized
resource creation/destruction, update, command recording, and shutdown.

## Migration Order

1. Extract `GlfwWindow`. Status: complete.
2. Migrate `examples/window_clear` to prove the platform layer. Status:
   complete.
3. Extract the first `WindowedHost` loop. Status: complete.
4. Migrate `examples/triangle` to prove the minimal render callback path.
   Status: complete.
5. Migrate `examples/particles` to prove update/input/compute-plus-graphics
   behavior. Status: complete.
6. Migrate `examples/spinning_cube` to prove indexed geometry, depth, and
   per-frame push constants. Status: complete.
7. Add pointer input and migrate `examples/textured_cube` to prove
   orbit-control input, frame stats, descriptors, setup-time compute, and
   texture sampling. Status: complete.
8. Migrate the windowed `examples/fractal` path while preserving its headless
   PNG path. Status: complete.
9. Extract a narrow no-GLFW headless PNG host after `headless_render`,
   `fractal --headless`, and `fluid_2d --headless` repeated the same offscreen
   render/readback/write loop. Status: complete.
10. Migrate `examples/headless_render` and `examples/fractal --headless` to the
    shared headless host. Status: complete.
11. Migrate `projects/fluid_2d --headless` to the shared headless host while
    preserving project-local compute simulation and fullscreen draw code.
    Status: complete.
12. Add project runtime services and move `fluid_2d` simulation timing onto
    `ProjectFrame` in both windowed and headless modes. Status: complete.
13. Extract `ProjectRuntimeAdapter` for the repeated host bridge: one project
    frame per host frame, context access, and deferred destruction retirement.
    Status: complete.

## Current Checkpoint

- `cubey_app` is an optional target that depends on `cubey`, GLFW, and Vulkan.
- `cubey::app::GlfwWindow` owns GLFW initialization, no-client-API window
  creation, required Vulkan instance extension lookup, framebuffer resize
  tracking, presentable-size waiting, title updates, and key/pointer event
  dispatch. It also feeds host-owned input state from GLFW callbacks.
- `cubey::input::InputState` and `InputFrame` provide the shared polling layer
  for keyboard, mouse button, cursor, drag, and scroll state. Windowed examples
  and projects now read input during `update()` instead of installing local
  callback-driven state machines.
- `cubey::input::PointerDrag`, `PanZoom2DController`, and the input-aware
  `OrbitController` cover the current repeated 2D/3D pointer-control shapes
  without introducing a scene, camera, or action-binding system.
- `cubey::app::GlfwSurface` owns the GLFW-created `VkSurfaceKHR`.
- `cubey::app::WindowedHost` owns the current windowed loop: instance, surface,
  device, swapchain, frame resources, frame timing, optional frame stats,
  acquire/record/submit/present, and swapchain recreation.
- All current windowed examples use the app host: `window_clear`, `triangle`,
  `spinning_cube`, `textured_cube`, `fractal`, and `particles`. They still own
  their shaders, pipelines, descriptors, command recording, and example-specific
  state.
- `cubey::HeadlessPngHost` owns the repeated no-window Vulkan instance/device,
  offscreen RGBA target, color-attachment/readback transitions, image readback,
  and PNG write path without depending on GLFW.
- `headless_render`, `fractal --headless`, and `fluid_2d --headless` use the
  headless host while keeping their resource setup, simulation/update work, and
  capture command recording local.
- `cubey::ProjectRuntimeServices` now owns project-facing jobs, uploads,
  captures, frame tickets, and deferred destruction.
- `cubey::ProjectRuntimeAdapter` wraps those services with the repeated
  host-bridge behavior: convert `FrameTiming` to one `ProjectFrame` per host
  frame, expose `ProjectContext`, and retire deferred destruction on shutdown.
  `fluid_2d` uses the adapter in both windowed and headless modes.
- A generic project host is still deferred; the current evidence only justifies
  shared service ownership and frame bridging.

## Promotion Rules

- Promote only repeated host mechanics.
- Keep command recording explicit in examples/projects.
- Keep lifecycle APIs small enough that `window_clear` and `triangle` remain
  readable.
- Prefer project-owned state over host-owned policy.
- Add more input/UI hosting only as examples or projects need it.
- Keep the headless host narrow: no scene/render abstraction, no implicit
  project lifecycle, and no GLFW dependency.
- Promote a full project lifecycle host only after another `projects/` target
  repeats the same setup/update/render/shutdown bridge.

## Current Non-Goals

- Dedicated render thread.
- Split graphics/compute/present queue scheduler.
- Parallel command recording.
- ImGui/UI hosting.
- WebGPU/Dawn backend parity.

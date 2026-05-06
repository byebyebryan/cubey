# App Runtime

Cubey has enough windowed examples to start extracting a real app/runtime core.
This layer should reduce repeated host code without becoming a renderer, a
backend abstraction, or a scene system.

## Direction

The app runtime is responsible for host flow:

- GLFW window lifetime and Vulkan surface handoff.
- Presentable framebuffer extent queries and minimized-window waiting.
- Input/event dispatch.
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
  core runtime, jobs, uploads, captures, Vulkan ownership helpers

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
6. Revisit headless hosting after the windowed host has real shape.

## Current Checkpoint

- `cubey_app` is an optional target that depends on `cubey`, GLFW, and Vulkan.
- `cubey::app::GlfwWindow` owns GLFW initialization, no-client-API window
  creation, required Vulkan instance extension lookup, framebuffer resize
  tracking, presentable-size waiting, title updates, and key event dispatch.
- `cubey::app::GlfwSurface` owns the GLFW-created `VkSurfaceKHR`.
- `cubey::app::WindowedHost` owns the current windowed loop: instance, surface,
  device, swapchain, frame resources, frame timing, optional frame stats,
  acquire/record/submit/present, and swapchain recreation.
- `examples/window_clear`, `examples/triangle`, and `examples/particles` use
  the app host. They still own their shaders, pipelines, descriptors, command
  recording, and example-specific state.

## Promotion Rules

- Promote only repeated host mechanics.
- Keep command recording explicit in examples/projects.
- Keep lifecycle APIs small enough that `window_clear` and `triangle` remain
  readable.
- Prefer project-owned state over host-owned policy.
- Add input events only as examples need them; start with key presses and
  resize.
- Keep headless host extraction separate from the first windowed host unless a
  migration makes the shared shape obvious.

## Current Non-Goals

- Dedicated render thread.
- Split graphics/compute/present queue scheduler.
- Parallel command recording.
- ImGui/UI hosting.
- WebGPU/Dawn backend parity.

# Host and Engine

Cubey needs a deliberate host/engine foundation for windowed and headless GPU
work. This layer should make the core frame, input, surface, timing, and project
boundaries explicit without becoming a renderer, backend abstraction, or scene
system.

## Direction

The host layer is responsible for host flow:

- GLFW window lifetime and Vulkan surface handoff.
- Presentable framebuffer extent queries and minimized-window waiting.
- Input collection, per-frame input snapshots, and low-level event dispatch.
- Windowed frame loop, frame timing, frame stats, and frame limits.
- Swapchain resize/out-of-date recreation orchestration.
- Project lifecycle calls.

The engine layer is responsible for scoped foundation ownership:

- Project runtime services.
- Render resource handle identity and material metadata.
- Scene creation/destruction and scene-backed manager validation.
- Shared project/GPU service bridges.

The host and engine layers are not responsible for rendering policy:

- No material system.
- No render graph.
- No scene graph.
- No particle system abstraction.
- No descriptor, pipeline, pass, or resource policy beyond host-owned
  surface/swapchain/frame services.
- No backend abstraction over Vulkan.

## Target Structure

Keep GLFW and window hosting outside `cubey::vulkan`.

```text
cubey_engine
  scoped root owner, project runtime services, uploads/captures,
  GPU project bridge, scenes, and render resource identity

cubey_host
  GLFW window host, windowed loop host, and headless PNG host

cubey
  aggregate target for examples/projects that want the full foundation

examples / projects
  project state, shaders, Vulkan resources, command recording sequence
```

`cubey_host` may depend on `cubey::engine`, GLFW, and Vulkan headers. The base
`cubey::engine` target should not depend on GLFW.

## First Interfaces

### `cubey::host::GlfwWindow`

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

### `cubey::host::WindowedHost`

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
14. Attach host GPU runtimes to `ProjectRuntimeAdapter`, add
    `ProjectGpuServices` readback tickets, and migrate `fluid_2d` project-owned
    GPU setup/headless simulation work through that bridge. Status: complete.

## Current Checkpoint

- `cubey_host` is an optional target that depends on `cubey::engine`, GLFW, and
  Vulkan.
- `cubey::host::GlfwWindow` owns GLFW initialization, no-client-API window
  creation, required Vulkan instance extension lookup, framebuffer resize
  tracking, presentable-size waiting, title updates, and key/pointer event
  dispatch. It also feeds host-owned input state from GLFW callbacks.
- `cubey::input::InputState` and `InputFrame` provide the shared polling layer
  for keyboard, mouse button, cursor, drag, and scroll state. Windowed examples
  and projects now read input during `update()` instead of installing local
  callback-driven state machines.
- `cubey::Camera2D` and `Camera3D` hold reusable 2D view state and 3D
  projection/view helpers. `OrbitController` and `OrbitCameraState` are layered
  control helpers for camera orbit behavior rather than standalone camera
  primitives.
- `cubey::Transform2D` and `Transform3D` are explicit affine transform value
  types. `Transform2D` uses translation, scalar-radian rotation, and scale to
  produce a `Mat3`; `Transform3D` uses translation, quaternion rotation, and
  scale to produce a `Mat4`. Both expose `affine_matrix()` instead of
  model-matrix terminology. `TransformManager2D` and `TransformManager3D` now
  provide entity-backed parent/child transform components, cached local-to-world
  affine matrices, scene edit queues, and epoch-local read-view snapshots.
- `CameraManager2D` and `CameraManager3D` provide entity-backed camera
  components through the same scene edit/read-view boundary.
- `RenderableManager3D` provides entity-backed mesh renderable components and
  builds CPU-side renderable packets from scene read views. Packets carry
  transform matrices plus opaque mesh/material handles; examples still resolve
  those handles to their own Vulkan resources and pipelines.
- `LightManager3D` provides entity-backed directional and point light
  components and builds CPU-side light packets from scene read views. Light
  packets are render input data; shader binding, lighting model, and GPU upload
  policy remain example/project-owned.
- `cubey::render::RenderResourceRegistry` is owned by `Engine` and issues the
  generational mesh/material handles used by renderable components. It tracks
  CPU-side identity, liveness, mesh labels, and material tags only; Vulkan
  resource ownership stays with examples/projects for now.
- `ResourceTable` and CPU draw planning give examples a shared way to resolve
  mesh handles and sort/validate draw packets before command recording.
- `cubey::vulkan::CommandRecorder` removes repeated low-level command-buffer
  call boilerplate while keeping pass order, barriers, descriptors, pipelines,
  and render intent in examples/projects.
- `cubey::vulkan::GpuRuntime` is the host-visible GPU owner. It runs a threaded
  owner by default, keeps inline mode explicit for tests/bring-up, and uses
  `SubmissionCoordinator` for serialized queue submission, so host/project setup
  work enters through the runtime queue rather than owning queue submission
  directly.
- `cubey::scene::View3D` gives 3D examples a shared CPU frame-planning
  boundary: camera matrices, draw packets, light packets, ambient-only
  environment, and conservative frustum culling. It is not a scene manager,
  renderer service, or Vulkan command recorder.
- `cubey::input::PointerDrag`, `PanZoom2DController`, and the input-aware
  `OrbitController` cover the current repeated 2D/3D pointer-control shapes
  without introducing a scene or action-binding system. The pan/zoom controller
  mutates `Camera2D`; `OrbitController` still models orbit input state that can
  be applied to a 3D camera when an example needs camera orbiting instead of
  object rotation.
- `cubey::host::GlfwSurface` owns the GLFW-created `VkSurfaceKHR`.
- `cubey::host::WindowedHost` owns the current windowed loop: instance, surface,
  device, submission coordinator, GPU runtime, swapchain, frame
  resources, frame timing, optional frame stats, acquire/record/submit/present,
  and swapchain recreation. It runs the GPU runtime threaded by default and uses
  explicit drain/wait calls at host-owned setup, update, swapchain-resource, and
  shutdown boundaries.
- Windowed render callbacks receive `cubey::host::WindowedRenderFrame`, which
  carries the command buffer, swapchain image index, timing, and
  `cubey::render::ColorTargetView` for the active swapchain image.
- All current windowed examples use the host layer: `window_clear`, `triangle`,
  `spinning_cube`, `textured_cube`, `shadow_cube`, `fractal`, and `particles`.
  They still own their shaders, pipelines, descriptors, command recording
  sequence, and example-specific state.
- `cubey::host::HeadlessPngHost` owns the repeated no-window Vulkan
  instance/device, submission coordinator, GPU runtime, offscreen RGBA target,
  color-attachment/readback transitions, ticketed RGBA8 image readback through
  `ProjectGpuServices`, and PNG write path without depending on GLFW. Its
  target view uses the same `cubey::render::ColorTargetView` vocabulary as the
  windowed path.
- `headless_render`, `fractal --headless`, and `fluid_2d --headless` use the
  headless host while keeping their resource setup, simulation/update work, and
  capture command recording sequence local.
- `cubey::Engine` is the first scoped root owner. It lives in the engine layer
  because it composes project runtime services, the render resource handle
  registry, and created `Scene` instances, but does not yet own window hosts,
  headless hosts, Vulkan instance/device setup, or app simulation state.
- Engine-created scenes validate renderable mesh/material handles against the
  Engine registry. Current cube examples use Engine-owned scenes, mutate scene
  transforms during `update()`, build CPU render frame plans during render, and
  keep Vulkan command recording sequence local.
- `cubey::ProjectRuntimeServices` now owns project-facing jobs, uploads,
  captures, GPU submission tickets, and deferred destruction.
- `cubey::ProjectRuntimeAdapter` wraps those services with the repeated
  host-bridge behavior: convert `FrameTiming` to one `ProjectFrame` per host
  frame, expose `ProjectContext`, and retire deferred destruction on shutdown.
  `fluid_2d` uses the adapter in both windowed and headless modes.
- `cubey::ProjectGpuServices` is the optional GPU-facing project bridge for
  draining queued uploads into owner-thread GPU work, marking upload completion
  or failure, routing queue-idle waits through `GpuRuntime`, and retiring
  deferred work by completed GPU submission tickets. It also owns project-level
  RGBA8 image readback tickets with pending/completed/failed status and
  completed pixel payload handoff.
- A generic project host is still deferred; the current contract is limited to
  shared service ownership, GPU bridge attachment, and frame bridging.

## Foundation Rules

- Design small foundation contracts when the graphics/runtime concept is
  durable, correctness-sensitive, or widely established. Do not require every
  shared boundary to first appear as duplicated project code.
- Use repeated host mechanics as useful evidence, not as the only reason to add
  library code.
- Keep command recording explicit in examples/projects.
- Keep lifecycle APIs small enough that `window_clear` and `triangle` remain
  readable.
- Prefer project-owned render intent over host-owned render policy.
- Add input/UI hosting when the contract is clear enough to keep projects
  cleaner, not only after the projects become messy.
- Keep the headless host narrow: no scene/render abstraction, no implicit
  project lifecycle, and no GLFW dependency.
- Build a full project lifecycle host only when its setup/update/render/resize
  contract is explicit enough to serve more than one project shape.

## Current Non-Goals

- Dedicated render thread.
- Split graphics/compute/present queue scheduler.
- Parallel command recording.
- ImGui/UI hosting.
- Backend parity work.

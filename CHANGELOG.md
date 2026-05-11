# Changelog

This file is the source of truth for release notes. Keep user-visible changes
under `Unreleased`; when a release is tagged, move the relevant entries into a
versioned section and use that section as the release notes.

## Unreleased

### Added

- C++20/CMake project bootstrap with presets, warning settings, formatting, and
  clang-tidy defaults.
- C++ style guide covering formatting, naming, Vulkan structure, and review
  priorities.
- Vulkan-first direction docs based on the WebGPU and Vulkan spikes.
- Threading and async design doc covering the async-ready runtime boundary,
  CPU job facade, Vulkan ownership rules, queued upload/capture direction, and
  deferred full multithreaded renderer work.
- Living roadmap and working-notes docs.
- MIT license, matching the original cubey branch.
- `cubey` static library target with public headers under `include/cubey/`.
- Public Vulkan `Instance` and `Device` types for validation/debug-utils
  setup, physical-device selection, logical-device ownership, and queue access.
- Public Vulkan `Swapchain`, `CommandPool`, and `FrameResources` components for
  swapchain image/view ownership, command-pool ownership, command-buffer
  allocation, and single-frame command/sync resources.
- Public Vulkan `ShaderModule` type and CMake GLSL-to-SPIR-V helper using
  `glslangValidator`.
- Public Vulkan `Buffer`, `Image`, `DepthAttachment`, `Sampler`, and
  `ImmediateCommands` components for device resources, generated texture paths,
  depth attachments, and setup uploads.
- Public Vulkan `CommandRecorder` helper for non-owning command-buffer
  recording: begin/end, dynamic rendering, image transitions, pipeline and
  descriptor binding, push constants, draws, indexed draws, and dispatches.
- Public Vulkan transfer/readback helpers for readback buffers, sampled image
  configs, buffer-image copies, and storage, transfer, sampled-image readback,
  color-attachment readback, and sampling image layout transitions.
- Public binary file I/O helpers and PNG image I/O helper backed by vendored
  `stb_image_write`.
- Public Vulkan pipeline and descriptor RAII types, plus descriptor write
  helpers, pipeline-layout create-info helpers, a compute pipeline create-info
  helper, and a dynamic graphics pipeline create-info helper for current color
  and depth-only dynamic-rendering paths.
- Public Vulkan descriptor set info and bundle helpers that own the repeated
  descriptor layout, pool, and single descriptor-set allocation path.
- Public Vulkan dynamic graphics pipeline blend controls and a storage-buffer
  descriptor write helper.
- Public GLM-backed `cubey::math` wrapper for matrix/vector types and the
  current Vulkan transform/projection conventions.
- Public Vulkan `RenderContext` component for explicit surface-backed
  begin/end frame lifecycle, acquire, command reset, submit, present, and
  swapchain-recreate signaling.
- Public Vulkan `SwapchainRecreateTracker` helper for guarding repeated
  out-of-date/suboptimal recreate loops.
- Public Vulkan image transition helpers and dynamic-rendering color/depth
  attachment setup helpers.
- GLSL shader helper support for shared include directories and dependency
  tracking.
- Public SPIR-V I/O helper for shader-backed examples and future projects.
- Public CPU job facade with a worker-backed `JobSystem`, deterministic
  `InlineExecutor`, and `JobHandle` result wrapper.
- Public PNG capture queue that moves completed RGBA pixels into job-backed
  encoding work and returns an explicit completion ticket.
- Public `HeadlessPngHost` for no-window Vulkan capture: instance/device setup,
  offscreen RGBA render target, capture transitions, readback, and PNG artifact
  writing behind project-provided render callbacks.
- Public upload request queue that owns submitted CPU bytes until the GPU owner
  drains them.
- Public frame ticket issuer and deferred destruction queue for retiring
  CPU-side cleanup actions after completed frame/submission points.
- Public async-ready project runtime vocabulary: project frames, extents,
  render packets, project context services, and `ProjectLike` concept checks.
- Public `ProjectRuntimeServices` owner for project-facing jobs, uploads,
  captures, frame tickets, deferred destruction, and `ProjectFrame` creation
  from frame timing.
- Public `ProjectRuntimeAdapter` for the narrow host bridge: same-frame
  `ProjectFrame` reuse, new-frame ticket issue, project context access, and
  deferred destruction retirement.
- Public frame timing, orbit-controller, and frame-stat types for interactive
  windowed examples.
- `examples/window_clear`, a minimal Vulkan/GLFW dynamic-rendering windowed
  clear/present smoke executable.
- `examples/triangle`, a minimal shader-backed Vulkan graphics pipeline smoke
  executable using dynamic rendering and `gl_VertexIndex`.
- `examples/spinning_cube`, a shader-generated cube smoke executable with push
  constants, animation, dynamic rendering, device-local vertex/index buffers,
  and a shared depth attachment helper.
- `examples/textured_cube`, an interactive shaded cube that generates texture
  data through a setup-time compute shader and samples it in the graphics pass
  with descriptor-backed scene uniforms and dynamic rendering.
- `examples/shadow_cube`, a two-pass directional shadow-map example using a
  sampled depth texture, depth-only dynamic rendering, orthographic light view,
  and explicit sampled-depth layout transitions.
- `examples/headless_render`, a no-window Vulkan smoke that renders an
  offscreen color target, reads it back, and writes a PNG artifact.
- `examples/fractal`, a fullscreen Mandelbrot-style shader smoke with
  example-local pan/zoom/reset navigation and headless PNG output.
- `examples/particles`, a compute-updated attractor particle smoke rendered as
  instanced screen-facing quads with procedural Gaussian splats and additive
  blending.
- `projects/fluid_2d`, the first project target: a 2D dye-and-velocity compute
  simulation with injection/advection passes, project-local pressure
  projection, pointer injection, pause/reset controls, debug render modes,
  fullscreen rendering, config tests, windowed smoke, and deterministic
  headless PNG smoke.
- CTest smoke that accepts either successful window startup or the known
  no-display GLFW failure in terminal sessions.
- CTest smoke for headless PNG artifact creation and PNG signature validation.
- Shared CMake CTest smoke helpers for windowed and headless example targets.
- Optional `cubey_app` target with a GLFW window/surface host, pointer/key input
  dispatch, and the shared windowed app loop.
- Public `cubey::render` pass planning, sampled depth texture, and depth-only
  rendering-info helpers for multi-view/multipass examples.
- Public Vulkan queue submit, sampled-depth transition, sampler config, and
  depth-only dynamic graphics pipeline support for shadow-map style work.

### Changed

- Cubey 2.0 is framed as a ground-up native Vulkan workbench rather than an
  OpenGL continuation or WebGPU-first rewrite.
- Runnable targets are explicit examples/projects rather than a generic `cubey`
  executable.
- Windowed examples now use dynamic rendering instead of classic render
  passes/framebuffers while keeping command recording and render-resource policy
  local.
- Windowed examples, `headless_render`, and `fluid_2d` now use the shared
  Vulkan command recorder for repeated command-buffer calls while keeping pass
  order, barriers, descriptors, pipelines, and render policy local.
- Graphics examples now share dynamic graphics pipeline create-info setup while
  retaining explicit example-local layout, shader, vertex-input, and descriptor
  choices.
- Cube examples now share device-local buffer upload and depth attachment setup
  helpers instead of carrying local staging-copy and depth-image code.
- `textured_cube` now shares descriptor layout/pool/write helpers plus compute
  pipeline and pipeline-layout create-info helpers instead of carrying raw
  descriptor and compute setup blocks.
- `textured_cube` and `particles` now use the descriptor set bundle helper
  instead of carrying separate descriptor layout, pool, and allocation members.
- `textured_cube` now uses shared storage-image transition and generated
  sampled-image config helpers for its compute-generated texture.
- `spinning_cube` and `textured_cube` now use shared Cubey math helpers instead
  of carrying duplicate local matrix code.
- Windowed examples now share GLFW windowing, Vulkan surface creation,
  acquire/present behavior, resize handling, frame timing, and swapchain
  recreation through `cubey_app`.
- Shader-backed examples now share SPIR-V I/O layered on generic binary file
  reads instead of carrying local file readers.
- Example CTest targets now use shared CMake smoke helpers instead of repeated
  shell snippets.
- Headless PNG smoke tests now apply a narrow LeakSanitizer suppression for
  DBus allocations left alive by the Vulkan loader/driver path on Linux.
- Roadmap and Vulkan abstraction docs now frame the app/runtime host as the
  standard windowed-example path while still deferring renderer and scene-system
  abstractions.
- The particle rewrite is currently categorized as an example-sized reference
  program rather than a first-class `projects/` target.
- `window_clear`, `triangle`, `spinning_cube`, `textured_cube`, `fractal`, and
  `particles` now use the shared GLFW/windowed app host while keeping command
  recording sequence and render resources example-local.
- `headless_render`, `fractal --headless`, and `fluid_2d --headless` now use
  the shared no-GLFW headless PNG host while keeping resource setup, simulation,
  and command recording sequence local to each runnable.
- `fluid_2d` simulation steps now consume `ProjectFrame` values from shared
  project runtime adapter in both windowed and headless modes, while keeping
  Vulkan command recording sequence and resource policy project-local.
- `Camera3D` now supports orthographic projection in addition to perspective
  projection.

## Pre-2.0 History

- The original cubey codebase remains preserved on the `master` branch as an
  OpenGL 4 shader playground.

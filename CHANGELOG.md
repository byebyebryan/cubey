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
- Public Vulkan transfer/readback helpers for readback buffers, sampled image
  configs, buffer-image copies, and storage, transfer, sampled-image readback,
  color-attachment readback, and sampling image layout transitions.
- Public PNG output helper backed by vendored `stb_image_write`.
- Public Vulkan pipeline and descriptor RAII types, plus descriptor write
  helpers, pipeline-layout create-info helpers, a compute pipeline create-info
  helper, and a dynamic graphics pipeline create-info helper for the current
  single-color-attachment path.
- Public Vulkan `RenderContext` component for explicit surface-backed
  begin/end frame lifecycle, acquire, command reset, submit, present, and
  swapchain-recreate signaling.
- Public Vulkan `SwapchainRecreateTracker` helper for guarding repeated
  out-of-date/suboptimal recreate loops.
- Public Vulkan rendering helpers for current image layout transitions and
  dynamic-rendering color/depth attachment setup.
- GLSL shader helper support for shared include directories and dependency
  tracking.
- Public SPIR-V file-loading helper for shader-backed examples and future
  projects.
- Public CPU job facade with a worker-backed `JobSystem`, deterministic
  `InlineExecutor`, and `JobHandle` result wrapper.
- Public PNG capture queue that moves completed RGBA pixels into job-backed
  encoding work and returns an explicit completion ticket.
- Public upload request queue that owns submitted CPU bytes until the GPU owner
  drains them.
- Public frame ticket issuer and deferred destruction queue for retiring
  CPU-side cleanup actions after completed frame/submission points.
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
- `examples/headless_render`, a no-window Vulkan smoke that renders an
  offscreen color target, reads it back, and writes a PNG artifact.
- `examples/fractal`, a fullscreen Mandelbrot-style shader smoke with
  example-local pan/zoom/reset navigation and headless PNG output.
- CTest smoke that accepts either successful window startup or the known
  no-display GLFW failure in terminal sessions.
- CTest smoke for headless PNG artifact creation and PNG signature validation.
- Shared CMake CTest smoke helpers for windowed and headless example targets.

### Changed

- Cubey 2.0 is framed as a ground-up native Vulkan workbench rather than an
  OpenGL continuation or WebGPU-first rewrite.
- Runnable targets are explicit examples/projects rather than a generic `cubey`
  executable.
- Windowed examples now use dynamic rendering instead of classic render
  passes/framebuffers while keeping GLFW, surface creation, command recording,
  acquire/present behavior, and resize policy local.
- Graphics examples now share dynamic graphics pipeline create-info setup while
  retaining explicit example-local layout, shader, vertex-input, and descriptor
  choices.
- Cube examples now share device-local buffer upload and depth attachment setup
  helpers instead of carrying local staging-copy and depth-image code.
- `textured_cube` now shares descriptor layout/pool/write helpers plus compute
  pipeline and pipeline-layout create-info helpers instead of carrying raw
  descriptor and compute setup blocks.
- `textured_cube` now uses shared storage-image transition and generated
  sampled-image config helpers for its compute-generated texture.
- Windowed examples now share recreate-attempt tracking while still owning their
  swapchain resource rebuild steps.
- Shader-backed examples now share SPIR-V file loading instead of carrying
  local file readers.
- Example CTest targets now use shared CMake smoke helpers instead of repeated
  shell snippets.
- Roadmap and Vulkan abstraction docs now route the next framework checkpoint
  toward a real project before a broader app/runtime host.

## Pre-2.0 History

- The original cubey codebase remains preserved on the `master` branch as an
  OpenGL 4 shader playground.

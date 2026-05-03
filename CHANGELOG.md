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
- Public Vulkan `Buffer`, `Image`, `Sampler`, and `ImmediateCommands`
  components for device resources, generated texture paths, and setup uploads.
- Public Vulkan pipeline and descriptor RAII types that accept explicit
  Vulkan create-info structs.
- Public Vulkan `RenderContext` component for explicit surface-backed
  begin/end frame lifecycle, acquire, command reset, submit, present, and
  swapchain-recreate signaling.
- Public Vulkan rendering helpers for current image layout transitions and
  dynamic-rendering color/depth attachment setup.
- GLSL shader helper support for shared include directories and dependency
  tracking.
- Public frame timing, orbit-controller, and frame-stat types for interactive
  windowed examples.
- `examples/window_clear`, a minimal Vulkan/GLFW dynamic-rendering windowed
  clear/present smoke executable.
- `examples/triangle`, a minimal shader-backed Vulkan graphics pipeline smoke
  executable using dynamic rendering and `gl_VertexIndex`.
- `examples/spinning_cube`, a shader-generated cube smoke executable with push
  constants, animation, dynamic rendering, and an example-local depth
  attachment.
- `examples/textured_cube`, an interactive shaded cube that generates texture
  data through a setup-time compute shader and samples it in the graphics pass
  with descriptor-backed scene uniforms and dynamic rendering.
- CTest smoke that accepts either successful window startup or the known
  no-display GLFW failure in terminal sessions.

### Changed

- Cubey 2.0 is framed as a ground-up native Vulkan workbench rather than an
  OpenGL continuation or WebGPU-first rewrite.
- Runnable targets are explicit examples/projects rather than a generic `cubey`
  executable.
- Windowed examples now use dynamic rendering instead of classic render
  passes/framebuffers while keeping GLFW, surface creation, pipeline setup,
  depth attachments, command recording, acquire/present behavior, and resize
  policy local until the repeated shape is clearer.

## Pre-2.0 History

- The original cubey codebase remains preserved on the `master` branch as an
  OpenGL 4 shader playground.

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
- Vulkan-first renderer direction docs based on the WebGPU and Vulkan spikes.
- Living roadmap and working-notes docs.
- MIT license, matching the original cubey branch.
- `cubey` static library target with public headers under `include/cubey/`.
- Public Vulkan `Instance` and `Device` primitives for validation/debug-utils
  setup, physical-device selection, logical-device ownership, and queue access.
- Public Vulkan `Swapchain` and `FrameResources` primitives for swapchain
  image/view ownership and single-frame command/sync resources.
- Public Vulkan `ShaderModule` primitive and CMake GLSL-to-SPIR-V helper using
  `glslangValidator`.
- `examples/window_clear`, a minimal Vulkan/GLFW visible-surface clear/present
  smoke executable.
- `examples/triangle`, a minimal shader-backed Vulkan graphics pipeline smoke
  executable using `gl_VertexIndex`.
- CTest smoke that accepts either successful window startup or the known
  no-display GLFW failure in terminal sessions.

### Changed

- Cubey 2.0 is framed as a ground-up native Vulkan workbench rather than an
  OpenGL continuation or WebGPU-first rewrite.
- Runnable targets are explicit examples/projects rather than a generic `cubey`
  executable.
- `examples/window_clear` now uses reusable `cubey::vulkan` instance, device,
  swapchain, and frame-resource primitives while keeping GLFW, surface creation,
  render pass, framebuffers, command recording, acquire/present behavior, and
  resize policy example-local.
- Shader-backed examples keep render pass, pipeline, framebuffers, command
  recording, acquire/present behavior, and resize policy local until the
  repeated shape is clearer.

## Pre-2.0 History

- The original cubey codebase remains preserved on the `master` branch as an
  OpenGL 4 shader playground.

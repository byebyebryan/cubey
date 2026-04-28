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

### Changed

- Cubey 2.0 is framed as a ground-up native Vulkan workbench rather than an
  OpenGL continuation or WebGPU-first rewrite.

## Pre-2.0 History

- The original cubey codebase remains preserved on the `master` branch as an
  OpenGL 4 shader playground.

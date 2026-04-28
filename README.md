# cubey

A personal GPU workbench for procedural graphics experiments and demos. Minimal
C++ framework, maximum shader work.

The original cubey explored OpenGL/GPGPU demos such as fluid simulation,
particles, marching cubes, fractals, and camera tests. Cubey 2.0 is a ground-up
rewrite using a modern native GPU stack.

## Current Direction

Primary target: **native Vulkan on desktop**.

The WebGPU/Dawn spike was useful and remains a reference for possible browser
demos, but it is not the foundation for the main renderer. The Vulkan spike
proved the core surface, compute, render, validation, resize, and headless
workflows on the target machine with a lighter dependency footprint and better
fit for Cubey's native-workbench goals.

See:

- [Design document](docs/DESIGN.md)
- [Spike findings and decision record](docs/spike-findings.md)
- [C++ style guide](docs/cpp-style.md)

## Spike Branches

| Branch | Purpose | Status |
| --- | --- | --- |
| `webgpu` | Dawn native plus emdawnwebgpu browser experiment | Successful API/prototyping spike; not the primary path |
| `vulkan` | Native Vulkan visible/headless experiment | Successful; informs the mainline renderer direction |

Main remains docs-first until the Vulkan spike is reshaped into a maintainable
foundation.

## Development Setup

Use the CMake presets as the default entrypoint:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The repo also includes:

- `.clang-format` for C++ formatting
- `.clang-tidy` for static-analysis defaults
- `.editorconfig` and `.gitattributes` for stable text formatting
- `asan` and `tidy` CMake presets for sanitizer and clang-tidy builds

For non-mechanical naming, ownership, and Vulkan structure conventions, use the
[Cubey C++ style guide](docs/cpp-style.md).

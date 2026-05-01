# cubey

A personal GPU workbench for procedural graphics experiments and projects.
Minimal C++ library, maximum shader work.

The original cubey explored OpenGL/GPGPU demos such as fluid simulation,
particles, marching cubes, fractals, and camera tests. Cubey 2.0 is a ground-up
rewrite using a modern native GPU stack.

## Current Direction

Primary target: **native Vulkan on desktop**.

The WebGPU/Dawn spike was useful and remains a reference for possible browser
showcases, but it is not the foundation for the main renderer. The Vulkan spike
proved the core surface, compute, render, validation, resize, and headless
workflows on the target machine with a lighter dependency footprint and better
fit for Cubey's native-workbench goals.

The repo is structured around a primary `cubey` C++ library. Runnable targets
are explicit examples or projects rather than a generic `cubey` executable.

See:

- [Design document](docs/DESIGN.md)
- [Roadmap](docs/roadmap.md)
- [Working notes](docs/working-notes.md)
- [Spike findings and decision record](docs/spike-findings.md)
- [C++ style guide](docs/cpp-style.md)
- [Changelog / release notes](CHANGELOG.md)

## Spike Branches

| Branch | Purpose | Status |
| --- | --- | --- |
| `webgpu` | Dawn native plus emdawnwebgpu browser experiment | Successful API/prototyping spike; not the primary path |
| `vulkan` | Native Vulkan visible/headless experiment | Successful; informs the mainline renderer direction |

Main now contains the first visible-surface slice: the `cubey` library plus a
minimal `examples/window_clear` executable. The example owns its clear/present
app code; reusable Vulkan pieces should move into `cubey` only after they are
shaped as library primitives. The spike branches remain reference material for
later compute, textured-cube, headless, and browser work.

## Development Setup

Use the CMake presets as the default entrypoint:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The first visible-surface smoke target is:

```bash
./build/dev/examples/window_clear/window_clear --frames 300 --width 1280 --height 720
```

Use validation as a hard requirement when the validation layers are installed:

```bash
./build/dev/examples/window_clear/window_clear --require-validation --frames 300 --width 1280 --height 720
```

The repo also includes:

- `.clang-format` for C++ formatting
- `.clang-tidy` for static-analysis defaults
- `.editorconfig` and `.gitattributes` for stable text formatting
- `asan` and `tidy` CMake presets for sanitizer and clang-tidy builds

For non-mechanical naming, ownership, and Vulkan structure conventions, use the
[Cubey C++ style guide](docs/cpp-style.md).

## License

Cubey is licensed under the [MIT License](LICENSE), matching the original
project branch.

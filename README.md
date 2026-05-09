# cubey

A personal GPU workbench for procedural graphics experiments and projects.
Minimal C++ library, maximum shader work.

The original cubey explored OpenGL/GPGPU demos such as fluid simulation,
particles, marching cubes, fractals, and camera tests. Cubey 2.0 is a ground-up
rewrite using a modern native GPU stack.

## Current Direction

Primary target: **native Vulkan on desktop**.

The WebGPU/Dawn spike was useful and remains a reference for possible browser
showcases, but it is not the foundation for the main Vulkan path. The Vulkan
spike proved the core surface, compute, rendering, validation, resize, and
headless workflows on the target machine with a lighter dependency footprint and
better fit for Cubey's native-workbench goals.

The repo is structured around a primary `cubey` C++ library. Runnable targets
are explicit examples or projects rather than a generic `cubey` executable.

See:

- [Design document](docs/DESIGN.md)
- [Roadmap](docs/roadmap.md)
- [Vulkan abstraction map](docs/vulkan-abstractions.md)
- [App runtime direction](docs/app-runtime.md)
- [Threading and async design](docs/threading-and-async.md)
- [Fluid simulation direction](docs/fluid-simulation.md)
- [Working notes](docs/working-notes.md)
- [Spike findings and decision record](docs/spike-findings.md)
- [C++ style guide](docs/cpp-style.md)
- [Changelog / release notes](CHANGELOG.md)

Project-local design notes live beside their targets:

- [Fluid 2D project](projects/fluid_2d/README.md)
- [Fluid 2.5D project design](projects/fluid_25d/README.md)

## Spike Branches

| Branch | Purpose | Status |
| --- | --- | --- |
| `webgpu` | Dawn native plus emdawnwebgpu browser experiment | Successful API/prototyping spike; not the primary path |
| `vulkan` | Native Vulkan windowed/headless experiment | Successful; informs the mainline Vulkan layer |

Main now contains windowed examples: `examples/window_clear`
for dynamic-rendering clear/present and `examples/triangle` for build-time GLSL
shaders plus dynamic-rendering graphics pipeline setup.
`examples/spinning_cube` adds device-local vertex/index buffers, push
constants, per-frame animation, and a depth attachment through dynamic
rendering.
`examples/textured_cube` adds a compute-generated texture, descriptor-backed
scene uniforms, normals, shared GLSL lighting, image/sampler ownership, and
fragment-shader sampling through dynamic rendering.
`examples/headless_render` adds the first no-window artifact path: it renders
an offscreen color target through dynamic rendering, reads the image back, and
writes an inspectable PNG.
`examples/fractal` adds a fullscreen Mandelbrot-style shader path with windowed
navigation and a headless PNG mode.
`examples/particles` adds compute-updated attractor particles rendered as
instanced screen-facing quads with procedural Gaussian splats.
`projects/fluid_2d` is the first project target: a 2D dye-and-velocity field
with compute injection/advection, pressure projection, fullscreen rendering,
and deterministic headless PNG output.
`cubey` owns the reusable GLM-backed math wrapper, explicit 2D/3D transform
value types, Vulkan instance, device, buffer, image, sampler, swapchain,
shader-module, command-pool, image-transition and dynamic-rendering helpers,
frame clock, 2D/3D camera helpers, orbit-controller, CPU job facade, PNG capture queue,
upload request queue,
frame tickets/deferred destruction, async-ready project runtime vocabulary,
project runtime services and adapter,
binary file I/O, SPIR-V file loading, pipeline ownership, dynamic graphics
pipeline setup including blend state, descriptor setup/write helpers including
descriptor set bundles and storage buffers, compute pipeline setup, depth
attachment setup, texture transfer/readback helpers, PNG image I/O helper,
headless PNG host,
shared shader includes, `RenderContext` surface-backed begin/end frame
lifecycle, single-frame
command/sync components, and swapchain recreate-attempt tracking; examples still
own command recording and render policy. `cubey::input` owns the shared
keyboard/mouse frame snapshot plus pointer-drag and 2D pan/zoom helpers, while
the 2D pan/zoom controller mutates `Camera2D` and `OrbitController` consumes
that input snapshot for the current 3D orbit-control path.
`cubey_app` owns the GLFW-backed window/app host layer used by all current
windowed examples and the first windowed project. The base `cubey` target owns a
separate no-GLFW `HeadlessPngHost` used by current headless examples and the
first project. The spike branches remain reference material for deeper compute
and browser work.

Cubey's foundation should be designed deliberately. Graphics/runtime concepts
with durable contracts, such as resources, synchronization, transforms,
cameras, descriptors, render targets, frame flow, and project boundaries, may
belong in the shared library before multiple projects duplicate them. The guard
rail is not "wait for pressure"; it is "build explicit, tested graphics
contracts without turning Cubey into a generic game engine, editor, or
backend-agnostic renderer."

When shaping new library boundaries, prefer established graphics terminology
and public precedent over invented vocabulary. Useful references include
Filament's engine/material/view/resource split, Godot's open rendering and
scene APIs, Unity and Unreal's public component/transform/camera/rendering
contracts, and Khronos Vulkan terminology.

## Development Setup

Use the CMake presets as the default entrypoint:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Shader examples require `glslangValidator` at build time. GLM is used through
the public `cubey::math` wrapper and is resolved by CMake with `find_package`
or a FetchContent fallback.

The windowed smoke targets are:

```bash
./build/dev/examples/window_clear/window_clear --frames 300 --width 1280 --height 720
./build/dev/examples/triangle/triangle --frames 300 --width 1280 --height 720
./build/dev/examples/spinning_cube/spinning_cube --frames 300 --width 1280 --height 720
./build/dev/examples/textured_cube/textured_cube --frames 300 --width 1280 --height 720
./build/dev/examples/fractal/fractal --frames 300 --width 1280 --height 720
./build/dev/examples/particles/particles --frames 300 --width 1280 --height 720
./build/dev/projects/fluid_2d/fluid_2d --frames 300 --width 1280 --height 720
```

The headless PNG smokes are:

```bash
./build/dev/examples/headless_render/headless_render --width 640 --height 360 --output /tmp/cubey-headless.png
./build/dev/examples/fractal/fractal --headless --width 640 --height 360 --output /tmp/cubey-fractal.png
./build/dev/projects/fluid_2d/fluid_2d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-2d.png
```

Use validation as a hard requirement when the validation layers are installed:

```bash
./build/dev/examples/window_clear/window_clear --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/triangle/triangle --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/spinning_cube/spinning_cube --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/textured_cube/textured_cube --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/fractal/fractal --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/particles/particles --require-validation --frames 300 --width 1280 --height 720
./build/dev/projects/fluid_2d/fluid_2d --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/headless_render/headless_render --require-validation --width 640 --height 360 --output /tmp/cubey-headless.png
./build/dev/examples/fractal/fractal --headless --require-validation --width 640 --height 360 --output /tmp/cubey-fractal.png
./build/dev/projects/fluid_2d/fluid_2d --headless --require-validation --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-2d.png
```

`spinning_cube` and `textured_cube` share the reusable `Transform3D` model
matrix helper plus the orbit-camera view/projection helper. `textured_cube`
supports basic interaction: left-drag rotates the shaded compute-textured cube,
Space pauses/resumes auto-rotation, `R` resets the view, and Escape closes the
window. Its window title periodically reports FPS, frame time, swapchain extent,
triangle count, and pixel rate.

`fractal` supports basic `Camera2D` navigation: left-drag pans, mouse wheel
zooms around the cursor, `R` resets the view, and Escape closes the window.

`particles` supports basic controls: Space pauses/resumes compute updates, `R`
resets the particle field, and Escape closes the window.

`fluid_2d` currently runs a compute-updated dye field with project-local
pressure projection. Left-drag injects dye and force, Space pauses/resumes
simulation, `R` resets the field, `D` cycles dye/velocity/divergence/pressure
views, and Escape closes the window. Headless mode remains deterministic.

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

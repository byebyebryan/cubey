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
- [Working notes](docs/working-notes.md)
- [Spike findings and decision record](docs/spike-findings.md)
- [C++ style guide](docs/cpp-style.md)
- [Changelog / release notes](CHANGELOG.md)

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
`cubey` owns the reusable Vulkan instance, device, buffer, image, sampler,
swapchain, shader-module, command-pool, image-transition and dynamic-rendering
helpers, frame clock, orbit-controller, CPU job facade, PNG capture queue,
upload request queue,
frame tickets/deferred destruction, async-ready project runtime vocabulary,
binary file I/O, SPIR-V file loading, pipeline ownership, dynamic graphics
pipeline setup including blend state, descriptor setup/write helpers including
storage buffers, compute pipeline setup, depth
attachment setup, texture transfer/readback helpers, PNG image I/O helper,
shared shader includes,
`RenderContext` surface-backed begin/end frame lifecycle, single-frame
command/sync components, and swapchain recreate-attempt tracking; examples still
own command recording and render policy.
`cubey_app` owns the GLFW-backed window/app host layer used by all current
windowed examples: `window_clear`, `triangle`, `spinning_cube`,
`textured_cube`, `fractal`, and `particles`. Headless paths remain explicit
until a shared headless host shape is proven. The spike branches remain
reference material for deeper compute and browser work.

The next framework checkpoint should come from a first real project such as
fluid simulation or marching cubes. The current examples now justify the
windowed host layer, but still do not justify a renderer, scene system, or broad
backend abstraction.

## Development Setup

Use the CMake presets as the default entrypoint:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Shader examples require `glslangValidator` at build time.

The windowed smoke targets are:

```bash
./build/dev/examples/window_clear/window_clear --frames 300 --width 1280 --height 720
./build/dev/examples/triangle/triangle --frames 300 --width 1280 --height 720
./build/dev/examples/spinning_cube/spinning_cube --frames 300 --width 1280 --height 720
./build/dev/examples/textured_cube/textured_cube --frames 300 --width 1280 --height 720
./build/dev/examples/fractal/fractal --frames 300 --width 1280 --height 720
./build/dev/examples/particles/particles --frames 300 --width 1280 --height 720
```

The headless PNG smokes are:

```bash
./build/dev/examples/headless_render/headless_render --width 640 --height 360 --output /tmp/cubey-headless.png
./build/dev/examples/fractal/fractal --headless --width 640 --height 360 --output /tmp/cubey-fractal.png
```

Use validation as a hard requirement when the validation layers are installed:

```bash
./build/dev/examples/window_clear/window_clear --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/triangle/triangle --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/spinning_cube/spinning_cube --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/textured_cube/textured_cube --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/fractal/fractal --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/particles/particles --require-validation --frames 300 --width 1280 --height 720
./build/dev/examples/headless_render/headless_render --require-validation --width 640 --height 360 --output /tmp/cubey-headless.png
./build/dev/examples/fractal/fractal --headless --require-validation --width 640 --height 360 --output /tmp/cubey-fractal.png
```

`textured_cube` supports basic interaction: left-drag rotates the shaded
compute-textured cube, Space pauses/resumes auto-rotation, `R` resets the view,
and Escape closes the window. Its window title periodically reports FPS, frame
time, swapchain extent, triangle count, and pixel rate.

`fractal` supports basic navigation: left-drag pans, mouse wheel zooms around
the cursor, `R` resets the view, and Escape closes the window.

`particles` supports basic controls: Space pauses/resumes compute updates, `R`
resets the particle field, and Escape closes the window.

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

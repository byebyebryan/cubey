# cubey

Cubey is a native desktop GPU workbench for procedural graphics experiments and
projects. The goal is a small, deliberate C++/Vulkan foundation that keeps
runtime, resource, input, camera, transform, and project boundaries explicit
while leaving the interesting work in shaders, compute, and project code.

Cubey is not a generic game engine, editor, SDK, or backend-agnostic renderer.
It should still use established graphics terminology and proven public
precedent when shaping new foundation contracts.

## Current Direction

- Primary target: native Vulkan on desktop.
- Primary library: `cubey`, with public headers under `include/cubey/`.
- Optional app layer: `cubey_app`, for GLFW-backed window hosting.
- Runnable targets live under `examples/` or `projects/`.
- Headless PNG output is a first-class verification path for projects that can
  render without a window.

Current examples:

- `window_clear`: minimal dynamic-rendering clear/present path.
- `triangle`: build-time GLSL shaders and dynamic graphics pipeline setup.
- `spinning_cube`: indexed cube with shared transform/camera math and depth.
- `textured_cube`: compute-generated texture, descriptors, lighting, and input.
- `headless_render`: no-window offscreen PNG path.
- `fractal`: fullscreen Mandelbrot-style shader with windowed navigation and
  headless output.
- `particles`: compute-updated attractor particles rendered as instanced splats.

Current projects:

- `fluid_2d`: compute-updated dye/velocity field with injection, advection,
  pressure projection, debug views, and deterministic headless PNG output.

## Documentation

Start with the [docs index](docs/README.md).

Authoritative current docs:

- [Design](docs/DESIGN.md)
- [Roadmap](docs/roadmap.md)
- [Vulkan abstraction map](docs/vulkan-abstractions.md)
- [App runtime](docs/app-runtime.md)
- [Threading and async](docs/threading-and-async.md)
- [Fluid simulation direction](docs/fluid-simulation.md)
- [C++ style guide](docs/cpp-style.md)
- [Changelog / release notes](CHANGELOG.md)

Project-local docs:

- [Fluid 2D](projects/fluid_2d/README.md)
- [Fluid 2.5D design](projects/fluid_25d/README.md)

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

Useful windowed smokes:

```bash
./build/dev/examples/spinning_cube/spinning_cube --frames 300 --width 1280 --height 720
./build/dev/examples/textured_cube/textured_cube --frames 300 --width 1280 --height 720
./build/dev/examples/fractal/fractal --frames 300 --width 1280 --height 720
./build/dev/examples/particles/particles --frames 300 --width 1280 --height 720
./build/dev/projects/fluid_2d/fluid_2d --frames 300 --width 1280 --height 720
```

Useful headless PNG smokes:

```bash
./build/dev/examples/headless_render/headless_render --width 640 --height 360 --output /tmp/cubey-headless.png
./build/dev/examples/fractal/fractal --headless --width 640 --height 360 --output /tmp/cubey-fractal.png
./build/dev/projects/fluid_2d/fluid_2d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-2d.png
```

Use `--require-validation` on local smoke commands when Vulkan validation
layers are installed.

## Controls

- `textured_cube`: left-drag rotates, Space pauses/resumes auto-rotation, `R`
  resets, Escape closes.
- `fractal`: left-drag pans, mouse wheel zooms around the cursor, `R` resets,
  Escape closes.
- `particles`: Space pauses/resumes compute updates, `R` resets, Escape closes.
- `fluid_2d`: left-drag injects dye/force, Space pauses/resumes, `R` resets,
  `D` cycles dye/velocity/divergence/pressure views, Escape closes.

## License

Cubey is licensed under the [MIT License](LICENSE).

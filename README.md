# cubey

Cubey is a native desktop GPU workbench for procedural graphics experiments and
projects. The goal is a small, deliberate C++/Vulkan foundation that keeps
engine, host, resource, input, camera, transform, and project boundaries explicit
while leaving the interesting work in shaders, compute, and project code.

Cubey is not a generic game engine, editor, SDK, or backend-agnostic renderer.
It should still use established graphics terminology and proven public
precedent when shaping new foundation contracts.

## Current Direction

- Primary target: native Vulkan on desktop.
- Primary library: layered `cubey::*` targets with public headers under
  `include/cubey/` and an aggregate `cubey::cubey` target for examples and
  projects.
- Optional host layer: `cubey_host`, for GLFW-backed window hosting.
- Runnable targets live under `examples/` or `projects/`; examples are
  intentionally cube-focused renderer demos, while richer/non-cube work lives
  under `projects/`.
- Headless PNG output is a first-class verification path for projects that can
  render without a window.

Current examples:

- `spinning_cube`: primitive cube mesh with shared transform/camera math and depth.
- `textured_cube`: primitive cube mesh, compute-generated texture, descriptors,
  scene lighting, and input.
- `shadow_cube`: primitive cube/plane meshes, graph-declared directional shadow
  map, transient scene color target, and fullscreen triangle present pass.
- `instanced_cubes`: real instance-rate vertex input with one cube mesh and many
  cube instances.
- `material_cubes`: multiple material handles and material instances bound per
  scene draw packet.
- `headless_cube`: no-window offscreen cube PNG path.
- `particle_cubes`: compute-updated cube particles rendered as indexed cube
  instances.

Current projects:

- `fluid_2d`: compute-updated dye/velocity field with injection, advection,
  pressure projection, debug views, and deterministic headless PNG output.
- `fractal_2d`: fullscreen Mandelbrot-style shader with windowed navigation and
  headless output.

## Documentation

Start with the [docs index](docs/README.md).

Authoritative current docs:

- [Design](docs/DESIGN.md)
- [Roadmap](docs/roadmap.md)
- [Architecture notes](docs/architecture/README.md)
- [Vulkan abstraction map](docs/architecture/vulkan-abstractions.md)
- [Renderer foundation](docs/architecture/renderer-foundation.md)
- [Render graph direction](docs/architecture/render-graph.md)
- [Entity and component foundation](docs/architecture/entity-component-foundation.md)
- [Host and engine](docs/architecture/host-engine.md)
- [Threading and async](docs/architecture/threading-and-async.md)
- [Fluid simulation direction](docs/architecture/fluid-simulation.md)
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
./build/dev/examples/shadow_cube/shadow_cube --frames 300 --width 1280 --height 720
./build/dev/examples/instanced_cubes/instanced_cubes --frames 300 --width 1280 --height 720
./build/dev/examples/material_cubes/material_cubes --frames 300 --width 1280 --height 720
./build/dev/examples/particle_cubes/particle_cubes --frames 300 --width 1280 --height 720
./build/dev/projects/fractal_2d/fractal_2d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid_2d/fluid_2d --frames 300 --width 1280 --height 720
```

Useful headless PNG smokes:

```bash
./build/dev/examples/headless_cube/headless_cube --width 640 --height 360 --output /tmp/cubey-headless-cube.png
./build/dev/projects/fractal_2d/fractal_2d --headless --width 640 --height 360 --output /tmp/cubey-fractal-2d.png
./build/dev/projects/fluid_2d/fluid_2d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-2d.png
```

Use `--require-validation` on local smoke commands when Vulkan validation
layers are installed.

## Controls

- `textured_cube`: left-drag rotates, Space pauses/resumes auto-rotation, `R`
  resets, Escape closes.
- `shadow_cube`: left-drag orbits the camera, Escape closes.
- `instanced_cubes`: left-drag orbits the camera, Space pauses/resumes
  auto-rotation, `R` resets, Escape closes.
- `material_cubes`: left-drag orbits the camera, Space pauses/resumes
  auto-rotation, `R` resets, Escape closes.
- `particle_cubes`: Space pauses/resumes compute updates, `R` resets, Escape
  closes.
- `fractal_2d`: left-drag pans, mouse wheel zooms around the cursor, `R` resets,
  Escape closes.
- `fluid_2d`: left-drag injects dye/force, Space pauses/resumes, `R` resets,
  `D` cycles dye/velocity/divergence/pressure views, Escape closes.

## License

Cubey is licensed under the [MIT License](LICENSE).

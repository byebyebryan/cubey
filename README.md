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
- Headless PNG and optional MP4 output are first-class verification/capture
  paths for projects that can render without a window.

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
- `headless_cube`: no-window offscreen cube PNG/MP4 capture path.
- `particle_cubes`: compute-updated cube particles rendered as indexed cube
  instances.

Current projects:

- `fluid_2d`: compute-updated dye/velocity field with injection, advection,
  pressure projection, debug views, and deterministic headless capture output.
- `fractal_2d`: fullscreen Mandelbrot-style shader with windowed navigation and
  headless output.
- `gltf_viewer`: glTF/glb viewer for imported assets, PBR materials, texture
  upload, rigid/morph/skinned animation, generated or HDR-backed IBL, skybox
  rendering, shadow maps, and headless capture.
- `pbr_furnace`: white-furnace PBR validation scene for roughness/metallic
  behavior under uniform generated IBL.

## Documentation

Start with the [docs index](docs/README.md).

Authoritative current docs:

- [Design](docs/DESIGN.md)
- [Roadmap](docs/roadmap.md)
- [Architecture notes](docs/architecture/README.md)
- [Vulkan abstraction map](docs/architecture/vulkan-abstractions.md)
- [Renderer foundation](docs/architecture/renderer-foundation.md)
- [PBR and IBL direction](docs/architecture/pbr-ibl.md)
- [Render graph direction](docs/architecture/render-graph.md)
- [Entity and component foundation](docs/architecture/entity-component-foundation.md)
- [Host and engine](docs/architecture/host-engine.md)
- [Threading and async](docs/architecture/threading-and-async.md)
- [glTF assets and PBR](docs/architecture/gltf-assets.md)
- [Fluid simulation direction](docs/architecture/fluid-simulation.md)
- [C++ style guide](docs/cpp-style.md)
- [Changelog / release notes](CHANGELOG.md)

Project-local docs:

- [Fluid 2D](projects/fluid_2d/README.md)
- [Fluid 2.5D design](projects/fluid_25d/README.md)

## Development Setup

Cubey needs a native C++20 toolchain plus system Vulkan development packages.
CMake fetches several project dependencies when needed, but it does not provide
the Vulkan SDK/loader, GPU driver, compiler toolchain, or shader compiler.

Required system dependencies:

- C++20 compiler and standard build tools.
- CMake, Ninja, and Git.
- Vulkan headers.
- Vulkan loader / ICD loader (`libvulkan.so` on Linux).
- A Vulkan-capable GPU driver / ICD for your hardware.
- `glslangValidator` for build-time GLSL to SPIR-V shader compilation.

Package names vary by distro. Examples:

```bash
# Arch Linux
sudo pacman -S --needed base-devel cmake ninja git vulkan-headers vulkan-icd-loader vulkan-tools glslang
# Also install one Vulkan driver package for your GPU, such as vulkan-radeon,
# vulkan-intel, amdvlk, or the NVIDIA driver stack.
# Optional for MP4 capture: sudo pacman -S --needed pkgconf ffmpeg

# Ubuntu / Debian
sudo apt install build-essential cmake ninja-build git libvulkan-dev vulkan-tools glslang-tools
# Also install the Vulkan driver package for your GPU, such as
# mesa-vulkan-drivers or the vendor driver stack.
# Optional for MP4 capture: sudo apt install pkg-config libavcodec-dev libavformat-dev libavutil-dev libswscale-dev

# Fedora
sudo dnf install gcc-c++ cmake ninja-build git vulkan-headers vulkan-loader-devel vulkan-tools glslang
# Also install the Vulkan driver package for your GPU, such as
# mesa-vulkan-drivers or the vendor driver stack.
# Optional for MP4 capture: install pkgconf-pkg-config and FFmpeg/libav
# development packages from your enabled repositories.
```

Optional but useful:

- Vulkan validation layers for local smoke runs with `--require-validation`.
- `pkg-config` plus FFmpeg/libav development packages for in-process H.264 MP4
  capture. CMake controls this with `CUBEY_VIDEO_CAPTURE=AUTO|ON|OFF`; `AUTO`
  enables it when `libavcodec`, `libavformat`, `libavutil`, and `libswscale`
  are found.

Use the CMake presets as the default entrypoint:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

GLFW, cgltf, stb, Basis Universal, and GLM fallback sources are resolved by
CMake through `FetchContent` or `find_package` where appropriate; they are not
the system packages that make Vulkan itself available.

Optional sample assets can be fetched at configure time:

```bash
cmake --preset dev -DCUBEY_FETCH_GLTF_SAMPLE_ASSETS=ON -DCUBEY_FETCH_HDR_SAMPLE_ASSETS=ON
```

Useful windowed smokes:

```bash
./build/dev/examples/spinning_cube/spinning_cube --frames 300 --width 1280 --height 720
./build/dev/examples/textured_cube/textured_cube --frames 300 --width 1280 --height 720
./build/dev/examples/shadow_cube/shadow_cube --frames 300 --width 1280 --height 720
./build/dev/examples/instanced_cubes/instanced_cubes --frames 300 --width 1280 --height 720
./build/dev/examples/material_cubes/material_cubes --frames 300 --width 1280 --height 720
./build/dev/examples/material_cubes/material_cubes --debug-view normal --frames 300 --width 1280 --height 720
./build/dev/examples/particle_cubes/particle_cubes --frames 300 --width 1280 --height 720
./build/dev/projects/fractal_2d/fractal_2d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid_2d/fluid_2d --frames 300 --width 1280 --height 720
./build/dev/projects/gltf_viewer/gltf_viewer --input path/to/model.glb --environment path/to/env.hdr --animation-index 0 --animation-speed 1.0 --frames 300 --width 1280 --height 720
./build/dev/projects/gltf_viewer/gltf_viewer --input path/to/model.glb --debug-view roughness --frames 300 --width 1280 --height 720
./build/dev/projects/pbr_furnace/pbr_furnace --frames 300 --width 1280 --height 720
```

Useful headless PNG smokes:

```bash
./build/dev/examples/headless_cube/headless_cube --width 640 --height 360 --output /tmp/cubey-headless-cube.png
./build/dev/projects/fractal_2d/fractal_2d --headless --width 640 --height 360 --output /tmp/cubey-fractal-2d.png
./build/dev/projects/fluid_2d/fluid_2d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-2d.png
./build/dev/projects/pbr_furnace/pbr_furnace --headless --width 640 --height 360 --output /tmp/cubey-pbr-furnace.png
```

Useful headless video captures when FFmpeg/libav support is enabled:

```bash
./build/dev/examples/headless_cube/headless_cube --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-headless-cube.mp4
./build/dev/projects/gltf_viewer/gltf_viewer --headless --capture video --frames 180 --fps 60 --input path/to/model.glb --environment path/to/env.hdr --output /tmp/cubey-gltf-viewer.mp4
./build/dev/projects/fluid_2d/fluid_2d --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-fluid-2d.mp4
```

Use `--require-validation` on local smoke commands when Vulkan validation
layers are installed.

## Controls

- `textured_cube`: left-drag orbits the camera, Space pauses/resumes
  auto-orbit, `R` resets the camera, Escape closes.
- `shadow_cube`: left-drag orbits the camera, `R` resets the camera, Escape
  closes.
- `instanced_cubes`: left-drag orbits the camera, `R` resets the camera, Escape
  closes.
- `material_cubes`: left-drag orbits the camera, `R` resets the camera, `D`
  cycles PBR debug views, Escape closes.
- `particle_cubes`: left-drag orbits the camera, Space pauses/resumes compute
  updates, `R` resets the camera and cube field, Escape closes.
- `fractal_2d`: left-drag pans, mouse wheel zooms around the cursor, `R` resets,
  Escape closes.
- `fluid_2d`: left-drag injects dye/force, Space pauses/resumes, `R` resets,
  `D` cycles dye/velocity/divergence/pressure views, Escape closes.
- `gltf_viewer`: left-drag orbits the camera, `D` cycles PBR debug views,
  Escape closes.
- `pbr_furnace`: left-drag orbits the camera, Escape closes.

`--debug-view` currently accepts `final`, `base-color`, `normal`,
`geometric-normal`, `roughness`, `metallic`, `occlusion`, `emissive`, `shadow`,
`alpha`, and `uv0` on the shared forward-PBR path.

## License

Cubey is licensed under the [MIT License](LICENSE).

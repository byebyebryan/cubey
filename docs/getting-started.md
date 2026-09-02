# Build and run Cubey

Cubey targets native desktop Vulkan with a C++20 toolchain. Most dependencies
can be resolved by CMake, but the system still needs a compiler, Vulkan
development files, a working GPU driver, and a GLSL compiler.

## Prerequisites

Required:

- a C++20 compiler and standard build tools;
- CMake, Ninja, and Git;
- Vulkan headers and loader;
- a Vulkan-capable GPU driver / ICD; and
- `glslangValidator` for build-time GLSL-to-SPIR-V compilation.

Package names vary by distribution. For example:

```bash
# Arch Linux
sudo pacman -S --needed base-devel cmake ninja git vulkan-headers vulkan-icd-loader vulkan-tools glslang
# Install the Vulkan driver for your GPU, such as vulkan-radeon, vulkan-intel,
# amdvlk, or the NVIDIA driver stack.
# Optional MP4 capture: sudo pacman -S --needed pkgconf ffmpeg

# Ubuntu / Debian
sudo apt install build-essential cmake ninja-build git libvulkan-dev vulkan-tools glslang-tools
# Install a compatible Vulkan driver, such as mesa-vulkan-drivers or the
# vendor driver stack.
# Optional MP4 capture:
sudo apt install pkg-config libavcodec-dev libavformat-dev libavutil-dev libswscale-dev

# Fedora
sudo dnf install gcc-c++ cmake ninja-build git vulkan-headers vulkan-loader-devel vulkan-tools glslang
# Install a compatible Vulkan driver. Optional MP4 capture also needs
# pkgconf-pkg-config and FFmpeg/libav development packages from an enabled
# repository.
```

Vulkan validation layers are optional but recommended for local development.
Use `--require-validation` when a run must fail rather than continue without
them.

## Configure, build, and test

The checked-in CMake presets are the normal entry point:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`dev` includes headless tests but excludes tests that open a window. Headless
PNG and video tests deliberately run without desktop-session environment
variables, so ordinary local and SSH validation does not depend on X11,
Wayland, or SDDM. A usable Vulkan device is still required.

Run the GLFW/swapchain tests only when opening windows is intentional:

```bash
CUBEY_ALLOW_WINDOWED_TESTS=1 ctest --preset dev-windowed
```

FFmpeg/libav support is optional. Configure with
`-DCUBEY_VIDEO_CAPTURE=AUTO` to enable in-process H.264 capture when
`libavcodec`, `libavformat`, `libavutil`, and `libswscale` are available; use
`ON` to require it or `OFF` to disable it.

Optional glTF and HDR sample assets can be fetched at configure time:

```bash
cmake --preset dev \
  -DCUBEY_FETCH_GLTF_SAMPLE_ASSETS=ON \
  -DCUBEY_FETCH_HDR_SAMPLE_ASSETS=ON
```

## Run a project

Applications open an interactive window unless `--headless` is supplied:

```bash
./build/dev/projects/ocean/ocean
./build/dev/projects/fluid/water_3d/water_3d
./build/dev/projects/fluid/fire_3d/fire_3d
./build/dev/projects/atmosphere/atmosphere
./build/dev/projects/planet/planet
./build/dev/projects/gltf_viewer/gltf_viewer --input path/to/model.glb
```

Project-local READMEs document their scene-specific controls and useful
recipes. The [docs index](README.md#project-docs) links the maintained guides.

Windowed `--frames N` runs exit after the requested frame count and print a
final `windowed_perf` summary. Add `--print-frame-stats` for periodic stdout
samples while a window remains open.

## Headless capture

Every maintained visual project supports deterministic offscreen PNG output:

```bash
./build/dev/projects/ocean/ocean --headless --frames 120 \
  --width 1280 --height 720 --output /tmp/cubey-ocean.png
```

When Cubey is built with FFmpeg/libav, the same host can produce MP4 directly:

```bash
./build/dev/projects/ocean/ocean --headless --capture video \
  --frames 480 --fps 60 --width 1280 --height 720 \
  --ocean-sea-state windy --capture-video-orbit-degrees 30 \
  --output /tmp/cubey-ocean.mp4
```

The committed showcase clips, exact recipes, hashes, and poster timestamps are
recorded in the [showcase manifest](media/showcase/manifest.json).

## Configuration

Every active executable owns a typed configuration facade composed from the
common host/profile schema and its live project options. Unrelated project keys
are rejected, and generated templates are target-specific.

Configuration precedence is:

1. compiled defaults;
2. `--config FILE`;
3. named CLI flags; and
4. `--set path=value`.

Examples:

```bash
./build/dev/projects/ocean/ocean \
  --config ocean.json --set ocean.map_size=512

./build/dev/projects/atmosphere/atmosphere \
  --write-config-template atmosphere-template.json
```

Config descriptors carry stable paths, labels, groups, value ranges, enum
choices, and help text. Runtime ImGui panels remain hand-authored project
surfaces, using shared group/control helpers for consistent hierarchy and hover
help. See [Configuration V2](architecture/configuration.md) for the ownership
and serialization contract.

## Common controls

Most 3D projects share a small interaction vocabulary:

- left-drag orbits the camera;
- the mouse wheel changes camera distance;
- Space pauses or resumes simulation where applicable;
- `R` resets the camera or simulation;
- `D` cycles project debug views; and
- Escape closes the window.

Project-local guides describe exceptions and the available debug views. Cubey
uses ImGui for focused runtime controls and diagnostics; it is not an editor.

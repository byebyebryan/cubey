# cubey Vulkan spike

This branch is a narrow native-Vulkan experiment. It intentionally avoids Dawn,
WebGPU, and any Cubey abstraction layer for now.

The current spike validates:

- Vulkan instance/device/compute queue setup
- build-time GLSL to SPIR-V compilation
- storage-image compute writes
- graphics pipeline drawing indexed cube geometry
- sampled compute texture, vertex input, uniform MVP data, and depth testing
- explicit compute-to-fragment and render-to-readback synchronization
- optional Vulkan validation layer and debug messenger support
- GLFW window and Vulkan surface creation
- swapchain presentation through a render pass, including resize/out-of-date recreation
- headless offscreen render, readback, and deterministic smoke verification
- simple PPM image output for visual inspection

It is still a spike, not a reusable renderer.

## Build and run

```bash
cmake -S . -B build-vulkan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
cmake --build build-vulkan -j$(nproc)
ctest --test-dir build-vulkan --output-on-failure
./build-vulkan/cubey
./build-vulkan/cubey --headless --width 512 --height 512 --frames 8 --output build-vulkan/spike.ppm
```

Use `--frames N` with window mode for a bounded smoke run:

```bash
./build-vulkan/cubey --frames 300
./build-vulkan/cubey --require-validation --frames 300 --width 1280 --height 720
```

Validation support is enabled by default when `VK_LAYER_KHRONOS_validation` is
installed. Use `--validation` to enable it explicitly, `--no-validation` to
suppress it, or `--require-validation` to fail fast when the layer is missing.
The build option
`-DCUBEY_ENABLE_VALIDATION=OFF` changes the default.

The fetched source dependencies are `Vulkan-Headers` and GLFW. The Vulkan
loader comes from the local `vulkan` pkg-config package.

In a tty-only shell with no `DISPLAY` or `WAYLAND_DISPLAY`, the window-path
test intentionally verifies that non-headless mode reaches GLFW and reports
`glfwInit failed`.

See [`docs/vulkan-notes.md`](docs/vulkan-notes.md) for progress, gotchas, and
branch learnings.

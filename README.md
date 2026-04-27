# cubey Vulkan spike

This branch is a narrow native-Vulkan experiment. It intentionally avoids Dawn,
WebGPU, and any Cubey abstraction layer for now.

The current spike validates:

- Vulkan instance/device/compute queue setup
- build-time GLSL to SPIR-V compilation
- storage-image compute writes
- graphics pipeline sampling the compute-written image
- explicit shader-write to host-read synchronization
- GLFW window and Vulkan surface creation
- swapchain presentation through a render pass
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
```

The fetched source dependencies are `Vulkan-Headers` and GLFW. The Vulkan
loader comes from the local `vulkan` pkg-config package.

In a tty-only shell with no `DISPLAY` or `WAYLAND_DISPLAY`, the window-path
test intentionally verifies that non-headless mode reaches GLFW and reports
`glfwInit failed`.

See [`docs/vulkan-notes.md`](docs/vulkan-notes.md) for progress, gotchas, and
branch learnings.

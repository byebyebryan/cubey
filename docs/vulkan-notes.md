# Vulkan Spike Notes

Progress and learnings from the native Vulkan spike on the `vulkan` branch.

---

## Current state

The spike is intentionally raw Vulkan rather than a reusable Cubey framework.
It validates the visible-surface path first, with a headless path kept for
automation:

- GLFW window creation and Vulkan surface creation
- Vulkan instance, physical-device selection, logical device, and queue setup
- build-time GLSL to SPIR-V compilation with `glslangValidator`
- compute pipeline writing a procedural image into a storage buffer
- host-visible readback for headless smoke verification
- swapchain acquisition and presentation
- copy from compute-written storage buffer into the acquired swapchain image

Desktop smoke status: the visible GLFW/Vulkan surface path was confirmed by the
user from a graphical session after the swapchain path was added. The Codex tty
session can only verify the expected no-display failure and the headless path.

This is not yet a render-pass demo. The visible path is deliberately simple:
compute writes pixels, transfer copies pixels into the swapchain image, present.

---

## Build setup

### Local Vulkan loader, fetched headers

The machine has the Vulkan loader and pkg-config metadata, but not Vulkan SDK
headers under `/usr/include/vulkan`. The spike therefore uses:

```cmake
pkg_check_modules(VULKAN REQUIRED vulkan)
FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.4.341
)
```

This keeps the dependency lighter than Dawn while avoiding a machine-wide SDK
install requirement.

### GLFW is fetched

GLFW is not available through `pkg-config` on this machine, so the branch fetches
GLFW `3.4` through CMake. Configure output currently includes both Wayland and
X11 support.

### Shader compilation is build-time

The compute shader is compiled at build time:

```cmake
glslangValidator -V shaders/headless.comp -o build-vulkan/shaders/headless.comp.spv
```

The generated shader path is injected through `src/config.h.in`.

---

## Runtime modes

### Window mode

Window mode is the default:

```bash
./build-vulkan/cubey
```

Use `--frames N` for bounded smoke tests:

```bash
./build-vulkan/cubey --frames 300
```

The current path does not use a graphics pipeline. It acquires a swapchain
image, dispatches compute, transitions the swapchain image to
`TRANSFER_DST_OPTIMAL`, copies the storage buffer into it, transitions to
`PRESENT_SRC_KHR`, and presents.

### Headless mode

Headless mode uses the same compute shader and storage buffer, then reads back
the buffer and writes a PPM:

```bash
./build-vulkan/cubey --headless --width 512 --height 512 --frames 8 --output build-vulkan/spike.ppm
```

The smoke verification checks that the output varies across the image and that
every pixel has alpha 255.

---

## Gotchas and learnings

### Visible smoke testing depends on the caller's session

From the Codex tty session, there is no `DISPLAY` or `WAYLAND_DISPLAY`, so a real
window cannot be opened. The CTest window smoke accepts either:

- `glfwInit failed` in tty/no-display environments
- `window mode:` in graphical sessions where the visible path starts

This keeps one test command useful in both environments.

### One queue family is enough for this spike

The spike requires a single queue family that supports compute and present. That
worked on the target machine. A more general renderer may need separate compute,
graphics, transfer, and present queues, but that complexity is intentionally out
of scope for this spike.

### Transfer-to-swapchain is a useful first visible path

Copying a compute-written buffer into the swapchain image avoids render-pass and
graphics-pipeline setup while still proving surface creation, swapchain image
ownership/layout transitions, and present. It is not the final rendering model,
but it is a good bridge from headless compute to visible output.

### Swapchain image usage matters

The visible path requires:

```cpp
VK_IMAGE_USAGE_TRANSFER_DST_BIT
```

in `VkSwapchainCreateInfoKHR::imageUsage`, and it checks
`VkSurfaceCapabilitiesKHR::supportedUsageFlags` before creating the swapchain.

### Channel order needs format awareness

Most Linux swapchains commonly expose BGRA formats. The shader takes a small
push-constant flag so the same procedural compute path can write either RGBA or
BGRA byte order before the buffer-to-image copy.

### Host-visible storage is convenient, not final

For this spike, the storage buffer is host-visible and coherent. That keeps
readback simple and avoids staging-buffer complexity. A performance-oriented
path should move compute output to device-local memory and use staging buffers
only where readback is required.

### Synchronization is explicit but still manageable

The branch currently has two explicit synchronization paths:

- compute shader write to host read for headless mode
- compute shader write to transfer read, then transfer write to present for
  window mode

This is heavier than WebGPU, but the exact work being synchronized is visible in
the code.

---

## Review notes before growing this branch

- The main source file is intentionally too large for a real architecture. If
  this branch becomes the project direction, split it into lifecycle, device,
  swapchain, compute pipeline, and demo files before adding another demo.
- Add validation-layer support before debugging harder rendering issues.
- Add resize/out-of-date swapchain handling before treating window mode as more
  than a smoke test.
- Replace per-frame semaphore/fence allocation with reusable per-frame state
  before measuring performance.
- Add a graphics-pipeline pass next if the goal is to compare Vulkan's real demo
  ergonomics against the WebGPU spike.

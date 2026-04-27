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
- compute pipeline writing a procedural image into a storage image
- graphics pipeline sampling the compute-written image
- render pass targeting either a swapchain image or an offscreen color image
- transfer-buffer readback for headless smoke verification
- swapchain acquisition and presentation

Desktop smoke status: the visible GLFW/Vulkan surface path was confirmed from a
graphical session after the swapchain path was added. After swapchain recreation
handling was added, a bounded desktop run printed one `swapchain out of date;
recreating` line and then completed normally. The Codex tty session can only
verify the expected no-display failure and the headless path.

The current visible path now exercises both compute and graphics. Compute writes
an RGBA storage image, the render pass samples it through a fullscreen triangle,
and the swapchain image is presented from the render pass final layout.

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

All shaders are compiled at build time:

```cmake
glslangValidator -V shaders/headless.comp -o build-vulkan/shaders/headless.comp.spv
glslangValidator -V shaders/fullscreen.vert -o build-vulkan/shaders/fullscreen.vert.spv
glslangValidator -V shaders/sample.frag -o build-vulkan/shaders/sample.frag.spv
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

The current path acquires a swapchain image, dispatches compute into a storage
image, transitions that image to `SHADER_READ_ONLY_OPTIMAL`, samples it in a
fullscreen graphics pass, and presents the swapchain image from
`PRESENT_SRC_KHR`.

### Headless mode

Headless mode uses the same compute shader and graphics pipeline, renders into
an offscreen color image, copies that image to a host-visible readback buffer,
and writes a PPM:

```bash
./build-vulkan/cubey --headless --width 512 --height 512 --frames 8 --output build-vulkan/spike.ppm
```

The smoke verification checks that the rendered output varies across the image
and that every pixel has alpha 255.

---

## Gotchas and learnings

### Visible smoke testing depends on the caller's session

From the Codex tty session, there is no `DISPLAY` or `WAYLAND_DISPLAY`, so a real
window cannot be opened. The CTest window smoke accepts either:

- `glfwInit failed` in tty/no-display environments
- `window mode:` in graphical sessions where the visible path starts

This keeps one test command useful in both environments.

### Present can invalidate the swapchain immediately

On the RTX 5070 Ti desktop smoke, the first graphics-pipeline window run reached
the present path and then returned `VK_ERROR_OUT_OF_DATE_KHR` from
`vkQueuePresentKHR` (`VkResult -1000001004`). The logged swapchain extent was
`1280x1432`, which is a useful reminder that the surface extent is owned by the
window system, not the requested window size.

Window mode now treats `VK_ERROR_OUT_OF_DATE_KHR` and `VK_SUBOPTIMAL_KHR` from
acquire/present as recoverable. It waits for the device, destroys and recreates
the swapchain, image views, framebuffers, render pass, source image, graphics
pipeline, and descriptor sets, then retries the frame. To avoid an invisible
hang during smoke tests, window mode aborts if the swapchain remains out of date
after eight consecutive recreation attempts.

Follow-up desktop smoke confirmed the expected behavior: one recreation message
was printed, and the run finished without issue.

### One queue family is enough for this spike

The spike requires a single queue family that supports graphics, compute, and
present. That worked on the target machine. A more general renderer may need
separate compute, graphics, transfer, and present queues, but that complexity is
intentionally out of scope for this spike.

### Transfer-to-swapchain was useful, then replaced

The first visible path copied a compute-written buffer directly into the
swapchain image. That was a useful bridge from headless compute to presentation,
but it did not exercise a graphics pipeline. The branch now renders through a
real render pass and fullscreen triangle.

### Swapchain image usage changed with the render pass

The buffer-copy path required:

```cpp
VK_IMAGE_USAGE_TRANSFER_DST_BIT
```

The graphics path now requires:

```cpp
VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
```

in `VkSwapchainCreateInfoKHR::imageUsage`, and it checks
`VkSurfaceCapabilitiesKHR::supportedUsageFlags` before creating the swapchain.

### Sampling avoids swapchain channel-order hacks

The buffer-copy path needed explicit RGBA/BGRA byte-order handling before
copying into the swapchain. The render-pass path samples a normal RGBA texture
and lets the fragment output path write to the actual swapchain format.

### Device-local images are now the main path

The compute source image and render target are device-local. Headless readback
uses a separate host-visible staging buffer after rendering. This is closer to
the shape a real demo will need than the initial host-visible storage-buffer
path.

### Format feature checks are explicit

The spike checks that the compute source format supports both storage-image and
sampled-image usage, and that the headless offscreen format supports color
attachment plus transfer source usage. This keeps unsupported device/format
combinations from failing later in a less obvious pipeline or copy call.

### Synchronization is explicit but still manageable

The branch currently has two explicit synchronization paths:

- compute shader write to fragment shader read
- color attachment output to transfer read for headless readback

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
- Replace per-frame semaphore/fence allocation and the current conservative
  `vkQueueWaitIdle` present cleanup with reusable per-frame state before
  measuring performance.
- Add vertex/index/uniform/depth state next if the goal is to match the WebGPU
  spinning-cube demo directly.

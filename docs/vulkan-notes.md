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
- graphics pipeline drawing indexed cube geometry
- vertex/index buffers, a uniform MVP buffer, and a depth attachment
- fragment shader sampling the compute-written image on the cube
- optional `VK_LAYER_KHRONOS_validation` and debug-utils messenger support
- render pass targeting either a swapchain image or an offscreen color image
- transfer-buffer readback for headless smoke verification
- swapchain acquisition, presentation, and resize/out-of-date recreation

Desktop smoke status: the visible GLFW/Vulkan surface path has now passed under
`--require-validation` in a niri session on the RTX 5070 Ti. The compositor
forced the framebuffer extent to `1280x1432` for a requested `1280x720` window,
which confirms again that the swapchain must follow the surface extent rather
than the requested window size. The bounded run reported one present-driven
recreate and two explicit resize recreates, then continued normally:

```text
window mode: NVIDIA GeForce RTX 5070 Ti rendering textured cube through graphics pipeline at 1280x1432
swapchain out of date; recreating
framebuffer resized; recreating swapchain
framebuffer resized; recreating swapchain
```

The Codex tty session can still only verify the expected no-display failure and
the headless path, but the user-run desktop smoke now covers the visible
validation, present, and resize paths.

The current visible path now exercises both compute and a basic 3D graphics
workload. Compute writes an RGBA storage image, the render pass draws indexed
cube geometry with an MVP uniform and depth testing, and the fragment shader
samples the compute-written image on the cube before presenting the swapchain
image from the render pass final layout.

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
glslangValidator -V shaders/cube.vert -o build-vulkan/shaders/cube.vert.spv
glslangValidator -V shaders/sample.frag -o build-vulkan/shaders/sample.frag.spv
```

The generated shader path is injected through `src/config.h.in`.

### Validation support is opt-out, but layer-dependent

`CUBEY_ENABLE_VALIDATION` is ON by default at configure time. At runtime, the
spike probes for `VK_LAYER_KHRONOS_validation`:

- if available, it enables the layer and routes warning/error messages through a
  `VK_EXT_debug_utils` messenger when that extension is available
- if unavailable, it logs a short message and continues
- `--validation` enables the probe explicitly
- `--no-validation` disables the probe
- `--require-validation` makes the missing layer a hard failure

On this machine, `pacman -Q vulkan-validation-layers` currently reports
`vulkan-validation-layers 1.4.341.0-2`, and the headless smoke passes with
`--require-validation`.

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

Use `--require-validation` for a stronger desktop smoke when validation layers
are installed:

```bash
./build-vulkan/cubey --require-validation --frames 300 --width 1280 --height 720
```

The current path acquires a swapchain image, dispatches compute into a storage
image, transitions that image to `SHADER_READ_ONLY_OPTIMAL`, binds cube
vertex/index buffers plus a per-frame MVP uniform buffer, draws with depth
testing, and presents the swapchain image from `PRESENT_SRC_KHR`.

GLFW framebuffer-size callbacks mark the swapchain dirty. The render loop
recreates window resources on explicit resize, and still handles
`VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` from acquire or present.

### Headless mode

Headless mode uses the same compute shader and textured-cube graphics pipeline,
renders into an offscreen color image with a depth attachment, copies the color
image to a host-visible readback buffer, and writes a PPM:

```bash
./build-vulkan/cubey --headless --width 512 --height 512 --frames 8 --output build-vulkan/spike.ppm
```

The smoke verification checks that the rendered output varies across the image
and that every pixel has alpha 255.

### Textured-cube slice

The first mesh slice keeps resource ownership intentionally simple:

- cube vertex and index buffers are host-visible/coherent
- one host-visible uniform buffer stores the current MVP matrix
- the compute-written storage image is still the sampled texture
- a depth attachment is created for both swapchain and headless framebuffers
- the graphics pass now uses vertex input plus `vkCmdDrawIndexed`

This gives the Vulkan branch a closer comparison point to the WebGPU spinning
cube experiment without introducing staging uploads, frame overlap, or reusable
renderer abstractions yet.

Review checkpoint: the follow-up review tightened the depth setup rather than
changing the slice shape. Depth/stencil fallback formats now create an image
view with the stencil aspect included, and the render-pass dependencies include
both early and late fragment-test stages so depth writes are covered explicitly.

---

## Gotchas and learnings

### Visible smoke testing depends on the caller's session

From the Codex tty session, there is no `DISPLAY` or `WAYLAND_DISPLAY`, so a real
window cannot be opened. The CTest window smoke accepts either:

- `glfwInit failed` in tty/no-display environments
- `window mode:` in graphical sessions where the visible path starts

This keeps one test command useful in both environments.

### Present and compositor resize can invalidate the swapchain

On the RTX 5070 Ti desktop smoke, the first graphics-pipeline window run reached
the present path and then returned `VK_ERROR_OUT_OF_DATE_KHR` from
`vkQueuePresentKHR` (`VkResult -1000001004`). The logged swapchain extent was
`1280x1432`, which is a useful reminder that the surface extent is owned by the
window system, not the requested window size.

Window mode now treats `VK_ERROR_OUT_OF_DATE_KHR` and `VK_SUBOPTIMAL_KHR` from
acquire/present as recoverable. It waits for the device, destroys and recreates
the swapchain, image views, framebuffers, render pass, source image, depth
attachment, graphics pipeline, and descriptor sets, then retries the frame. To
avoid an invisible hang during smoke tests, window mode aborts if the swapchain
remains out of date after eight consecutive recreation attempts.

Follow-up desktop smoke confirmed the expected behavior: one recreation message
was printed, and the run finished without issue.

After validation support and framebuffer resize handling were added, a
`--require-validation` desktop run under niri exercised both recovery paths in
one bounded run. The compositor forced the surface extent to `1280x1432`, the
first present caused `swapchain out of date; recreating`, and manual resizing
printed `framebuffer resized; recreating swapchain` twice. That makes the
current resize handling good enough for this spike.

The resize path now also waits for a nonzero GLFW framebuffer size before
creating a swapchain, so minimized or temporarily zero-sized windows do not feed
an invalid extent into swapchain creation.

### One queue family is enough for this spike

The spike requires a single queue family that supports graphics, compute, and
present. That worked on the target machine. A more general renderer may need
separate compute, graphics, transfer, and present queues, but that complexity is
intentionally out of scope for this spike.

### Transfer-to-swapchain was useful, then replaced

The first visible path copied a compute-written buffer directly into the
swapchain image. That was a useful bridge from headless compute to presentation,
but it did not exercise a graphics pipeline. The branch now renders through a
real render pass. The graphics pass started as a fullscreen triangle and now
draws indexed cube geometry.

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

The branch currently has a few explicit ordering points:

- compute shader write to fragment shader read
- color attachment output to transfer read for headless readback
- per-frame host uniform update before submitting the command buffer
- render-pass dependency coverage for color and early/late depth writes

This is heavier than WebGPU, but the exact work being synchronized is visible in
the code.

---

## Review notes before growing this branch

- Replace per-frame semaphore/fence allocation and the current conservative
  `vkQueueWaitIdle` present cleanup with reusable per-frame state before
  measuring performance.
- Split the single source file once the spike needs another demo or persistent
  renderer surface.

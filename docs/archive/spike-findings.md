# WebGPU and Vulkan Spike Findings

Decision record after the `webgpu` and `vulkan` branches.

## Recommendation

Use **native Vulkan as the primary Cubey 2.0 graphics path**.

Keep WebGPU/Dawn as:

- a reference implementation for API shape and browser constraints
- a future optional export path for presentable demos
- a source of portability lessons, not a current backend contract

Do not build a Filament-style dual backend abstraction now. Dawn is already an
abstraction layer, and adding another broad layer on top would add project
complexity while still inheriting WebGPU's constraints. The next architecture
step should be smaller Vulkan-native seams: device, surface/swapchain,
resources, pipelines, frame state, and demo/pass code.

## What the WebGPU Spike Proved

The `webgpu` branch successfully exercised:

- Dawn native surface creation through GLFW
- Emscripten browser builds through `emdawnwebgpu`
- async adapter/device request flow
- hello triangle
- spinning cube with vertex/index/uniform/depth state
- compute-driven vertex deformation
- compute-written texture sampled on cube faces

The API was productive. WebGPU gave clear resource binding, implicit ordering
between compute and render passes inside a command encoder, and a real native to
web path.

## WebGPU Costs and Gotchas

The spike also exposed costs that matter for Cubey:

- Dawn is a heavy dependency; first configure/build is significant.
- Dawn required Clang on the tested Linux environment because GCC 15 rejected
  Dawn's current standard-library interaction.
- Dawn FetchContent needs careful dependency/submodule settings.
- Wayland/X11 support needs explicit Dawn and `glfw3webgpu` configuration.
- Emscripten needs `--use-port=emdawnwebgpu` on both compile and link, plus
  Asyncify for the WaitAny adapter/device pattern.
- Browser behavior is not just "runs anywhere": Chrome on Linux may blocklist
  new GPUs, requiring `--ignore-gpu-blocklist` / `--enable-unsafe-webgpu`;
  Firefox worked better on the tested setup.
- The browser uses the local GPU, so a web build does not solve remote-dev GPU
  access.
- WebGPU's safety and portability constraints become Cubey's ceiling if it is
  the primary path.

The conclusion is not that WebGPU is bad. It is a good optional presentation
path. It is less aligned with a native graphics workbench where explicit GPU
control and project feel are part of the point.

## What the Vulkan Spike Proved

The `vulkan` branch successfully exercised:

- Vulkan instance/device/queue setup
- GLFW windowed surface creation
- swapchain acquisition, presentation, out-of-date handling, and resize
  recreation
- compute shader writing a storage image
- graphics pipeline drawing indexed cube geometry
- sampled compute texture on cube faces
- vertex/index buffers, uniform MVP data, and depth attachment
- headless offscreen render, readback, PPM output, and deterministic smoke
  verification
- validation-layer probing plus `VK_EXT_debug_utils` messenger support

The key desktop smoke passed under `--require-validation` in niri on the RTX
5070 Ti. niri forced a `1280x1432` framebuffer extent for a requested
`1280x720` window, which confirmed the app must follow the compositor-owned
surface extent. The run exercised both recovery paths:

```text
window mode: NVIDIA GeForce RTX 5070 Ti rendering textured cube through graphics pipeline at 1280x1432
swapchain out of date; recreating
framebuffer resized; recreating swapchain
framebuffer resized; recreating swapchain
```

## Vulkan Costs and Gotchas

Vulkan's costs are real but useful:

- more boilerplate for instance/device/surface/swapchain setup
- explicit image layout transitions and synchronization
- explicit render pass/framebuffer/depth resources
- swapchain invalidation and compositor resize handling
- validation layer setup and debug messenger plumbing
- need to build reusable frame resources before measuring performance

These costs are closer to the native Vulkan complexity Cubey should own. The
spike made that complexity concrete and manageable rather than hiding it behind a
larger dependency.

## Decision

Proceed with Vulkan first.

Near-term direction:

1. Split the current Vulkan spike into small modules rather than one giant
   source file.
2. Add reusable frame state: semaphores, fences, command buffers, and
   N-frames-in-flight.
3. Add staging uploads for mesh/uniform/texture data where appropriate.
4. Preserve headless rendering and validation-layer smoke tests as first-class
   workflows.
5. Keep WebGPU branch notes as reference material, but do not design the main
   API around WebGPU compatibility yet.

Revisit WebGPU when there is a concrete need for browser demos or public
interactive showcases.

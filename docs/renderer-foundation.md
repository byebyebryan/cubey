# Renderer Foundation

Cubey's renderer foundation lives above `cubey::vulkan` as `cubey::render`.
This layer gives examples and projects shared rendering vocabulary while keeping
Vulkan ownership, synchronization, layouts, descriptors, and command recording
visible where they affect correctness.

## Direction

The first renderer slice creates small contracts for concepts that are already
stable across the repo:

- render target views for swapchain and offscreen color targets, with optional
  depth targets;
- a frame-facing target contract that hosts can pass to examples and projects;
- texture and mesh resource wrappers that own Vulkan resources without hiding
  layout transitions or descriptor updates;
- draw packet metadata for simple indexed draws.

`cubey::render` may depend on `cubey::vulkan`. `cubey::vulkan` must not depend
on `cubey::render`.

## Boundaries

`cubey::render` owns renderer-facing vocabulary:

- target views: color, depth, extent, format, and image/view handles;
- resource wrappers: texture and mesh ownership around existing Vulkan buffers,
  images, and samplers;
- small command helpers for well-defined draw operations.

Examples and projects still own render intent:

- shader code and shader stage selection;
- descriptor set layouts, descriptor writes, and binding order;
- pipeline layout and graphics/compute pipeline setup;
- image layout transitions, barriers, pass ordering, and command-buffer scope;
- camera, material, simulation, and interaction policy.

The app layer owns host flow: GLFW, input, timing, swapchain lifecycle, and
headless output. It may pass `cubey::render` target views to callbacks, but it
should not become a renderer.

## Non-Goals For This Slice

- Scene graph, entity IDs, components, or renderable ownership.
- Material system, shader reflection, shader hot reload, or pipeline cache.
- Render graph/frame graph/pass scheduler.
- VMA or another memory allocator.
- Dedicated render thread, split graphics/compute/present queues, or parallel
  command recording.
- Backend-agnostic API vocabulary.

## Precedent

Cubey should use established names and constraints without copying another
engine's scope:

- Filament separates engine, renderer, swap chain, view, scene, camera,
  renderables, geometry, and material instances.
- Godot exposes rendering resources and instances through rendering-server
  vocabulary while keeping 2D canvas and 3D scenario concepts distinct.
- Unity keeps common scene contracts centered on transform, mesh renderer,
  material, and camera.
- Vulkan remains the source of truth for synchronization, command buffers,
  descriptors, image layouts, and queue ownership.

The first `cubey::render` layer should borrow this vocabulary discipline, not a
full engine architecture.

## Current Checkpoint

- `ColorTargetView`, `DepthTargetView`, and `RenderTargetView` describe
  swapchain and offscreen targets without owning layout transitions.
- `RenderTargetRenderingInfo` builds dynamic rendering attachment state from a
  target view and clear values while keeping command-buffer scope explicit.
- `WindowedHost` passes `WindowedRenderFrame` to render callbacks, including the
  command buffer, swapchain image index, timing, and color target view.
- `HeadlessRenderTarget` is now the same target-view vocabulary as
  `cubey::render::ColorTargetView`.
- `Texture2D` owns a Vulkan image plus an optional sampler for the current
  storage-sampled and transfer-sampled texture paths.
- `Mesh`, `DrawItem`, and `record_draw_item` own uploaded indexed geometry and
  record the minimal bind/draw sequence. Vertex layout, pipeline state,
  descriptors, materials, transforms, and push constants remain caller-owned.

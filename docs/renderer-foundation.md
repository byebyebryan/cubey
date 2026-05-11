# Renderer Foundation

Cubey's renderer foundation lives above `cubey::vulkan` as `cubey::render`.
This layer gives examples and projects shared rendering vocabulary while keeping
Vulkan ownership, synchronization, layouts, descriptors, and command recording
visible where they affect correctness.

## Direction

The first renderer slice creates small contracts for concepts that are already
stable across the repo:

- render target views for swapchain and offscreen color targets, with optional
  depth targets and sampled depth targets;
- a frame-facing target contract that hosts can pass to examples and projects;
- texture and mesh resource wrappers that own Vulkan resources without hiding
  layout transitions or descriptor updates;
- explicit frame slots and per-frame uniform buffers for CPU-updated render
  data;
- draw packet metadata for simple indexed draws;
- opaque render resource handles that scene snapshots can carry, plus a small
  CPU-side registry for handle identity/liveness that does not own Vulkan
  resources.

`cubey::render` may depend on `cubey::vulkan`. `cubey::vulkan` must not depend
on `cubey::render`.

## Boundaries

`cubey::render` owns renderer-facing vocabulary:

- target views: color, depth, extent, format, and image/view handles;
- resource wrappers: texture and mesh ownership around existing Vulkan buffers,
  images, and samplers;
- frame data: frame-slot identity and host-visible per-frame uniform buffers;
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

- Full scene graph, node update system, or renderer-owned scene traversal.
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
- `DepthOnlyRenderingInfo` builds the depth-only dynamic-rendering state needed
  by shadow-map and prepass-style work. It deliberately stores depth so the
  caller can transition and sample it later.
- `WindowedHost` passes `WindowedRenderFrame` to render callbacks, including the
  command buffer, swapchain image index, timing, and color target view.
- `HeadlessRenderTarget` is now the same target-view vocabulary as
  `cubey::render::ColorTargetView`.
- `Texture2D` owns a Vulkan image plus an optional sampler for the current
  storage-sampled and transfer-sampled texture paths.
- `DepthTexture` owns a sampled depth image plus an optional sampler for shadow
  maps and other depth-as-texture paths. Layout transitions and descriptor
  image layouts stay explicit at the call site.
- `Mesh`, `DrawItem`, and `record_draw_item` own uploaded indexed geometry and
  record the minimal bind/draw sequence. Vertex layout, pipeline state,
  descriptors, materials, transforms, and push constants remain caller-owned.
- `MeshHandle` and `MaterialHandle` are opaque CPU-side values used by scene
  renderables and renderable packets. `RenderResourceRegistry` issues and
  destroys those handles, validates liveness, and stores CPU metadata:
  mesh labels plus material domain, blend mode, label, and sort key. It does
  not own `Mesh`, textures, descriptors, pipelines, or shader/material binding
  data.
- `ResourceTable` maps typed render handles to app-owned resources such as
  `Mesh`, so examples can resolve registry-issued handles without hardcoded
  one-off checks.
- `RenderDrawPacket3D` and `build_render_draw_packets_3d` validate live
  mesh/material handles, attach material metadata, preserve world bounds, and
  sort draw packets deterministically.
- `View3D`, `Environment3D`, and `RenderFramePlan3D` form the current CPU
  view-planning boundary. They combine a scene read view, camera entity,
  viewport size, ambient-only environment, draw packets, light packets, and
  conservative CPU frustum culling. Vulkan command recording, pipeline
  selection, descriptor binding, and pass ordering remain caller-owned.
- `RenderPassPlan3D` and `FrameRenderPlan3D` provide a small CPU-side pass list
  for multi-view and multi-pass examples. They preserve explicit pass order and
  pass kind without becoming a render graph or scheduler.
- `RenderableManager3D` lives in the scene/component layer and emits compact
  renderable packets with world matrices, world bounds, and resource handles.
  The cube
  examples now use Engine-owned scenes, registry-issued handles, mesh resource
  tables, and CPU draw planning while still owning pipelines, descriptors, and
  Vulkan command recording locally.
- `LightManager3D` lives in the scene/component layer and emits compact
  CPU-side light packets for directional and point lights. The packets provide
  light kind, color, intensity, direction or world position, and range; shader
  interpretation, light limits, descriptor layout, and GPU upload policy remain
  outside `cubey::render` for now.
- `FrameSlot` gives render callbacks a stable frame-data index, and
  `FrameUniformBuffer<T>` owns one host-visible uniform buffer per frame slot.
  The current windowed host uses real frame slots for overlapping frame
  resources, while swapchain image ownership stays Vulkan-visible.

## Multipass Direction

`examples/shadow_cube` is the current reference for manual multipass rendering:
build a shadow `View3D`, build a camera `View3D`, record a depth-only pass into
a `DepthTexture`, transition that image for shader reads, then record the color
pass that samples it. The reusable foundation intentionally stops at view/pass
planning, target/texture ownership, layout-transition helpers, and depth-only
rendering info. Render-graph scheduling, automatic resource barriers, material
binding, and shadow policy remain future work.

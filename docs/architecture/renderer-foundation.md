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
- CPU-side primitive mesh data for common world-space shapes, plus vertex
  input layouts matching current shader contracts;
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
- image layout transition choice, barriers, pass ordering, and command-buffer
  scope;
- camera, material, simulation, and interaction policy.

Examples and projects may use `cubey::vulkan::CommandRecorder` for repeated
command-buffer calls, but the render sequence and synchronization decisions
remain their responsibility.

The host layer owns host flow: GLFW, input, timing, swapchain lifecycle, and
headless output. It may pass `cubey::render` target views to callbacks, but it
should not become a renderer.

## Non-Goals For This Slice

- Full scene graph, node update system, or renderer-owned scene traversal.
- Material system, shader reflection, shader hot reload, or pipeline cache.
- Render graph/frame graph/pass scheduler. The future graph vocabulary is
  mapped separately in [render graph direction](render-graph.md).
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
- `cubey::render::pass.h` exposes `record_render_target_pass`,
  `record_depth_only_pass`, and `record_present_render_target_pass` as the
  current command-recording helpers for dynamic-rendering scopes, clear values,
  fullscreen triangle draws, and presentable swapchain color/depth layout
  sequencing. They keep pass contents caller-owned and do not replace render
  graph resource/barrier ownership.
- `WindowedHost` passes `WindowedRenderFrame` to render callbacks, including the
  command buffer, swapchain image index, timing, and color target view.
- `cubey::host::HeadlessRenderTarget` is now the same target-view vocabulary as
  `cubey::render::ColorTargetView`.
- `Texture2D` owns a Vulkan image plus an optional sampler for the current
  storage-sampled and transfer-sampled texture paths.
- `DepthTexture` owns a sampled depth image plus an optional sampler for shadow
  maps and other depth-as-texture paths. Layout transitions and descriptor
  image layouts stay explicit at the call site.
- `Mesh`, `DrawItem`, and `record_draw_item` own uploaded indexed geometry and
  record the minimal bind/draw sequence. Vertex layout, pipeline state,
  descriptors, materials, transforms, and push constants remain caller-owned.
- `PrimitiveMeshData`, `VertexPositionColor`,
  `VertexPositionColorNormal`, and `VertexPositionColorNormalUv` provide
  CPU-side cube and XZ-plane mesh data for the existing `Mesh` upload path.
  The primitive layer also exposes reusable vertex input layouts for those
  vertex contracts. It does not create renderables, materials, descriptors,
  pipelines, or scene entities.
- `MeshHandle` and `MaterialHandle` are opaque CPU-side values used by scene
  renderables and renderable packets. `RenderResourceRegistry` issues and
  destroys those handles, validates liveness, and stores CPU metadata:
  mesh labels plus material domain, blend mode, label, sort key, and pass
  participation mask. It does not own `Mesh`, textures, descriptors, pipelines,
  or shader/material binding data.
- `ResourceTable` maps typed render handles to app-owned resources such as
  `Mesh`, so examples can resolve registry-issued handles without hardcoded
  one-off checks.
- `cubey::scene::RenderDrawPacket3D` and
  `cubey::scene::build_render_draw_packets_3d` validate live mesh/material
  handles, attach material metadata, preserve world bounds, and sort draw
  packets deterministically.
- `cubey::render::RenderItem` is the renderer-facing draw intent shared by
  scene draw packets and examples. It carries mesh/material handles plus draw
  range fields, and `resolve_draw_item` converts it to the lower-level
  `DrawItem` once the caller resolves app-owned mesh resources.
- `cubey::scene::record_draw_packets_3d` is the current scene-side recording
  helper for the repeated packet-filter, per-packet state, resolve, and draw
  sequence used by 3D examples. It does not choose pipelines, descriptor sets,
  pass order, or push-constant contents.
- `cubey::render::MaterialPassInfo` is the first explicit material/pass
  metadata contract. It describes pass kind, descriptor layout shape,
  push-constant ranges, and reusable graphics pipeline state.
- `cubey::render::ShaderProgram`, `GraphicsPipelineResource`, and
  `ComputePipelineResource` own the current shader-module, pipeline-layout, and
  graphics/compute pipeline lifetime shapes. They consume explicit shader stage
  files, descriptor set layouts, push constants, attachment formats, vertex
  input, and `MaterialPassInfo` where applicable; they do not reflect shaders,
  cache pipelines, allocate descriptors, bind materials, or record commands.
  Current examples and `fluid_2d` create app/project graphics pipelines through
  this render-level wrapper using file-backed shader stage configs; the
  lower-level Vulkan pipeline builders remain as tested implementation details
  and escape hatches.
- `cubey::vulkan::DescriptorWriteBatch` is the current descriptor-update
  boundary: callers still choose descriptor sets, bindings, resources, and
  image layouts, while the batch owns write backing storage until submission.
- `cubey::scene::View3D`, `Environment3D`, and `RenderFramePlan3D` form the
  current CPU view-planning boundary. They combine a scene read view, camera
  entity, viewport size, ambient-only environment, draw packets, light packets,
  and conservative CPU frustum culling. Vulkan command recording, pipeline
  selection, descriptor binding, and pass ordering remain caller-owned.
- `cubey::scene::RenderPassPlan3D` and `FrameRenderPlan3D` provide a small
  CPU-side pass list for multi-view and multi-pass examples. They preserve
  explicit pass order and pass kind without becoming a render graph or
  scheduler.
- `cubey::render::RenderGraphBuilder` provides the first graph declaration and
  validation layer for imported/transient texture and buffer resources,
  graphics/compute/transfer passes, and ordered pass/resource usage. It
  compiles declarations and `CompiledRenderGraph::execute()` invokes pass
  callbacks synchronously in compiled order. It now derives in-graph
  texture-transition and buffer-barrier requirements, imported acquire/release
  barriers, and transient first-use barriers. `RenderGraphResourceSet` resolves
  imported resources and can allocate simple non-aliased transient resources.
  `RenderGraphFrameResources` owns one resource set per frame slot, and graph
  texture resolution helpers expose dynamic-rendering color targets and
  descriptor-ready sampled image/view/layout triples. Recorder-backed graph
  execution records before/after requirements through `CommandRecorder` around
  pass callbacks. Pass reordering, pass culling, descriptor allocation,
  aliasing, and async scheduling remain future work. `shadow_cube` now uses
  graph-derived shadow-depth, transient
  scene-color, backbuffer, and depth sync, and `fluid_2d` declares a coarse
  simulation-to-render graph boundary.
- `cubey::vulkan::CommandRecorder` is the current low-level recording helper
  used by examples and `fluid_2d` for common Vulkan command-buffer calls. It
  does not own pass scheduling, automatic barriers, descriptor policy, or
  renderer state.
- Scene render-planning helpers live with the scene/component layer and emit
  sorted CPU draw plans from committed scene read views. They use
  `cubey::scene` even though the output feeds rendering, because the helpers
  depend on `SceneReadView` and sit above `cubey::render` in the target graph.
- `RenderableManager3D` lives in the scene/component layer and emits compact
  renderable packets with world matrices, world bounds, and resource handles.
  The cube examples now use Engine-owned scenes, registry-issued handles, mesh
  resource tables, CPU draw planning, primitive mesh data, and shared graphics
  pipeline resources while still owning descriptor writes and Vulkan command
  recording sequence locally. Cube pipelines now read pass metadata from
  `MaterialPassInfo` instead of spelling descriptor layouts, push constants,
  and depth/blend state entirely ad hoc. Fullscreen examples use shared
  pipeline-bind/descriptor/push-constant/fullscreen-triangle helpers for the
  common fullscreen draw shape.
- `LightManager3D` lives in the scene/component layer and emits compact
  CPU-side light packets for directional and point lights. The packets provide
  light kind, color, intensity, direction or world position, and range; shader
  interpretation, light limits, descriptor layout, and GPU upload policy remain
  outside `cubey::render` for now.
- `FrameSlot` gives render callbacks a stable frame-data index, and
  `FrameUniformBuffer<T>` owns one host-visible uniform buffer per frame slot.
  The current windowed host uses real frame slots for overlapping frame
  resources, while swapchain image ownership stays Vulkan-visible.
- Fullscreen passes are not modeled as quad primitives. `fractal`, `fluid_2d`,
  and `shadow_cube` use the oversized single-triangle pattern generated from
  `gl_VertexIndex`, avoiding vertex/index buffers and the internal diagonal of
  a two-triangle fullscreen quad.

## Multipass Direction

`examples/shadow_cube` is the current reference for graph-declared multipass rendering:
build a shadow `cubey::scene::View3D`, build a camera `cubey::scene::View3D`,
record a depth-only pass into a `DepthTexture`, record the scene pass into a
graph-created transient color target, then record a fullscreen present pass that
samples that target into the swapchain. The example declares the pass/resource
flow through `RenderGraphBuilder` and enters those passes through
`CompiledRenderGraph::execute()`. Shadow-depth, scene-depth, scene-color,
backbuffer acquire, and present release transitions are graph-derived and
recorded by recorder-backed graph execution, while graph transient resources are
held per frame slot. The reusable foundation intentionally stops at view/pass
planning, render-item draw intent, target/texture ownership, material pass
metadata, depth-only rendering info, synchronous pass-callback execution,
execution-time resource resolution, simple transient allocation, frame-slot
resource ownership, and graph-owned barrier recording. Render-graph
scheduling, automatic material binding, shadow policy, descriptor ownership, and
transient aliasing remain future work.

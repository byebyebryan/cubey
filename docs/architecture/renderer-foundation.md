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
- material pass metadata, material descriptor instances, and small pipeline
  recipe helpers for the descriptor/pipeline shapes already shared by examples;
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
- material pass declarations, descriptor resources, descriptor writes, and
  binding order;
- graphics/compute pipeline target choice and shader selection;
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
- Full material asset graph, shader reflection, shader hot reload, or pipeline
  cache.
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
- `Texture2D` and `TextureCube` own Vulkan images plus optional samplers for
  the current storage-sampled, transfer-sampled, and cubemap IBL texture paths.
  Texture upload helpers cover tightly packed 2D data and mip-major,
  face-major cubemap data.
- `create_compute_generated_texture_2d` covers the setup-time storage-image
  generation path: texture creation, storage-image descriptor setup, compute
  pipeline creation, layout transitions, dispatch sizing, and final sampled
  layout.
- `DepthTexture` owns a sampled depth image plus an optional sampler for shadow
  maps and other depth-as-texture paths. Layout transitions and descriptor
  image layouts stay explicit at the call site.
- `ShadowMapPass3D` owns the repeated sampled-depth target plus depth-only
  graphics pipeline shape for directional shadow-map passes. Callers still
  choose the shadow view, shader, push constants, graph pass order, and packet
  filtering.
- `Mesh`, `DrawItem`, and `record_draw_item` own uploaded indexed geometry and
  record the minimal bind/draw sequence. Vertex layout, pipeline state,
  descriptors, materials, transforms, and push constants remain caller-owned.
- `InstanceBuffer<T>` owns device-local instance-rate vertex data for the same
  mesh draw path. `VertexInputLayout` now supports multiple bindings so examples
  can model real per-instance attributes instead of shader-only shortcuts.
- `PrimitiveMeshData`, `VertexPositionColor`,
  `VertexPositionColorNormal`, and `VertexPositionColorNormalUv` provide
  CPU-side cube and XZ-plane mesh data for the existing `Mesh` upload path.
  The primitive layer also exposes reusable vertex input layouts for those
  vertex contracts. `create_primitive_mesh_resource` covers the common
  registry-handle plus app-owned mesh-table upload path, while keeping table
  ownership and GPU lifetime explicit. The primitive layer does not create
  renderables, materials, descriptors, pipelines, or scene entities.
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
- `cubey::scene::record_draw_packets_3d` is the low-level scene-side helper
  for packet filtering, per-packet state, resolve, and draw. The higher-level
  `record_pipeline_draw_packets_3d` also binds a caller-selected graphics
  pipeline and either a fixed material instance or a material-instance table
  keyed by packet material handle before recording filtered packets. Pass order
  and push-constant contents remain caller-owned.
- `cubey::render::MaterialPassInfo` is the first explicit material/pass
  metadata contract. It describes pass kind, descriptor layout shape,
  push-constant ranges, and reusable graphics pipeline state.
- `PbrVertex`, `PbrSceneUniforms`, `PbrSkyboxUniforms`, `PbrPostUniforms`,
  `PbrMaterialFactors`, `PbrMaterialUniforms`, `PbrPushConstants`,
  `pbr_skybox_pass_info()`, `pbr_forward_pass_info()`, and
  `pbr_post_pass_info()` define the current PBR contract: one scene
  uniform/shadow/IBL set, one material texture/uniform set, model-only per-draw
  push constants, a skybox environment set, a fullscreen post set, reflectance
  controls, `KHR_materials_specular` factors/textures, and the current opaque
  glTF extension lobes: clearcoat, sheen, anisotropy, and iridescence. Optional
  extension texture slots stay fixed in the descriptor layout, while material
  texture flags gate shader fetches for absent textures. The scene set includes
  irradiance cube, prefiltered cube, and the DFG/BRDF lookup binding.
  `PbrDisplayTransform`
  carries final exposure, tone-map, and output-encoding controls; the reusable
  forward PBR renderer applies it in the post pass after HDR scene-color
  shading.
- `cubey::engine::RendererService` is the Engine-owned renderer instance
  service. It creates, destroys, and fan-outs lifecycle calls to renderer
  instances without owning assets, shader packages, material tables, or render
  settings.
- `cubey::engine::ForwardPbrRenderer3D` is the first reusable renderer policy
  layer above scene/render/vulkan. It owns the repeated shadow map, skybox,
  forward PBR pipelines, HDR scene-color graph target, post pipeline,
  scene/skybox/post material descriptors, depth attachment, and render-graph
  recording for a 3D PBR view. The reusable GLSL package lives under
  `shaders/cubey/forward_pbr`; projects still decide when to create the
  renderer and which frame plans, scene resources, targets, environment
  settings, and display settings to submit.
- `ForwardPbrRenderer3DFrameRequestInfo`,
  `ForwardPbrRenderer3DSceneResources`, and `ForwardPbrRenderer3DRenderRequest`
  are the first explicit renderer request boundary. They group target state,
  scene/view plans, mesh/material/deformation resources, and
  display/environment settings so renderer call sites do not depend on a long
  nested parameter list or raw material descriptor layout plumbing.
- `GeneratedPbrEnvironment` creates setup-time irradiance cube,
  GGX-prefiltered radiance cube, and DFG LUT resources from either deterministic
  generated radiance or equirectangular HDR image data. The shared PBR shader
  helpers remap base color into diffuse color plus material-derived F0 before
  lighting. The DFG lookup stores scale/bias terms plus a white-conductor
  energy term used for Filament-style specular energy compensation. It is a
  checkpoint helper for PBR quality and descriptor shape, not a replacement for
  future prefiltered KTX/KTX2 environment assets and offline filtering.
- `cubey::render::MaterialInstance` owns the descriptor set layout, pool, and
  one or per-frame descriptor sets for one declared `MaterialPassInfo`
  descriptor set. `MaterialDescriptorWriter` keeps descriptor writes tied to a
  material instance set while still requiring callers to choose concrete
  buffers, images, samplers, and image layouts.
- `FrameUniformMaterialInstance<T>` owns the common per-frame uniform buffer
  plus per-frame material descriptor-set shape. It removes repeated descriptor
  setup for simple forward materials without becoming a full material system.
- `ForwardScenePass3D` owns the current forward-color pass resource shape:
  swapchain-sized depth, a file-backed graphics pipeline, clear values, and
  helpers for recording to present or graph-owned color targets. Scene packet
  selection and per-packet state stay in `cubey::scene` and the caller.
- `cubey::render::ShaderProgram`, `GraphicsPipelineResource`, and
  `ComputePipelineResource` own the current shader-module, pipeline-layout, and
  graphics/compute pipeline lifetime shapes. They consume explicit shader stage
  files, descriptor set layouts, push constants, attachment formats, vertex
  input, and `MaterialPassInfo` where applicable; they do not reflect shaders,
  cache pipelines, choose material resources, or record commands. Current
  examples and `fluid_2d` create app/project graphics pipelines through this
  render-level wrapper using file-backed shader stage configs and recipe
  helpers; the lower-level Vulkan pipeline builders remain as tested
  implementation details and escape hatches.
- `cubey::vulkan::DescriptorWriteBatch` remains the lower-level descriptor
  update primitive. `MaterialDescriptorWriter` layers material-instance set
  selection on top of it for graphics material bindings.
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
  resource tables, CPU draw planning, primitive mesh data, shared forward-pass
  resources, generated texture setup, frame-uniform materials, and instance
  buffers while still owning shader-specific push constants and per-demo render
  intent locally. Cube pipelines now read pass metadata from `MaterialPassInfo`
  and `MaterialInstance` instead of spelling descriptor layouts,
  descriptor-set ownership, push constants, and depth/blend state entirely ad
  hoc. Fullscreen examples use shared
  pipeline-bind/descriptor/push-constant/fullscreen-triangle helpers for the
  common fullscreen draw shape.
- `LightManager3D` lives in the scene/component layer and emits compact
  CPU-side light packets for directional and point lights. The packets provide
  light kind, color, intensity, direction or world position, and range; shader
  interpretation, light limits, descriptor layout, and GPU upload policy remain
  outside `cubey::render` for now.
- `cubey::scene::scene_builder.h` contains small transaction helpers for
  common renderable, camera, and directional-light entity setup. They reduce
  repeated example transaction boilerplate without replacing explicit
  `SceneTransaction` ownership.
- `FrameSlot` gives render callbacks a stable frame-data index, and
  `FrameUniformBuffer<T>` owns one host-visible uniform buffer per frame slot.
  The current windowed host uses real frame slots for overlapping frame
  resources, while swapchain image ownership stays Vulkan-visible.
- Fullscreen passes are not modeled as quad primitives. `fractal_2d`,
  `fluid_2d`, and `shadow_cube` use the oversized single-triangle pattern
  generated from `gl_VertexIndex`, avoiding vertex/index buffers and the
  internal diagonal of a two-triangle fullscreen quad.

## Multipass Direction

`examples/shadow_cube` is the current reference for graph-declared multipass rendering:
build a shadow `cubey::scene::View3D`, build a camera `cubey::scene::View3D`,
record a depth-only pass through `ShadowMapPass3D`, record the scene pass into
a graph-created transient color target, then record a fullscreen present pass
that samples that target into the swapchain. The example declares the
pass/resource flow through `RenderGraphBuilder` and enters those passes through
`CompiledRenderGraph::execute()`. Shadow-depth, scene-depth, scene-color,
backbuffer acquire, and present release transitions are graph-derived and
recorded by recorder-backed graph execution, while graph transient resources are
held per frame slot. The reusable foundation intentionally stops at view/pass
planning, render-item draw intent, target/texture ownership, material pass
metadata, depth-only rendering info, synchronous pass-callback execution,
execution-time resource resolution, simple transient allocation, frame-slot
resource ownership, and graph-owned barrier recording. Render-graph
scheduling, transient aliasing, and broader material systems remain future
work. `shadow_cube` still uses the lower-level `ShadowMapPass3D` helper
directly, while `gltf_viewer` now creates an engine-owned
`ForwardPbrRenderer3D` through `RendererService` and records each frame through
`ForwardPbrRenderer3DFrameRequestInfo` and `ForwardPbrRenderer3DRenderRequest`
for reusable shadow, skybox, HDR scene color, PBR forward, and post-pass graph
recording. Its implementation is split by responsibility: resource lifetime,
graph declaration/execution, and pass recording stay in separate source files
behind the public renderer/request contract. The PBR path now treats material
alpha policy as render policy:
masked materials still write depth and cast cutout shadows through an
alpha-tested shadow path, while blended materials render after opaque/masked
packets with depth testing enabled and depth writes disabled. PBR blended
materials use premultiplied source-over blending; material textures and factors
stay straight alpha until the fragment shader emits final premultiplied color.

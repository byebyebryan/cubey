# Roadmap

This is the living roadmap for Cubey 2.0. It should reflect the current plan,
not a frozen promise. When implementation changes the plan, update this file
before the old assumptions become tribal knowledge.

## Current Direction

Cubey 2.0 is a native Vulkan desktop GPU workbench for procedural graphics
experiments. Vulkan is the architecture driver for the shared runtime and
graphics foundation.

See the [architecture notes](architecture/README.md), especially the
[Vulkan abstraction map](architecture/vulkan-abstractions.md) for planned
framework layers and foundation rules,
[renderer foundation](architecture/renderer-foundation.md) for the first
`cubey::render` contracts above Vulkan,
[render graph direction](architecture/render-graph.md) for current and future
pass/resource graph vocabulary,
[procedural generation foundation](architecture/procedural-generation.md) for
shared source-field, noise, and operator direction,
[Configuration V2](architecture/configuration.md) for the project-owned typed
config clean break,
[host and engine](architecture/host-engine.md) for the GLFW/windowed host and
scoped engine ownership path, and
[threading and async design](architecture/threading-and-async.md) for the
async-ready runtime boundary.

## Current Readiness Checkpoint

Status: Configuration V2 is complete; the next product or foundation
checkpoint is ready to be selected from `main`.

The recent foundation push landed shared runtime, renderer, shader,
environment, UI, and project-owned configuration infrastructure. Retired
cloud/planet applications and terrain-study viewers are gone, and the global
configuration registry has now been removed. Product-facing or engine-level
work can proceed without a compatibility config layer.

Current stable foundation pieces:

- shared windowed/headless host flow, queued capture/video encoding,
  GPU-owner submission, deferred destruction vocabulary, and project runtime
  services;
- progressive whole-generation initialization over shared CPU jobs and typed
  GPU-owner results, now exercised by terrain products, Planet surface
  products, generated atmosphere atlases, and glTF atmosphere-atlas consumers
  with placeholder-first windowed presentation, deterministic headless
  completion, atomic activation, and deferred retirement, plus a bounded
  worktree-local generated-artifact cache that removes repeat night-sky, lunar
  atlas, planet-surface, and terrain backdrop-product generation;
- shared config descriptors and ImGui option controls for nested project UI,
  config templates, CLI overrides, and option help text;
- composable `cubey::config::Schema`, the typed host-owned
  `CommonRunConfig`, target-specific typed facades for every active executable,
  shared subsystem schema fragments where semantics truly match, and direct
  schema-backed JSON/CLI/template handling. The former `RunConfig`, global
  option registry, parser/runner, and host compatibility conversion are gone;
- shared procedural/noise helpers on CPU and GLSL for project-local terrain,
  water, atmosphere, ocean, and cloud fields;
- shared sky/celestial/atmosphere state used by atmosphere, ocean,
  glTF/PBR, water, pyro, and planet-scale adapters;
- surface-only Cloud V1 promoted into the shared atmosphere environment and
  consumed directly by atmosphere, glTF Viewer, ocean, and Water 3D. The shared
  runtime owns visible cloud products plus the cached PBR environment; ocean
  adds bounded local shadows and planar reflection, while aerial/orbit cloud
  work remains explicitly deferred;
- Surface Ocean V1 closed around a fixed-cost C0/C1 spectral core, independent
  Calm/Windy/Stormy look presets, shared atmosphere/cloud lighting, and an
  explicit terrain/water-body handoff boundary;
- Terrain V1 promoted to one validated external-heightfield far-backdrop
  product with source-bounds-centered deterministic placement, continuous
  cached geometry, a persistent recipe-keyed CPU product, shared
  atmosphere/cloud/HDR lighting, submission-safe runtime replacement, terrain
  and glTF consumers, executable headless smokes, a product review UI,
  accepted mineral-led visual closure across selected/raw placement and the
  day-to-night envelope, and opt-in historical studies;
- renderer foundation pieces for target views, render graph declaration,
  material/pass metadata, forward PBR, generated/HDR IBL, surface LOD
  planning, and primitive mesh/draw helpers;
- shader source ownership cleaned up enough that ocean and atmosphere entry
  shaders are split, shared shader packages expose include dependencies, and
  tests audit those package dependencies.

The bounded progressive-initialization slice is complete for generated
atmosphere atlases, Planet surface products, and terrain products; glTF Viewer
also consumes the shared staged atmosphere-atlas lifecycle. Further adoption
should be driven by a concrete consumer such as glTF asset loading. General
asset streaming, partial terrain residency, split-queue scheduling, and
per-frame upload budgets remain deferred until profiling shows that
whole-generation installation is the real bottleneck.

Recommended next feature or foundation streams:

- renderer foundation: use the shared dynamic environment handoff for future
  atmosphere/PBR consumers instead of adding project-local probe descriptors;
  deepen render-graph or command ownership only around a concrete repeated
  project need.
- glTF/asset pipeline: extend progressive initialization to asset loading if
  profiling confirms it is the next visible host/engine bottleneck.
- `projects/terrain`: far-backdrop V1 is closed. Reopen it only for a concrete
  consumer failure or a bounded next product; glTF Viewer already proves the
  shared path. Close terrain and planet-scale terrain remain separate projects.
- `projects/planet`: the orbital product is closed for its current scope.
  Surface terrain, local/global morphing, streaming/residency, and ocean
  handoff require a separately scoped planet-surface product rather than
  expansion of the active executable by default.
- `projects/atmosphere`: cloud look-dev and surface horizon polish only when it
  directly improves current surface/ocean views; high/aerial/orbit cloud
  products should stay deferred until a dedicated batch.
- coastal shoreline/bathymetry composition remains deliberately deferred; it
  is not the default next stream.

Known non-blockers:

- The filtered cloud environment is available to general PBR, but visible cloud
  composition, cloud shadows, and planar reflections remain explicit products;
  one cached cubemap should not be treated as a replacement for them.
- `cloud_march.comp`, `surface_cloud_march.comp`, and a few volume diagnostics
  shaders remain future split targets; the active shader foundation is
  sufficient for current feature work.
- Planet-surface streaming and planet/ocean composition remain possible future
  product tracks, not unfinished obligations of the orbital executable or
  reasons to block ocean or atmosphere iteration.
- Configuration V2 is complete and has no legacy compatibility layer or dual
  system.

## Phase 0: Repo Foundation

Status: complete for the first implementation slice.

Goal: make `main` a clean place to build from before building the new native
runtime foundation.

- CMake presets for development, release, sanitizer, and clang-tidy builds.
- Formatting, linting, editor, Git text-normalization, and warning defaults.
- License, changelog/release-note source, roadmap, and working notes.
- C++ style guide for formatting, naming, ownership, and Vulkan structure.
- Explicit monorepo shape: layered `cubey::*` library targets, `examples/` for
  focused reference programs, and `projects/` for first-class graphics work.

Exit criteria:

- A new contributor or future agent can configure, build, and understand the
  project direction from the repo docs.
- Empty-project build and tooling presets work cleanly.

## Phase 1: Windowed Vulkan Runtime Skeleton

Status: windowed/headless host, frame-overlap, and first artifact checkpoints
complete; later queue and recording optimizations remain deferred.

Goal: build maintainable mainline Vulkan modules and prove visible desktop
rendering without turning the library into a generic game engine.

- Layered `cubey::*` library targets with public headers under `include/cubey/`
  and an aggregate `cubey::cubey` target.
- First runnable targets under explicit `examples/` and `projects/`
  directories, not a generic `cubey` executable.
- CLI/config surface for windowed examples and future headless modes.
- GLFW window creation and Vulkan surface ownership.
- Vulkan instance, physical device, logical device, queues, validation layers,
  and debug messenger setup.
- Swapchain acquisition, presentation, out-of-date handling, and compositor
  resize recovery.
- Frame resources: command buffers, semaphores, fences, and the frame-slot
  synchronization path.
- Build-time GLSL to SPIR-V shader flow.

Completed criteria:

- `cubey::cubey` builds as the aggregate library target and examples link
  against it.
- `spinning_cube`, `textured_cube`, `shadow_cube`, `instanced_cubes`,
  `material_cubes`, `particle_cubes`, `fractal_2d`, and `smoke_2d` open
  windows and render through dynamic rendering.
- Validation-layer smoke can be required from the command line.
- Resize and swapchain recreation remain first-class tested behavior.
- Headless smoke renders and writes an inspectable PNG.

Open criteria:

- Frame overlap is now part of the runtime foundation. Timeline semaphores,
  split queues, and parallel command recording remain future optimizations.

Current checkpoint:

- Layered `cubey::*` static library targets exist, with aggregate
  `cubey::cubey` for convenience.
- Reusable `cubey::vulkan::Instance`, `Device`, `Buffer`, `Image`, `Sampler`,
  `Swapchain`, `CommandPool`, and `FrameResources` components now own
  validation/debug-utils setup,
  physical-device selection, logical-device lifetime, queue access, swapchain
  image/view ownership, buffer/image allocation, sampler ownership,
  command-pool ownership, command-buffer allocation, per-frame-slot
  command/sync resources, and per-image present synchronization.
- Reusable `cubey::FrameClock`, `cubey::Transform2D`, `cubey::Transform3D`,
  `cubey::TransformManager2D`, `cubey::TransformManager3D`, `cubey::Camera2D`,
  `cubey::Camera3D`, `cubey::CameraManager2D`, `cubey::CameraManager3D`,
  `cubey::RenderableManager3D`, `cubey::LightManager3D`, and
  `cubey::OrbitController` cover basic frame timing, explicit 2D/3D affine
  transform boundaries, quaternion-backed 3D rotation, entity-backed
  parent/child world transforms, explicit 3D affine matrix overrides for
  imported node transforms, shared 2D/3D camera state, entity-backed 3D
  renderable/light packets, and mouse-driven orbit input.
- Reusable `cubey::input::InputState`/`InputFrame` provide per-frame keyboard
  and mouse polling over the GLFW callback stream, with shared pointer-drag,
  camera-backed 2D pan/zoom, and input-aware orbit-control helpers.
- Reusable `cubey::host::FrameStats` covers lightweight
  FPS/frame-time/extent/triangle telemetry formatting for windowed examples.
- Reusable `cubey::config::Schema` provides a composable typed option layer:
  stable paths, labels, groups, value kinds, ranges, enum choices, help text,
  config templates, named CLI flags, and generic `--set path=value` overrides.
  `cubey::host::CommonRunConfig` separates host-consumed state from project
  state. Every active executable composes that common schema with only its live
  typed project options and validation. Shared subsystem fragments exist only
  where multiple consumers share path, default, and semantic contracts. JSON,
  named flags, `--set`, and target-specific templates use this path directly;
  the legacy global registry and host conversion have been deleted.
- Reusable ImGui helpers now cover hierarchical debug-panel groups, standard
  option hover help, command buttons, and common
  bool/int/unsigned-int/float/vector/enum/color controls. Active ocean,
  atmosphere, terrain, smoke, water, fire, and explosion panels use those
  helpers for standard controls while keeping project commands, diagnostics,
  reset behavior, and custom selectors project-owned. Startup panel disclosure
  is curated so diagnostics, source details, solver tuning, and deep render
  controls stay collapsed until needed.
- Shared performance UI now groups common frame stats, process CPU/RAM, VRAM
  budget, project-owned GPU allocation, workload counters, and GPU pass timings
  so demos do not reimplement those rows in each diagnostics panel.
- Reusable `cubey::math` wraps GLM matrix/vector/quaternion types and the
  current Vulkan transform/projection conventions used by the shared
  transform/camera helpers and cube examples.
- Reusable `cubey::procedural` now provides the first procedural foundation
  slice: local 2D scalar fields, centered grid sampling, deterministic 2D/3D
  value-noise/FBM/ridged-FBM, scalar operators, field normalization, weighted
  3x3 blur, and matching small GLSL helper includes. Retained terrain studies,
  active terrain, Planet, ocean, water, atmosphere/sky, and active cloud consume
  shared helpers where their formulas match, while domain drivers remain
  project-owned.
- Reusable `cubey::vulkan::ShaderModule` exists, and CMake can compile GLSL to
  SPIR-V with `glslangValidator` for example targets, including shared include
  directories and dependency tracking. Reusable `cubey::vulkan::read_spirv_file`
  loads compiled shader bytecode through the Vulkan `shader_bytecode` helper
  for shader-backed examples and future projects.
- Reusable `cubey::vulkan::ImmediateCommands` plus buffer helpers support
  one-shot setup uploads into device-local buffers.
- Reusable `cubey::vulkan::CommandRecorder` wraps the repeated non-owning
  command-buffer recording calls used by current examples and `smoke_2d`:
  begin/end, dynamic rendering boundaries, image layout transitions, pipeline
  and descriptor binding, push constants, draws, indexed draws, and dispatches.
- Reusable `cubey::vulkan::SubmissionCoordinator` serializes queue submission,
  issues monotonic GPU submission tickets, and lets `RenderContext` mark
  frame-slot tickets completed after the matching fence wait.
- Reusable `cubey::vulkan::GpuRuntime` is the first strict GPU-owner work
  queue: host contexts expose `gpu()`, worker threads can enqueue labeled work,
  and hosts run a threaded owner by default while keeping inline execution as an
  explicit deterministic mode.
- Reusable `cubey::vulkan::DepthAttachment` owns depth image/view setup and
  shared depth format selection.
- Reusable `cubey::vulkan` transfer/readback helpers cover readback buffers,
  sampled image configs, buffer-image copy regions, buffer-to-image copies,
  image-to-buffer copies, and current storage, transfer, sampled-image readback,
  and sampling image layout transitions.
  The headless path also uses an explicit offscreen color render-target config
  and a color-attachment-to-readback transition.
- Reusable `cubey::read_binary_file` and `cubey::write_binary_file` cover
  generic byte I/O; `cubey::write_png_rgba8` wraps the vendored
  `stb_image_write` PNG path behind the `image_io` API.
- Reusable `cubey::jobs::JobSystem`, `InlineExecutor`, and `JobHandle` provide
  the first CPU job facade behind Cubey APIs without exposing a third-party
  executor.
- Reusable `cubey::CaptureQueue`, `CaptureTicket`, `CaptureBacklog`, and
  `QueuedVideoEncoder` provide async-shaped PNG encoding, bounded multi-file
  export draining, and ordered MP4 frame encoding over completed RGBA pixel
  buffers.
- Reusable `cubey::VideoEncoder` provides optional libav/FFmpeg-backed H.264
  MP4 output for queued headless capture sessions when the system development
  packages are available.
- Reusable `cubey::UploadQueue`, `UploadTicket`, and `QueuedUpload` provide the
  first CPU-owned upload request queue for GPU-owner draining, with pending,
  completed, and failed ticket status.
- Reusable Vulkan submission primitives
  `cubey::vulkan::GpuSubmissionTicketIssuer`, `GpuSubmissionTicket`, and
  `DeferredGpuDestructionQueue` provide the first GPU submission retirement
  vocabulary outside the core target.
- Reusable `cubey::Engine` provides the first scoped root owner for project
  runtime services, render resource handle identity, and scene
  creation/destruction without becoming a singleton or taking over host/device
  ownership yet. It lives in the engine layer because it composes runtime,
  scene, and render-registry services rather than being scene state itself.
- Reusable `cubey::ProjectContext`, `ProjectFrame`, `ProjectExtent`,
  `RenderPacket`, `ProjectRuntimeServices`, `ProjectRuntimeAdapter`, and
  `ProjectLike` provide the first async-ready project runtime vocabulary,
  service ownership bundle, and thin host bridge.
- Reusable `cubey::ProjectGpuServices` provides the optional project-facing GPU
  bridge for draining pending uploads into owner-thread GPU work, marking upload
  completion/failure, and retiring deferred work from completed GPU submission
  tickets.
- Reusable `cubey::render` target, texture, mesh, and draw-item contracts now
  sit above `cubey::vulkan`: windowed/headless color target views,
  dynamic-rendering target setup, generated/uploaded sampled 2D and cubemap
  texture ownership, sampled depth texture ownership, generated and HDR-backed
  PBR IBL environment resources, depth-only rendering setup, indexed mesh upload,
  minimal indexed draw recording, explicit frame-slot identity, per-frame
  uniform buffers, frame-uniform material instances, compute-generated sampled
  textures, instance-rate vertex buffer helpers, shader-program and
  graphics-pipeline resource
  ownership for graphics passes, CPU-side cube/XZ-plane primitive mesh data
  with matching single- and multi-binding vertex input layouts, a helper for primitive mesh
  registry/table upload, and generational mesh/material handles issued by
  `RenderResourceRegistry`.
- Reusable atmosphere and terrain-adjacent render helpers now cover atmosphere
  frame uniforms, background atlas bindings, lunar and night-sky generated
  atlases, runtime atmosphere reflection-probe cubemaps for dynamic PBR
  environment lighting, shared celestial mechanics plus fullscreen sky/body
  frame helpers, a shared lightweight environment-lighting uniform/GLSL include
  consumed by water and volumetric pyro, terrain-ocean field packing, and shared
  surface LOD planning: 2D clipmap grids for ocean and archived procedural
  terrain diagnostics, and adaptive quadtree patch planning for planet-scale
  surface LOD. The active Terrain V1 backdrop uses fixed cached sectors instead.
- Reusable `cubey::render::ResourceTable`,
  `cubey::render::RenderItem`,
  `cubey::render::MaterialPassInfo`,
  `cubey::render::PbrPostUniforms`,
  `cubey::render::MaterialInstance`,
  `cubey::scene::RenderDrawPacket3D`, and
  `cubey::scene::build_render_draw_packets_3d` provide the first CPU
  render-planning layer: renderer-facing draw intent, handle-to-resource
  resolution, live resource validation, material tag attachment, material/pass
  metadata, material descriptor set ownership, world bounds propagation, and
  deterministic draw sorting without owning pass order, shader selection, or
  renderer policy.
- Reusable `cubey::ForwardPbrRenderer3D` now shades glTF/PBR scene and
  skybox passes into a graph-created `R16G16B16A16_SFLOAT` HDR scene color
  target, then samples that target in a fullscreen post pass for exposure,
  tone mapping, and final output encoding into the caller's swapchain or
  headless target.
- Reusable `cubey::render::ForwardScenePass3D` is the lower-level/simple
  forward-color pass helper: swapchain-sized depth, a file-backed graphics
  pipeline, clear values, and record helpers for either present or graph-owned
  color targets. Reusable PBR renderer policy lives above it in
  `ForwardPbrRenderer3D`; cube examples use example-local common helpers for
  repeated simple pass setup without promoting that glue into engine policy.
- Reusable `cubey::scene::View3D`, `Environment3D`, and
  `RenderFramePlan3D` provide the first CPU 3D view-planning boundary over
  `SceneReadView`: camera matrices, viewport aspect, ambient-only environment,
  draw packets, light packets, and conservative CPU frustum culling.
- Reusable `cubey::scene::RenderPassPlan3D` and `FrameRenderPlan3D` provide a
  small explicit pass-list contract for multi-view and multi-pass command
  recording without introducing a render graph.
- Reusable `cubey::render::RenderGraphBuilder` and `CompiledRenderGraph`
  provide the first render graph declaration, validation, and synchronous
  execution shell for imported/transient texture and buffer resources, ordered
  graphics/compute/transfer passes, pass/resource usage, optional material pass
  metadata, imported initial/final state, transient first-use transitions,
  execution-time resource resolution, non-aliased transient allocation, and
  in-graph sync requirements. It also provides per-frame-slot graph resource
  ownership, recorder-aware execution context, a frame executor for
  command-buffer begin/end plus graph resource allocation, and sampled-texture
  resolution helpers for descriptor updates. `shadow_cube` now uses a
  graph-created transient scene color target plus graph-derived shadow-depth,
  scene-depth, scene-color, backbuffer, and present transitions recorded by
  graph execution while pass callbacks still own descriptors, pipelines, and
  app-specific resource policy.
  The graph is now used by `shadow_cube`, `ForwardPbrRenderer3D`, atmosphere,
  cloud reference, ocean, terrain, Smoke 2D, Water 2D, Water 3D, and the shared
  Pyro 3D renderer. Graph-owned barriers cover declared pass boundaries while
  solver-internal synchronization and project render intent remain explicit.
- Reusable `cubey::scene` transaction helpers cover common renderable, camera,
  and directional-light entity setup while keeping entity/component ownership
  explicit.
- Reusable `cubey::vulkan::PipelineLayout`, `GraphicsPipeline`,
  `ComputePipeline`, `DescriptorSetLayout`, and `DescriptorPool` own basic
  pipeline and descriptor lifetimes while still taking raw Vulkan create-info
  structs.
- Reusable `cubey::vulkan` descriptor helpers build layout bindings, pool
  sizes, descriptor writes, and descriptor updates for current uniform-buffer,
  storage-buffer, storage-image, and combined image sampler paths.
- Reusable `cubey::vulkan::DescriptorWriteBatch` stages mixed descriptor writes
  in append order and owns their backing info storage until update submission;
  `cubey::render::MaterialDescriptorWriter` applies that lower-level primitive
  to material-instance descriptor sets.
- Reusable `cubey::vulkan::DescriptorSetInfo` and `DescriptorSetBundle` cover
  the current one-layout/one-pool/one-set descriptor shape used by examples.
- Reusable `cubey::vulkan::DescriptorSetArray` covers the same owned
  layout/pool shape for multiple sets, primarily for per-frame bindings.
- Reusable `cubey::vulkan::PipelineLayoutInfo` and `ComputePipelineInfo` build
  the current pipeline-layout and compute-pipeline create-info shapes.
- Reusable `cubey::vulkan::DynamicGraphicsPipelineInfo` builds the current
  dynamic-rendering graphics pipeline create-info shape for color and
  depth-only pipelines, optional depth, and blending. `cubey::render`
  `ShaderProgram`, `GraphicsPipelineResource`, and `ComputePipelineResource`
  now package shader modules, pipeline layouts, and graphics/compute pipeline
  lifetime for the examples and `smoke_2d`. Those examples use file-backed
  graphics pipeline configs, source descriptor layout shape, push constants,
  depth state, blend state, and pass participation from `MaterialPassInfo`,
  source standard cube/plane vertex data and input layouts from
  `cubey::render` primitives, and use render-target/fullscreen/scene-draw
  helpers while still owning descriptor writes, Vulkan resource ownership, and
  command binding policy explicitly.
- Reusable `cubey::vulkan` image-transition helpers build the current
  color/depth/storage/transfer/sampled-depth layout transitions, while
  dynamic-rendering helpers build color/depth attachment descriptors without
  owning render policy.
- Reusable `cubey::vulkan::QueueSubmit` centralizes the current binary-semaphore
  queue-submit shape used by the submission coordinator.
- Reusable `cubey::vulkan::RenderContext` exposes explicit `begin_frame` and
  `end_frame` calls for the common surface-backed acquire, command reset,
  coordinator-backed submit, present, frame-slot advance, per-image in-flight
  fence wait, and out-of-date result path used by all current windowed examples.
- Reusable `cubey::vulkan::SwapchainRecreateTracker` guards repeated
  out-of-date/suboptimal recreate loops across all current windowed examples.
- Optional `cubey_host` target owns the first GLFW-backed host/engine layer:
  window lifetime, surface creation/destruction, key/pointer dispatch, windowed
  frame loop, frame timing, window-title stats, stdout frame-stat summaries for
  bounded runs, optional periodic frame-stat logging, and swapchain recreate
  orchestration. Render callbacks receive `WindowedRenderFrame`, including the
  active frame slot and swapchain color target view. The windowed host defaults
  to two frame slots.
- `examples/spinning_cube` links against `cubey`, compiles vertex/fragment
  shaders at build time, draws an indexed cube through `cubey::render::Mesh`,
  `DrawItem`, `MeshResourceTable`, registry-issued mesh/material handles, and
  CPU render frame plans built from scene renderables, updates scene transforms
  during the project `update()` phase, builds an MVP matrix from `View3D` camera
  planning through push constants, and uses `ForwardScenePass3D` through the
  shared host layer. It follows the current cube-example split: app callback
  shell, resource setup, scene/update code, and frame recording live in separate
  local files.
- `examples/textured_cube` links against `cubey`, generates a `Texture2D` with a
  compute shader writing a storage image, transitions it for shader sampling,
  binds per-frame scene uniforms plus a combined image sampler through
  `FrameUniformMaterialInstance`, and draws an interactive shaded
  textured indexed cube through `cubey::render::Mesh`, `DrawItem`,
  `MeshResourceTable`, registry-issued mesh/material handles, CPU render frame
  plans built from scene renderables and a scene-owned directional light
  packet, ambient-only `Environment3D`, dynamic rendering, per-face normals,
  shared GLSL Lambert lighting, shared transform/model matrices, shared camera
  projection, and shared math helpers through the shared host layer.
- `examples/shadow_cube` links against `cubey`, records a graph-declared
  three-pass frame with a depth-only directional shadow map pass, a scene pass
  into a graph-created transient color target, and a fullscreen present pass
  that samples the scene target into the swapchain. It executes those pass
  bodies through `CompiledRenderGraph` and exercises orthographic `Camera3D`,
  sampled depth targets, depth-only dynamic rendering, graph-derived
  sampled-depth/scene-color/backbuffer sync, render-graph pass declarations,
  transient allocation, and CPU pass planning.
- `projects/gltf_viewer` links against `cubey`, loads static glTF/glb assets
  through `cubey::asset`, imports scene/render resources through the shared
  engine glTF scene importer, uploads PBR material textures, builds shadow and
  scene frame plans, and delegates reusable shadow/skybox/PBR graph recording
  to an engine-owned `ForwardPbrRenderer3D` created by `RendererService`. It
  submits per-frame target, view, `ForwardPbrRenderer3DSceneResources`, and
  display settings through the forward-PBR request helper, binds generated or
  HDR-backed IBL resources, supports windowed and headless capture output, and
  falls back to a generated PBR cube when no input asset or sample-asset
  checkout is configured.
- `projects/pbr_furnace` links against `cubey` and renders a white-furnace PBR
  validation grid: shared UV-sphere primitive mesh, roughness columns, metallic
  rows, uniform white IBL cubemaps, Filament-style base-color/reflectance
  remapping, DFG-based energy compensation, reusable PBR material uniform
  descriptors, and windowed plus headless capture output.
- `examples/instanced_cubes` links against `cubey` and draws a cube grid through
  a single renderable packet, real instance-rate vertex input, one shared cube
  mesh, and `instance_count` propagation from scene primitive to Vulkan draw.
- `examples/material_cubes` links against `cubey` and draws multiple cube
  entities with distinct material handles, per-material uniform data, and
  per-packet material instance binding.
- `examples/headless_cube` links against `cubey`, creates no GLFW window or
  surface, renders an indexed cube into an offscreen color target through
  dynamic rendering, copies it into a readback buffer, and writes a PNG
  artifact.
- `projects/fractal_2d` links against `cubey`, renders a fullscreen
  Mandelbrot-style fragment shader, supports camera-backed pan/zoom/reset
  navigation through the shared host layer, and reuses the shared headless PNG
  host for no-window output.
- `examples/particle_cubes` links against `cubey`, updates a storage-buffer
  cube-particle field with a per-frame compute shader, inserts an explicit
  compute-to-vertex memory barrier, and renders the result as indexed cube
  instances through the shared host layer.
- The current device model intentionally selects one queue family for required
  graphics, compute, and present capabilities. Split queue-family support is a
  future framework slice, not part of the current example-local compute path.
- Windowed example host mechanics now live in `cubey_host`; swapchain-sized
  render resources, pipeline layout choices, and command recording sequence
  remain example-local.
- Dev CTest covers the target in graphical, no-display terminal, and headless
  artifact sessions through shared windowed and PNG smoke helpers.
- Split graphics/compute/present queue-family support, GPU capture polling,
  timeline semaphores, parallel command recording, and richer asset features
  remain future slices.

Alignment: the Vulkan layer now has visible windowed examples plus a minimal
headless PNG path. Cubey has the first async-ready runtime vocabulary: CPU jobs
behind Cubey APIs, queued upload/capture requests, GPU submission tickets,
deferred cleanup, and project lifecycle concepts. Larger systems such as a
threaded renderer, split queues, or parallel command recording should be
designed from clear contracts and established graphics precedent before
implementation; they do not need duplicated project code as a prerequisite, but
they must stay narrow and testable.

## Phase 2: Headless Output And Runtime Boundary

Status: initial pass complete.

Goal: prove that Cubey can produce inspectable GPU artifacts without a desktop
surface while keeping the runtime boundary concrete.

- Added `examples/headless_cube` as an explicit no-window cube smoke.
- Added `stb_image_write` as a small third-party dependency for PNG artifact
  output.
- Created an offscreen color target through the existing Vulkan resource model.
- Added only the layout transitions and copy helpers needed for that concrete
  render-target readback path.
- Wrote a simple deterministic PNG artifact.
- Added CTest coverage for the no-display success path, including output-file
  existence and PNG signature checks.
- Follow-up extraction added the shared no-GLFW
  `cubey::host::HeadlessCaptureHost` once repeated examples and the first
  project revealed a concrete reusable shape. `HeadlessPngHost` remains as the
  original compatibility name.

Exit criteria:

- A no-window command can run with validation enabled and produce an inspectable
  PNG artifact in a terminal session.
- The offscreen path reuses `cubey` resource/readback helpers and exposes any
  new layout assumptions by name.
- The docs identify what belongs to examples/projects versus reusable host
  plumbing.

## Phase 3: Fractal Project

Status: initial pass complete.

Goal: keep the fullscreen fractal path as a project because it has interaction,
camera state, tests, and headless output.

- Added `projects/fractal_2d` as a fullscreen Mandelbrot-style shader project.
- Kept windowed setup, command recording sequence, and navigation
  example-local.
- Reused existing dynamic graphics pipeline helpers.
- Reused the shared headless PNG host for deterministic output.
- Did not promote fullscreen helpers; no durable contract was clearer than the
  current explicit example code yet.

Exit criteria:

- The project runs interactively with a window.
- The same project can run headlessly and produce a deterministic PNG artifact.
- README contains the exact commands for local smoke testing.

## Phase 3.5: Particle Cube Example

Status: initial pass complete.

Goal: keep the compact particle path as a cube-first example that carries
forward the original Cubey attractor feel while staying below the threshold for
a `projects/` runtime.

- Added `examples/particle_cubes` with a deterministic attractor-style cube
  particle field.
- Rendered particles as indexed cube instances, not points, quads, or geometry
  shader billboards.
- Updated particle position/velocity in a compute shader over a storage buffer,
  then rendered the same buffer in the graphics pass.
- Kept controls example-local: Space pauses updates, `R` resets the field, and
  Escape closes the window.
- Did not promote particle simulation, storage-buffer, or host abstractions; this
  remains reference example code until a real project repeats the shape.

Exit criteria:

- The example runs interactively with validation enabled.
- The compute-to-graphics storage-buffer synchronization is explicit in command
  recording.
- The docs identify why this is still an example and not the first project.

## Phase 4: Threading And Async Runtime Boundary

Status: initial pass complete.

Goal: make the first real project fit a non-stalling Vulkan model without
building a full threaded renderer too early.

- Added a small `cubey::jobs` facade around a standard-library worker executor.
- Keep third-party task/executor types out of public Cubey APIs.
- Added job-backed PNG capture encoding as the first queued work consumer, then
  extended the same capture queue with ordered video encoding sessions.
- Added optional headless MP4 capture: deterministic capture-frame timing,
  `--capture video`, `--fps`, default `cubey-output.mp4`, and CPU video encoding
  on a shared queued encoder behind the headless host.
- Added CPU-owned upload requests that can be drained by the GPU owner.
- Added GPU submission tickets and deferred destruction helpers for future
  in-flight GPU lifetime tracking.
- Added `SubmissionCoordinator` for serialized queue submission and frame-slot
  GPU completion marking.
- Added `GpuRuntime` as the public GPU-owner work queue over the submission
  coordinator; windowed/headless hosts expose it through their contexts and run
  it threaded by default, with inline mode remaining explicit for tests.
- Added project runtime vocabulary for setup, update, render-packet, resize,
  and shutdown contracts.
- Added `ProjectRuntimeServices` and moved `smoke_2d` simulation timing onto
  `ProjectFrame` while keeping command recording sequence project-local.
- Added `ProjectRuntimeAdapter` and moved `smoke_2d` onto it while keeping host
  callbacks and command recording sequence project-local.
- Added `ProjectGpuServices` for project-facing upload draining,
  upload-completion/failure status, queue-idle waits, and readback tickets.
  `GpuRuntime` alone owns deferred destruction against actual GPU submissions.
- Shape GPU readbacks and deeper capture polling as queued work.
- Keep GPU ownership and `VkQueue` submission serialized through one owner.
- Keep examples direct; require longer-lived `projects/` to use the
  async-ready structure once it exists.

Exit criteria:

- Docs identify the app thread, GPU owner, worker executor, frame packet,
  upload request, capture request, and GPU submission ticket concepts.
- The job facade can run CPU work, propagate errors, shut down cleanly, and
  provide deterministic test behavior.
- Upload/capture APIs can start synchronous internally while exposing queued
  semantics to project code.
- A project-like type can be checked at compile time against the setup, update,
  render-packet, resize, and shutdown contract.

## Phase 5: First Real Project

Status: complete for the current active project portfolio.

Goal: prove the framework with non-trivial procedural graphics projects and let
repeated project needs shape the host/engine API.

Project checkpoints:

- The fluid family now covers Smoke 2D, Water 2D, Water 3D, and shared Pyro 3D
  fire/explosion products. Each has windowed interaction, deterministic
  headless PNG/MP4 output, project-owned solver policy, and graph-declared
  compute/render boundaries where that contract is useful.
- `projects/atmosphere` now hosts the active surface cloud/weather layer.
  The shared `CloudLayerRuntime` keeps the texture-backed coherent density path,
  generated 3D base/detail noise, generated 2D weather map, spherical-shell
  direct march, cloud product/composite passes, nested controls, and diagnostics
  for raw density, transmittance, lighting, distance, and steps. The historical
  planet-aware prototype remains historical evidence for surface/high/orbit
  lessons, while retired `projects/cloud_ref_2` remains an architecture
  reference. Cloud V1 is intentionally surface-only: atmosphere is the tuning
  host; glTF Viewer, ocean, and Water 3D consume its accepted products.
- `projects/ocean` exercises spectral FFT water rendering, atmosphere/material
  integration, horizon-scale local frames, curved far-surface mapping, and
  terrain-field handoff vocabulary. It is now treated as a local-water renderer
  and future donor rather than the owner of planet-scale navigation. Surface
  Ocean V1 fixes the default cost at C0/C1 and adds Calm/Windy/Stormy behavior
  presets without conflating sea state with quality or water-body ownership.
- `projects/terrain` owns the active external-heightfield far-backdrop product:
  validated source/placement, cached CPU products, fixed continuous sectors,
  shared environment lighting, runtime replacement, and glTF consumption. The
  retained terrain studies no longer own runnable viewers.
- `projects/planet` is the orbital-only planet product. It owns a deterministic
  cached direction-domain surface, phase presets, shared sky/celestial
  composition, fixed-sphere rendering, and headless review coverage. Surface
  terrain, patch LOD, streaming, and coast integration belong to future
  projects rather than the active executable.
- `projects/gltf_viewer` and `pbr_furnace` prove the reusable forward-PBR
  renderer, imported glTF material/animation/deformation paths, HDR or generated
  IBL, graph-owned shadow/HDR/post passes, and optional environment, terrain,
  and ocean composition.

Candidate follow-ups:

- Reconcile and consolidate render-graph contracts only where the now-broad
  consumer set repeats real setup or synchronization policy.
- Profile glTF startup and adopt staged asset preparation/installation if
  blocking asset loading is the next visible host/engine bottleneck.
- Reopen Planet surface/streaming work only as an explicitly separate product
  checkpoint with a concrete residency contract.
- Marching cubes for compute-generated geometry and indirect draw pressure.
- SDF sculpting if the sparse resource model becomes the more interesting
  framework driver.
- A larger particle system only if it grows beyond the current example-sized
  attractor demo.

Exit criteria:

- Active projects run interactively with a window.
- Active visual projects can run headlessly for a fixed number of frames and
  produce deterministic output artifacts where capture is part of their
  contract.
- README contains exact commands for local smoke testing. Status: complete for
  the current portfolio.

## Phase 6: Runtime Extraction

Status: complete for the current host/engine/config extraction checkpoint;
further extraction is consumer-driven.

Goal: extract only the host concepts that repeated windowed/headless project code
has proven useful.

- GLFW-backed window host outside `cubey::vulkan`. Status: complete.
- Initial windowed host outside `cubey::vulkan`. Status: complete for all active
  windowed examples and projects.
- Project lifecycle vocabulary: setup, update, render packet, resize, shutdown.
  Status: complete as a narrow concept and runtime-service boundary; no generic
  lifecycle host has been introduced.
- Project runtime services for jobs, uploads, captures, and `ProjectFrame`
  creation. Status: complete.
- Thin project runtime adapter for one project frame per host frame and context
  access. Status: complete.
- Strict GPU runtime exposed through windowed/headless host contexts, with
  setup-time GPU work entering through a queue and running on the runtime owner.
  Status: complete for current host contexts, project GPU services, ticketed
  readback, and deferred retirement.
- Narrow no-GLFW headless capture host that shares no-window instance/device,
  offscreen target, capture transitions, readback, PNG writing, and optional
  MP4 writing. Status: complete for current headless examples/projects.
- Project-owned config facades. Shared core keeps generic
  parse/template/override plumbing; every active executable owns its option
  groups, defaults, and validation. Status: Configuration V2 complete and the
  legacy global registry removed.
- Shared input, camera, ImGui control, performance, and config metadata helpers
  cover repeated host/project mechanics without becoming a generic editor.
- The minimal render graph and `ForwardPbrRenderer3D` provide the first reusable
  command/resource and renderer-policy boundaries earned by multiple projects.
- A generic project lifecycle host remains deferred; current projects still own
  setup, simulation, render intent, and shutdown policy.

Exit criteria:

- Cube examples remain short without hiding Vulkan synchronization, layout,
  descriptor, or resize constraints.
- `particle_cubes` proves the host can support update/input/compute-plus-graphics
  without becoming a particle system or renderer.
- Examples remain useful as small reference programs rather than becoming hidden
  framework tests.

## Deferred Until Concrete Pressure

- Timeline semaphores, split graphics/compute/present queues, parallel command
  recording, transient graph aliasing, and automatic async scheduling.
- Source-asset streaming, partial terrain/atlas residency, and general resource
  dependency graphs; whole-generation staged activation remains the current
  contract.
- Marching cubes, larger particle systems, or other compute-generated geometry
  only when a project needs indirect draw or geometry-lifetime pressure.
- SDF sculpting experiments from `projectR` if the resource model holds up.
- Browser or alternate-backend work only after a concrete project earns the
  extra shader and platform surface area.

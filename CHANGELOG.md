# Changelog

This file is the source of truth for release notes. Keep user-visible changes
under `Unreleased`; when a release is tagged, move the relevant entries into a
versioned section and use that section as the release notes.

## Unreleased

### Added

- C++20/CMake project bootstrap with presets, warning settings, formatting, and
  clang-tidy defaults.
- C++ style guide covering formatting, naming, Vulkan structure, and review
  priorities.
- Vulkan-first direction docs based on the WebGPU and Vulkan spikes.
- Threading and async design doc covering the async-ready runtime boundary,
  CPU job facade, Vulkan ownership rules, queued upload/capture direction, and
  deferred full multithreaded renderer work.
- Living roadmap and working-notes docs.
- MIT license, matching the original cubey branch.
- `cubey` static library target with public headers under `include/cubey/`.
- Public Vulkan `Instance` and `Device` types for validation/debug-utils
  setup, physical-device selection, logical-device ownership, and queue access.
- Public Vulkan `Swapchain`, `CommandPool`, and `FrameResources` components for
  swapchain image/view ownership, command-pool ownership, command-buffer
  allocation, and single-frame command/sync resources.
- Public Vulkan `ShaderModule` type and CMake GLSL-to-SPIR-V helper using
  `glslangValidator`.
- Public Vulkan `Buffer`, `Image`, `DepthAttachment`, `Sampler`, and
  `ImmediateCommands` components for device resources, generated texture paths,
  depth attachments, and setup uploads.
- Public Vulkan `CommandRecorder` helper for non-owning command-buffer
  recording: begin/end, dynamic rendering, image transitions, pipeline and
  descriptor binding, push constants, draws, indexed draws, and dispatches.
- Public Vulkan `SubmissionCoordinator` helper for serialized queue submission
  and monotonic GPU submission tickets.
- Public Vulkan `GpuRuntime` helper as the host-owned GPU work queue and
  owner-thread context. Windowed and headless hosts expose it through their
  contexts and run it threaded by default, with explicit inline mode for tests.
- Public Vulkan transfer/readback helpers for readback buffers, sampled image
  configs, buffer-image copies, and storage, transfer, sampled-image readback,
  color-attachment readback, and sampling image layout transitions.
- Public binary file I/O helpers and PNG image I/O helper backed by vendored
  `stb_image_write`.
- Public Vulkan pipeline and descriptor RAII types, plus descriptor write
  helpers, pipeline-layout create-info helpers, a compute pipeline create-info
  helper, and a dynamic graphics pipeline create-info helper for current color
  and depth-only dynamic-rendering paths.
- Public Vulkan descriptor set info and bundle helpers that own the repeated
  descriptor layout, pool, and single descriptor-set allocation path.
- Public Vulkan dynamic graphics pipeline blend controls and a storage-buffer
  descriptor write helper.
- Public GLM-backed `cubey::math` wrapper for matrix/vector types and the
  current Vulkan transform/projection conventions.
- Public Vulkan `RenderContext` component for explicit surface-backed
  begin/end frame lifecycle, acquire, command reset, submit, present, and
  swapchain-recreate signaling.
- Public Vulkan `SwapchainRecreateTracker` helper for guarding repeated
  out-of-date/suboptimal recreate loops.
- Public Vulkan image transition helpers and dynamic-rendering color/depth
  attachment setup helpers.
- GLSL shader helper support for shared include directories and dependency
  tracking.
- Public SPIR-V I/O helper for shader-backed examples and future projects.
- Public CPU job facade with a worker-backed `JobSystem`, deterministic
  `InlineExecutor`, and `JobHandle` result wrapper.
- Public PNG capture queue that moves completed RGBA pixels into job-backed
  encoding work and returns an explicit completion ticket.
- Public `HeadlessPngHost` for no-window Vulkan capture: instance/device setup,
  offscreen RGBA render target, capture transitions, readback, and PNG artifact
  writing behind project-provided render callbacks.
- Public upload request queue that owns submitted CPU bytes until the GPU owner
  drains them and tracks pending/completed/failed ticket status.
- Public frame ticket issuer and deferred destruction queue for retiring
  CPU-side cleanup actions after completed frame/submission points.
- Public async-ready project runtime vocabulary: project frames, extents,
  render packets, project context services, and `ProjectLike` concept checks.
- Composable typed configuration schemas with JSON file loading,
  target-specific template output, named CLI flags, deferred `--set`
  overrides, unknown-key rejection, and project-owned executable facades.
- Public `ProjectRuntimeServices` owner for project-facing jobs, uploads,
  captures, frame tickets, deferred destruction, and `ProjectFrame` creation
  from frame timing.
- Public `ProjectRuntimeAdapter` for the narrow host bridge: same-frame
  `ProjectFrame` reuse, new-frame ticket issue, project context access, and
  deferred destruction retirement.
- Public `ProjectGpuServices` bridge for project-facing upload draining,
  upload completion/failure status, owner-thread GPU work submission, and
  deferred destruction retirement from completed GPU submission tickets.
- Public frame timing, orbit-controller, and frame-stat types for interactive
  windowed examples.
- `examples/spinning_cube`, a shader-generated cube smoke executable with push
  constants, animation, dynamic rendering, device-local vertex/index buffers,
  and a shared depth attachment helper.
- `examples/textured_cube`, an interactive shaded cube that generates texture
  data through a setup-time compute shader and samples it in the graphics pass
  with descriptor-backed scene uniforms and dynamic rendering.
- `examples/shadow_cube`, a two-pass directional shadow-map example using a
  sampled depth texture, depth-only dynamic rendering, orthographic light view,
  and explicit sampled-depth layout transitions.
- `examples/headless_cube`, a no-window Vulkan smoke that renders an indexed
  cube into an offscreen color target, reads it back, and writes a PNG artifact.
- `examples/particle_cubes`, a compute-updated attractor particle smoke
  rendered as indexed cube instances.
- `projects/fluid/smoke_2d`, the first fluid project target: a 2D
  dye-and-velocity compute simulation with injection/advection passes,
  project-local pressure projection, pointer injection, pause/reset controls,
  debug render modes, fullscreen rendering, config tests, windowed smoke, and
  deterministic headless PNG smoke.
- `projects/fractal_2d`, a fullscreen Mandelbrot-style shader project with
  pan/zoom/reset navigation and headless PNG output.
- CTest smoke that accepts either successful window startup or the known
  no-display GLFW failure in terminal sessions.
- CTest smoke for headless PNG artifact creation and PNG signature validation.
- Shared CMake CTest smoke helpers for windowed and headless example targets.
- Optional `cubey_host` target with a GLFW window/surface host, pointer/key input
  dispatch, and the shared windowed host loop.
- Public `cubey::render` pass planning, sampled depth texture, and depth-only
  rendering-info helpers for multi-view/multipass examples.
- Public Vulkan queue submit, sampled-depth transition, sampler config, and
  depth-only dynamic graphics pipeline support for shadow-map style work.
- Public `cubey::render::ForwardScenePass3D`, `FrameUniformMaterialInstance`,
  `create_compute_generated_texture_2d`, and `InstanceBuffer` helpers for the
  current forward cube rendering path.
- `examples/instanced_cubes`, a real instance-rate cube grid example.
- `examples/material_cubes`, a per-packet material instance binding example.
- `projects/pbr_furnace`, a white-furnace PBR validation scene for
  roughness/metallic behavior under generated IBL.
- `projects/gltf_viewer`, a glTF/glb integration project with imported meshes,
  PBR materials, texture upload, rigid/morph/skinned animation, generated or
  HDR-backed IBL, skybox rendering, shadow maps, and headless capture.
- CPU-side glTF 2.0 asset loading through `cgltf`, including buffers, images,
  samplers, sparse accessors, morph target names, animations, skins, alpha
  modes, and common material extensions.
- `KHR_texture_basisu` KTX2 material texture import through Basis Universal,
  with BC7 upload when supported and RGBA8 fallback.
- Radiance HDR environment loading plus generated/HDR-backed PBR IBL resources
  with irradiance, prefiltered radiance, and DFG LUT data.
- Shared forward-PBR renderer service and shader package with shadow, skybox,
  HDR scene color, post/display transform, debug views, and render-graph-backed
  recording.
- Shared staged-resource lifecycle with typed CPU preparation/GPU installation,
  generation-safe frame-boundary activation, deterministic headless completion,
  and submission-ticket retirement, plus a bounded generated-artifact cache.
- Premultiplied-alpha blending policy for forward PBR alpha materials.
- Optional in-process H.264 MP4 capture for headless runs when libav/FFmpeg
  development packages are available at configure time.
- Shared atmosphere/celestial environment state with procedural clear sky,
  dynamic time of day, exposure, moon, stars, Milky Way, environment lighting,
  and reusable project controls.
- Shared surface-cloud runtime with generated weather/noise fields, projected
  receiver shadows, reflected planar cloud views, and a coherent filtered cloud
  environment cache.
- Shared cloud-environment lifecycle and forward-PBR generation blending, with
  clouded procedural reflections validated in the glTF viewer.
- `projects/ocean`, a horizon-scale spectral ocean with configurable cascade
  workload, Calm/Windy/Stormy presets, persistent whitecaps, clipmap LOD,
  curved-local surface mapping, shared environment lighting, and deterministic
  visual review tooling.
- Shared ocean-surface model/runtime ownership so forward-PBR consumers can
  compose the accepted ocean product without taking ownership of its FFT and
  presentation implementation.
- `projects/terrain`, an external-heightfield far-backdrop product with
  validated source/placement, cached CPU products, continuous fixed-sector
  geometry, shared environment lighting, runtime replacement, and headless
  review coverage.
- Shared raster-backdrop preparation, placement/clearance contracts, and runtime
  composition used by glTF Viewer, Water 3D, and the Pyro 3D products.
- `projects/planet`, an orbital-only Earth-like globe with cached deterministic
  surface fields, phase presets, shared sky/celestial composition, project-owned
  typed configuration, and headless review coverage.
- `projects/fluid/water_2d`, `water_3d`, `fire_3d`, and `explosion_3d`, covering
  APIC/PIC-FLIP liquids and shared dense volumetric pyro with windowed controls,
  diagnostics, and deterministic headless capture.

### Changed

- Cubey 2.0 is framed as a ground-up native Vulkan workbench rather than an
  OpenGL continuation or WebGPU-first rewrite.
- Runnable targets are explicit examples/projects rather than a generic `cubey`
  executable.
- Windowed examples now use dynamic rendering instead of classic render
  passes/framebuffers while keeping command recording and render-resource policy
  local.
- Windowed examples, `headless_cube`, and `smoke_2d` now use the shared
  Vulkan command recorder for repeated command-buffer calls while keeping pass
  order, barriers, descriptors, pipelines, and render policy local.
- Frame submission routes through the Vulkan submission coordinator, and
  host-visible setup/capture GPU work now routes through `GpuRuntime` before
  reaching immediate commands. `GpuRuntime` now defaults to a threaded GPU owner
  while keeping inline execution explicit for deterministic tests and narrow
  bring-up paths.
- Graphics examples now share dynamic graphics pipeline create-info setup while
  retaining explicit example-local layout, shader, vertex-input, and descriptor
  choices.
- Cube examples now share device-local buffer upload and depth attachment setup
  helpers instead of carrying local staging-copy and depth-image code.
- `textured_cube` now shares descriptor layout/pool/write helpers plus compute
  pipeline and pipeline-layout create-info helpers instead of carrying raw
  descriptor and compute setup blocks.
- `textured_cube` and `particle_cubes` now use the descriptor set bundle helper
  instead of carrying separate descriptor layout, pool, and allocation members.
- `textured_cube` now uses shared storage-image transition and generated
  sampled-image config helpers for its compute-generated texture.
- `spinning_cube` and `textured_cube` now use shared Cubey math helpers instead
  of carrying duplicate local matrix code.
- Windowed examples now share GLFW windowing, Vulkan surface creation,
  acquire/present behavior, resize handling, frame timing, and swapchain
  recreation through `cubey_host`.
- Shader-backed examples now share SPIR-V I/O layered on generic binary file
  reads instead of carrying local file readers.
- Example CTest targets now use shared CMake smoke helpers instead of repeated
  shell snippets.
- Headless PNG smoke tests now apply a narrow LeakSanitizer suppression for
  DBus allocations left alive by the Vulkan loader/driver path on Linux.
- Roadmap and Vulkan abstraction docs now frame the host/engine layer as the
  standard windowed/headless path while keeping project lifecycle and render
  intent explicit.
- The particle rewrite is currently categorized as a cube-first example-sized
  reference program rather than a first-class `projects/` target.
- `spinning_cube`, `textured_cube`, `shadow_cube`, `instanced_cubes`,
  `material_cubes`, and `particle_cubes` now use the shared GLFW/windowed host
  layer while keeping command recording sequence and render resources
  example-local.
- `headless_cube` and every active visual project now use the shared no-GLFW
  PNG/MP4 host while keeping resource setup, simulation, and command recording
  local; projects with per-frame renderer resources size them from the active
  capture frame-slot count.
- `smoke_2d` simulation steps now consume `ProjectFrame` values from shared
  project runtime adapter in both windowed and headless modes, while keeping
  Vulkan command recording sequence and resource policy project-local.
- `Camera3D` now supports orthographic projection in addition to perspective
  projection.
- Cube examples now share forward pass, generated texture, frame-uniform
  material, and example-local cube scene helpers where appropriate.
- Public examples are now intentionally cube-focused: the old clear-only and
  triangle-only app targets were removed, the headless smoke became
  `headless_cube`, the particle smoke became `particle_cubes`, and the
  fullscreen fractal moved to `projects/fractal_2d`.
- Core tests now register test cases through domain-specific registry files
  rather than a single monolithic test registry translation unit.
- Transform manager public headers now expose the component-manager contract
  while the instantiated implementation lives under `src/cubey/scene`.
- `shadow_cube` is split into example-local lifecycle, resource, scene, render
  graph, and shadow-pass helper files so the example app stays orchestration
  focused without promoting shadow policy into the Cubey library.
- Headless capture can now produce either one PNG frame or a deterministic MP4
  sequence; video capture uses a small in-flight render/readback slot ring and
  CPU encoding on a worker thread.
- Surface clouds are hosted by `projects/atmosphere`; the dedicated cloud
  reference project is a comparison lane rather than a competing production
  owner, and superseded cloud viewers have been retired.
- Ocean cloud lighting now uses a planar reflected-camera product with cached
  environment fallback. Current-view/hybrid reflections, spectral-moment
  handoff experiments, synthetic far normals, and filtered far-whitecaps were
  removed after review.
- Ocean controls are grouped by Waves, Surface, Lighting, Scale & LOD,
  Environment, and Diagnostics, with shared atmosphere/cloud controls exposed
  through their foundation components.
- Render-graph declaration, per-frame transient ownership, resource resolution,
  and graph-derived declared-boundary synchronization are now used by the
  forward-PBR renderer and atmosphere, cloud, ocean, terrain, and fluid
  projects without introducing scheduling or automatic descriptor policy.
- Every active executable now composes the common host schema with only its
  project-owned typed options; JSON/CLI/template behavior no longer crosses a
  global option registry or compatibility conversion.

### Removed

- The legacy global `RunConfig`, 302-option descriptor registry, parser/runner,
  compatibility adapters, sentinel defaults, and `lazy-serializable`
  dependency.
- Superseded runnable viewers under `projects/cloud_ref_2`,
  `projects/clouds_legacy`, and `projects/planet_legacy`; their architecture
  lessons remain documented and their source is recoverable from Git history.
- Terrain reference, ShaderToy, and hydrology viewer layers; their useful
  algorithms, generators, exports, and tests remain as opt-in studies.

## Pre-2.0 History

- The original cubey codebase remains preserved on the `legacy` branch as an
  OpenGL 4 shader playground.

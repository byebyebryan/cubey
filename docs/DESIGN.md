# Cubey 2.0 — Design Document

## What It Is

A minimal C++ library and workbench for GPU-driven procedural graphics
experiments. Not a game engine, not a polished external SDK - a personal
workbench for trying graphics ideas quickly while keeping the reusable runtime
small and explicit.

## Background

Cubey 2.0 is a ground-up native Vulkan rewrite of the original C++/OpenGL/GPGPU
Cubey experiments. Historical spike notes and migration context live under
`docs/archive/` and `docs/notes/`; this document should stay focused on the
current design.

## Guiding Principles

- **Minimal library, maximum project.** The C++ runtime exists to get out of the way. The interesting work happens in shaders and compute.
- **Primary target: desktop with full GPU power.** No compromises for portability. Native Vulkan is the foundation.
- **Headless rendering is first-class.** Every project should eventually render to image without a window. Enables automated testing and AI-assisted development.
- **Shaders compile at build time.** No runtime hot-reload complexity. GLSL → SPIR-V via glslangValidator at build time.
- **Async-ready before heavily threaded.** Shape project code around jobs,
  queued uploads, queued captures, and explicit GPU ownership before adding a
  dedicated render thread or parallel command recording.
- **Intentional foundation, not accidental extraction.** Rendering engines are
  inherently contract-heavy. Cubey should design small shared library
  boundaries for durable graphics/runtime concepts early, then revise them as
  projects sharpen the requirements.
- **Established graphics vocabulary first.** Prefer industry terms and proven
  API shapes over invented names. Before adding major concepts, compare
  Filament, Godot, Unity, Unreal, Khronos/Vulkan guidance, and relevant papers
  or engine notes.

## Technology Stack

| Layer | Choice | Rationale |
|-------|--------|-----------|
| Language | C++20 | Concepts, ranges, std::format, std::span |
| Build | CMake + Ninja | Cross-platform, fast incremental builds |
| Primary GPU API | Vulkan | Full GPU control, async compute, no feature ceiling |
| Windowing | GLFW | Minimal, Vulkan-native surface creation |
| UI | None yet; ImGui is the likely debug UI | Current telemetry stays lightweight until UI earns the dependency |
| Math | GLM behind `cubey::math` | Share matrix/vector types, transform/camera state, and Vulkan projection conventions without exposing ad hoc example math |
| Shader compilation | glslangValidator (build time) | GLSL → SPIR-V, no runtime dependency |
| Image output | `stb_image_write` | Single-header dependency, enough for inspectable artifacts |
| CPU async work | undecided behind `cubey::jobs` | Taskflow and `BS::thread_pool` are the first candidates |

## Architecture

```
                    +---------------------+
                    | Projects / Examples |  <-- fluid sim, SDF sculpt, particles, etc.
                    +----------+----------+
                               |
                    +----------v----------+
                    | Cubey Foundation    |  <-- frame/input/resource helpers now;
                    |                     |      host/window/UI layers stay narrow
                    +----------+----------+
                               |
                    +----------v----------+
                    |    Vulkan Layer      |  <-- device, swapchain, resources, pipelines,
                    |  (primary path)     |      dynamic rendering, frame state
                    +---------------------+
```

Cubey should not build a broad backend-agnostic GPU API. The implementation
should make Vulkan livable through small native modules while keeping the
constraints that affect correctness visible.

### Vulkan Layer Seams

The near-term Vulkan layer should split around real ownership boundaries:

- `Device` — instance, physical device, logical device, queue selection, validation
- `Surface` / `Swapchain` — platform surface handoff, surface extent ownership,
  image views, present-mode constraints, and resize/out-of-date recreation
- `Buffer` / `Image` — allocation, views, staging uploads, readback
- `Shader` / `Pipeline` — build-time shader paths, compute and graphics pipelines
- `CommandPool` / `FrameResources` — command pool ownership, command-buffer
  allocation, sync objects, and per-frame-slot submitted GPU tickets
- `SubmissionCoordinator` — serialized queue submission helper and monotonic
  GPU submission tickets
- `GpuRuntime` — host-owned GPU work queue and owner-thread context; threaded
  by default, with an explicit inline mode for deterministic tests and narrow
  bring-up paths
- project/pass code — the actual procedural experiments

These seams should be practical C++ modules first. Portability work should be
driven by a concrete project need rather than symmetry.

### Resource Vocabulary

Projects and examples should eventually interact with a small set of rendering
concepts:

- `Buffer` — GPU buffer (vertex, storage, uniform, index, indirect)
- `Image` / future `Texture2D`, `Texture3D` — image data
- `Pipeline` — compute or graphics pipeline
- `DescriptorSet` — Vulkan resource binding set

Core operations: create resources, dispatch compute, draw, synchronize, submit,
present, and read back. In the first version, those operations map directly to
Vulkan and remain free to expose Vulkan-specific requirements where useful.

### Transform, Camera, And Renderable Vocabulary

Reusable spatial types should stay explicit and narrow:

- `Transform2D` is translation, scalar-radian rotation, scale, and a `Mat3`
  affine matrix.
- `Transform3D` is translation, quaternion rotation, scale, and a `Mat4` affine
  matrix.
- `TransformManager2D` and `TransformManager3D` are entity-backed component
  managers for parented local/world affine transforms. They publish read-view
  snapshots through `Scene` commits and are the first scene/component managers,
  not a full scene graph.
- `Camera2D` and `Camera3D` own view/projection state separately from transform
  component ownership. `Camera3D` supports perspective and orthographic
  projection modes so normal scene cameras and light/shadow views share the
  same camera contract. `OrbitCameraState` and `OrbitController` are control
  helpers layered on top of `Camera3D`, not camera primitives.
- `RenderableManager3D` owns entity-backed 3D renderable components and builds
  CPU-side renderable packets from committed scene read views. Renderables use
  registry-issued opaque mesh/material handles so scene snapshots do not expose
  mutable Vulkan resources.
- `LightManager3D` owns entity-backed 3D light components and builds CPU-side
  light packets from committed scene read views. Directional lights carry
  normalized directions; point lights derive world position from `Transform3D`.
- Scene render-planning helpers expose the first renderer-facing 3D view and
  pass-list contracts. They combine a scene read view, camera entity, viewport
  size, ambient environment, renderables, and lights into CPU
  `RenderFramePlan3D` values.
- The planning helpers can describe manual multi-view/multi-pass work such as a
  shadow pass plus a camera pass while leaving scheduling and synchronization
  explicit.
- The scene layer can turn renderable packets into CPU draw packets by
  validating resource handles, attaching material tags, sorting them, computing
  world bounds, and frustum-culling visible renderables. This is a planning
  layer only; material pass metadata can describe pass participation,
  descriptor layout shape, push constants, and reusable pipeline state, but
  Vulkan command recording, concrete descriptors, pipelines, light upload, and
  shader/material binding stay owned by examples/projects for now.

The broader entity/component shape is captured in the
[entity and component foundation](architecture/entity-component-foundation.md):
Cubey should move toward manager-oriented, MT-stable component storage with
explicit read views and edit commits before adding broader material,
environment, or renderer ownership.

`cubey::Engine` is the scoped root owner for engine services and scene
creation. It owns project runtime services, render resource handle identity,
material metadata, and created scenes. It is intentionally not a singleton:
applications pass `Engine&`, `Scene&`, or narrower contexts through the
boundaries that need them.

### Host And Project Interfaces

Cubey now has two narrow hosts:

- `cubey_host` owns GLFW window/surface hosting and the shared windowed loop.
- `cubey::host::HeadlessPngHost` owns no-window Vulkan setup, an offscreen RGBA
  target, capture transitions, ticketed RGBA8 readback, and PNG artifact
  writing.
- `cubey::Engine` owns project runtime services and created scenes. Windowed
  and headless hosts still own platform and Vulkan setup until a later renderer
  ownership pass. Engine-created scenes validate renderable mesh/material
  handles against the Engine-owned render resource registry. Examples/projects
  still own concrete GPU resources behind those handles through local resource
  tables.

Examples and projects still own their shaders, simulation choices, and render
intent. The library should own durable foundation contracts when those
contracts are clearer and safer than ad hoc project code. Repetition is useful
evidence, but it is not a required gate for graphics concepts that are already
well-established or correctness-sensitive.

Before this becomes a broad host, projects should move toward the async-ready
shape described in [threading and async design](architecture/threading-and-async.md):
project/application state produces render packets, CPU jobs run behind Cubey APIs,
upload/capture requests are queued, and one GPU owner serializes queue
submission and resource lifetime decisions. Today that GPU owner is
`cubey::vulkan::GpuRuntime`, backed by `SubmissionCoordinator` for actual queue
submission and threaded by default in hosts. `cubey::ProjectGpuServices` is the
project-facing bridge for queued uploads, upload completion status, RGBA8 image
readback tickets, owner-thread queue-idle waits, and deferred GPU retirement
without handing project code the raw submission coordinator.

```cpp
struct App {
    virtual void setup() = 0;
    virtual void update(float dt) = 0;
    virtual void render() = 0;
    virtual void ui() = 0;  // ImGui panel
    virtual void on_key(...) {}   // optional
    virtual void on_mouse(...) {} // optional
};
```

Entry point:
```cpp
int main() {
    return cubey::run<MyProject>(Config{.title = "fluid sim", .width = 1280, .height = 720});
}
```

## Testing & Feedback Loop

Every non-trivial project should eventually support two modes:

**Interactive:** Opens a window, renders at 60fps, ImGui controls for parameters.

**Headless:**
```bash
./fluid_sim --headless --frames 60 --width 1280 --height 720 --output result.png
```

Headless mode enables:
- Automated validation (Vulkan validation layers, exit code, numerical invariants)
- Image output for visual verification
- Compute data readback for numerical checks (mass conservation, NaN detection, bounds)
- Golden image comparison for regression testing

This is critical for AI-assisted development — the agent gets structured pass/fail feedback without needing to "see" the output.

## Shader Strategy

- **Desktop (Vulkan):** GLSL → SPIR-V at build time via glslangValidator
- Shared shader includes (lighting, noise functions, math utilities) in a common directory

## Projected Projects

| Project | Source | Notes |
|------|--------|-------|
| Fluid 2D | cubey1 rewrite warmup | First project target; compute-updated 2D dye/velocity field with headless PNG output |
| Fluid Simulation 3D | cubey1 rewrite | Future Eulerian 3D fluid sim, compute-based, raymarched volume rendering |
| Particle System | cubey1 rewrite | Prototype attractor particles now live under `examples/particles`; a larger project would need a clear compute + indirect draw contract before graduating |
| Marching Cubes | cubey1 rewrite | Isosurface extraction via compute + indirect draw |
| Fractal 2D | cubey1 rewrite | Mandelbrot/Julia renderer |
| SDF Sculpting | projectR port | Sparse SDF brick tree, raymarched rendering, Morton-coded spatial indexing |

## Reference Sources

Graphics is specialized enough that Cubey should actively check precedent
before inventing vocabulary or architecture. Preferred references:

- [Filament](https://github.com/google/filament) and its
  [public docs](https://google.github.io/filament/): primary practical
  reference for engine/resource/view/material boundaries, explicit handles,
  frame flow, and modern PBR terminology.
- [Godot documentation](https://docs.godotengine.org/en/stable/): open-source
  reference for 2D/3D transforms, scene/rendering-server boundaries, resources,
  and editor-independent runtime concepts.
- [Unity Manual](https://docs.unity.cn/Manual/) and
  [Unreal Engine documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine):
  public API references for established contracts such as transforms,
  components, cameras, renderers, materials, scenes, and assets.
- [Khronos Vulkan documentation](https://docs.vulkan.org/): source of truth for
  Vulkan names, synchronization, layouts, descriptor terminology, and valid
  usage.
- Graphics papers, GPUOpen/NVIDIA/Intel notes, and mature OSS renderers when a
  topic is more specific than the engine-level references above.

Use these sources for terminology and contract shape. Do not copy code, and do
not import large engine architecture wholesale when Cubey only needs a narrow
foundation type. For nontrivial foundation work, record the main precedents
consulted in the design note, roadmap update, PR description, or working notes.

## Borrowing from Filament

Where applicable, borrow architectural patterns (not code) from Google's Filament:
- Handle-based resource management (`Handle<T>` — typed IDs, not raw pointers)
- Ring buffer / N-frames-in-flight without stalling
- Uniform arena / blob allocator for batching uniforms
- Sampler caching to deduplicate identical VkSampler objects

Filament is a strong reference, but not a template to clone. Cubey does not
currently need Filament's full backend/frontend split or material system.
Borrow the contract clarity and terminology; keep Cubey's implementation small.

## Repository Structure

Cubey is becoming a small C++ monorepo. The public foundation is modeled as
layered `cubey::*` targets (`core`, `vulkan`, `render`, `scene`, `engine`, and
optional `host`) plus the aggregate `cubey::cubey` target for examples and
projects that do not need a narrower dependency. Runnable binaries should be
named explicitly and live in either `examples/` or `projects/`:

- `include/cubey/` - public library headers. These define the include discipline
  used by examples, projects, and tests.
- `src/cubey/` - library implementation and private headers.
- `cmake/` - shared CMake helpers for shaders, warnings, and CTest smoke
  targets.
- `examples/` - small, focused reference programs that prove one concept or API
  path. Current examples are `window_clear`, `triangle`, `spinning_cube`,
  `textured_cube`, `shadow_cube`, `headless_render`, `fractal`, and
  `particles`.
- `projects/` - first-class graphics experiments and longer-lived creative
  work, starting with `fluid_2d` and later candidates such as `fluid_sim`,
  `marching_cubes`, and `sdf_sculpt`.
- `third_party/` - small vendored dependencies with explicit license notes.
- `tools/` - repo utilities, asset processors, shader tools, or diagnostics.
- `tests/` - unit and integration tests.
- `benchmarks/` - performance targets once there is something meaningful to
  measure.

CMake should model this as explicit targets, not source-folder convention. The
layering order is:

```
cubey::core
  -> cubey::vulkan
  -> cubey::render
  -> cubey::scene
  -> cubey::engine
  -> cubey::cubey aggregate
        + cubey::input
        + optional cubey::host for concrete hosts
  ^
  |
examples / projects / tools / tests / benchmarks
```

Projects can depend on the aggregate `cubey::cubey` target or a narrower
`cubey::*` layer; Cubey library targets must not depend on projects. Shared
code either graduates into the appropriate Cubey layer or stays local to the
project that needs it. Example-specific host behavior should stay in that
example. The Cubey library should contain reusable engine/host pieces, not named
examples such as `window_clear`.

Cubey targets should expose public headers now, but they should not gain
install/export/package rules until the project genuinely needs external
consumption and versioning.

```
cubey/
  CMakeLists.txt
  CHANGELOG.md             -- release-note source
  LICENSE                  -- MIT license
  cmake/
    CubeyShaders.cmake     -- GLSL to SPIR-V build helper
    CubeySmokeTests.cmake  -- shared CTest smoke helper definitions
    CubeyWarnings.cmake    -- compiler warning helper
  include/
    cubey/
      core/
        run_config.h       -- shared run configuration
        jobs.h             -- CPU job facade
        frame_clock.h      -- frame timing
        file_io.h          -- generic binary file reads/writes
        image_io.h         -- PNG artifact output
        math.h             -- GLM-backed math aliases and Vulkan projection helpers
      input/
        input.h            -- shared keyboard and mouse input snapshot
        pointer_drag.h     -- shared pointer drag helper
        pan_zoom_2d_controller.h -- input-driven 2D camera controller
        orbit_controller.h -- basic orbit input state
      engine/
        capture_queue.h    -- job-backed PNG capture encoding queue
        engine.h           -- scoped root owner for runtime, scenes, and registries
        project_gpu_services.h -- project-facing GPU uploads/readbacks/retirement
        project_runtime.h  -- async-ready project vocabulary
        upload_queue.h     -- CPU-owned upload request queue
      host/
        frame_stats.h      -- windowed telemetry sampling and title formatting
        glfw_window.h      -- GLFW window and surface host
        headless_png_host.h -- no-window offscreen PNG capture host
        windowed_host.h    -- shared windowed host loop
      render/
        material.h         -- material metadata and material/pass contracts
        target.h           -- color/depth render target views and pass recording helpers
        mesh.h             -- indexed mesh buffers and draw item vocabulary
        pipeline_resource.h -- shader modules and graphics/compute pipeline resources
        primitive_mesh.h   -- CPU primitive mesh data and vertex input layouts
        texture.h          -- sampled color/depth texture ownership helpers
        resource_handle.h  -- opaque render resource handle values
        resource_registry.h -- render handle identity and material tags
        resource_table.h   -- project-owned move-only render resources
      scene/
        entity.h           -- generational entity handles and stable manager
        scene.h            -- scene edit/read-view contract
        camera_*.h         -- 2D/3D cameras and entity-backed camera managers
        render_plan.h      -- scene-backed CPU 3D draw-packet planning
        view_3d.h          -- scene-backed CPU 3D view/pass planning and culling
        stable_slot_store.h -- MT-stable scene/component slot storage
        single_instance_component_store.h -- one-component-per-entity storage
        transform_*.h      -- explicit 2D/3D affine transform value types
        transform_manager.h -- parented transform components and snapshots
        light_manager.h    -- entity-backed 3D light components
        renderable_manager.h -- entity-backed 3D renderable components
      vulkan/
        buffer.h           -- Vulkan buffer and memory ownership
        command_pool.h     -- command pool ownership and command-buffer begin
        command_recorder.h -- non-owning command-buffer recording helper
        descriptors.h      -- descriptor set layout/pool ownership and write batching
        device.h           -- physical/logical device and queue ownership
        dynamic_rendering.h -- dynamic-rendering attachment helpers
        frame_resources.h  -- per-frame command/sync resources
        gpu_runtime.h      -- queued GPU work and owner-thread context
        image.h            -- Vulkan image, memory, and image-view ownership
        image_transitions.h -- image layout transitions and barriers
        immediate_commands.h -- one-shot setup command submission
        instance.h         -- instance, validation, debug messenger
        pipeline.h         -- pipeline ownership and graphics setup helpers
        queue_submit.h     -- binary-semaphore queue-submit description
        render_context.h   -- surface-backed begin/end frame lifecycle
        sampler.h          -- Vulkan sampler ownership
        shader_bytecode.h  -- SPIR-V bytecode file loading
        shader_module.h    -- shader module lifetime
        submission_tickets.h -- monotonic GPU submission tickets and retirement
        submission_coordinator.h -- serialized GPU submission helper
        swapchain.h        -- swapchain images and image views
        vk_check.h         -- Vulkan result helpers
  src/
    cubey/
      CMakeLists.txt       -- layered cubey::* library targets
      core/                -- run config, jobs, frame timing, I/O, and PNG writer
      engine/              -- engine root, project runtime, queues, GPU services
      host/                -- concrete GLFW/windowed/headless hosts
      input/               -- input snapshot and camera-control helpers
      render/              -- renderer-facing CPU resource and target helpers
      scene/               -- scene/entity, planning helpers, and component managers
      vulkan/
        *.cpp              -- Vulkan object, command, submission, and swapchain helpers
  examples/
    window_clear/          -- host smoke target for clear/present
    triangle/              -- minimal shader-backed graphics pipeline path
    spinning_cube/         -- indexed cube, push constants, depth
    textured_cube/         -- compute texture generation, uniforms, sampling
    shadow_cube/           -- graph-declared shadow/scene/present example
    headless_render/       -- minimal offscreen image path
    fractal/               -- fullscreen fractal shader smoke and headless PNG
    particles/             -- compute-updated attractor particles
  projects/
      fluid_2d/
        CMakeLists.txt
        main.cpp
        fluid_2d_app.*     -- host/engine/input orchestration
        fluid_2d_commands.* -- simulation and fullscreen draw command recording
        fluid_2d_gpu_resources.* -- project-owned GPU buffers/descriptors/pipelines
        shaders/
          fluid_2d_inject.comp
          fluid_2d_advect.comp
          fluid_2d_divergence.comp
          fluid_2d_pressure.comp
          fluid_2d_projection.comp
          fluid_2d.vert
          fluid_2d_render.frag
      fluid_25d/
  tools/
  tests/
  benchmarks/
  shaders/                 -- shared GLSL includes (lighting, noise, math)
  assets/                  -- textures, meshes
  third_party/             -- vendored single-header dependencies and notices
  docs/
    README.md              -- docs index and taxonomy
    DESIGN.md              -- current design and tenets
    roadmap.md             -- living implementation plan
    architecture/          -- detailed current foundation notes
      host-engine.md
      entity-component-foundation.md
      fluid-simulation.md
      renderer-foundation.md
      threading-and-async.md
      vulkan-abstractions.md
    cpp-style.md           -- C++ naming, formatting, and review conventions
    notes/                 -- scratch context and working notes
    archive/               -- historical decisions and superseded context
```

## What We're Not Building

- Another SDL / raylib / shadertoy
- A game engine
- A framework others build on
- A broad cross-platform compatibility layer
- Runtime shader hot-reload
- A full material/render-graph/pipeline system before its contract is clear

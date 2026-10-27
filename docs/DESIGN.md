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
| UI | ImGui for project debug panels | Live controls stay project-focused; Cubey is not an editor |
| Math | GLM behind `cubey::math` | Share matrix/vector types, transform/camera state, and Vulkan projection conventions without exposing ad hoc example math |
| Shader compilation | glslangValidator (build time) | GLSL → SPIR-V, no runtime dependency |
| Image output | `stb_image_write` | Single-header dependency, enough for inspectable artifacts |
| Asset import | `cgltf` + `stb_image` + Basis Universal transcoder | Narrow glTF/glb CPU loading for meshes, materials, textures, animation/deformation data, PNG/JPEG decode, and `KHR_texture_basisu` KTX2 upload support before a broader asset pipeline |
| CPU async work | Cubey-owned `std::jthread` worker pool behind `cubey::jobs` | Typed job results, an inline executor for deterministic work, and explicit drain-and-join shutdown |

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
- `Image` / `Texture2D` / `TextureCube` — image and sampled texture data
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
  matrix. It also supports an explicit affine matrix override for imported
  authoring data that cannot be represented faithfully as Cubey TRS.
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
- `cubey::asset` owns CPU-side imported asset data. The current slice supports
  glTF/glb meshes, nodes, matrix-authored node transforms, images, samplers, PBR
  materials, KTX2 BasisU image payloads, one animation/deformation data path,
  and standalone Radiance HDR image decode for IBL inputs; it does not create
  scenes, render handles, Vulkan resources, or a material system.
- `cubey::engine` owns the current glTF scene importer and the renderer service.
  The importer maps CPU asset nodes into scene entities, registry-issued render
  handles, app-owned mesh/material resources, uploaded textures, deformation
  resources, animation update hooks, and imported bounds. `RendererService`
  owns renderer instance lifetime, and `ForwardPbrRenderer3D` owns the repeated
  shadow/skybox/PBR forward pass resources, HDR scene-color target, post pass,
  render-graph recording, and shared forward-PBR shader package wiring.
  Per-frame renderer inputs are submitted through one
  `ForwardPbrRenderer3DRenderRequest`: target and view state are nested under
  `.target` and `.view`, while mesh/material/deformation resources and renderer
  settings remain explicit. Asset loading, environment choice, and view setup
  stay project-owned.
- `cubey::render::ForwardScenePass3D` remains a lower-level/simple pass helper.
  It is useful when an example or validation project needs direct control over
  pass recording; reusable PBR renderer policy belongs in
  `cubey::ForwardPbrRenderer3D`. Cube examples may use local helpers
  under `examples/common/` to remove repeated pass setup, but that glue stays
  outside the library until it proves to be real renderer policy.
- `cubey::render` owns the current generated and HDR equirectangular PBR IBL
  environment helpers for irradiance cube, GGX-prefiltered cube, and DFG LUT
  resources.
  Shared PBR shader helpers follow the established metallic-roughness remap:
  base color becomes diffuse color for non-metals and F0 for metals, while
  dielectric F0 comes directly from glTF IOR plus glTF specular controls. The
  PBR post contract carries final display transform
  controls for exposure, tone mapping, and target encoding.
  Prefiltered KTX/KTX2 environment import and offline filtering remain future
  asset-pipeline work.
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
  descriptor layout shape, push constants, and reusable pipeline state. The
  render layer now owns small material-instance descriptor lifetimes and shared
  draw-recording helpers. The engine-owned renderer service owns renderer
  instance lifetime, and the first forward PBR renderer owns reusable shadow,
  skybox, and PBR forward pass resources behind a request-shaped public
  contract. Asset loading, shader package selection, environment selection, and
  broader material policy stay owned by projects for now.

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
- `cubey::host::HeadlessCaptureHost` owns no-window Vulkan setup, an offscreen
  RGBA target, deterministic capture-frame timing, capture transitions,
  ticketed RGBA8 readback, queued PNG artifact writing, and optional MP4 video
  capture through the shared queued video encoder. `HeadlessPngHost` remains a
  compatibility alias for older call sites.
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
readback tickets, and owner-thread queue-idle waits without handing project
code the raw submission coordinator. Actual submission completion and deferred
GPU retirement remain owned by `GpuRuntime`, so project timing cannot be
mistaken for GPU progress.

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

<a id="projected-projects"></a>

## Project Origins and Current Status

Several entries began as plans for the OpenGL/Cubey 1 rewrite. Their origins
remain part of the project history; the status below describes the current
native repository:

| Project | Origin | Current status |
|------|--------|----------------|
| Smoke 2D | cubey1 rewrite warmup | Native fluid application with headless capture. |
| Fire 3D / Explosion 3D | cubey1 rewrite | Native front ends over the shared dense 3D pyro solver. |
| Particle System | cubey1 rewrite | Attractor prototype remains in `examples/particle_cubes`; a larger project awaits a concrete compute and indirect-draw contract. |
| Marching Cubes | cubey1 rewrite | Proposed isosurface extraction project; not a current application. |
| Fractal 2D | cubey1 rewrite | Native project remains under `projects/fractal_2d`; its public-site presentation is deferred, not retired. |
| SDF | projectR port | Native project under `projects/sdf` with procedural CSG, authored scenes, saved construction history and a material workshop. It is not a live desktop sculpting editor; freehand and incremental field editing remain deferred. |

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

Cubey is a small C++ monorepo. Its explicit `cubey::*` CMake libraries and
public include boundaries define ownership; a directory tree is not a second
source registry. The [project guides](README.md#project-docs) index the
application inventory, and [project layout](architecture/project-layout.md)
distinguishes supported front doors, tools, tests and retained studies.

| Location | Responsibility |
| --- | --- |
| [`include/cubey/`](../include/cubey) | Public foundation contracts, checked as standalone headers. |
| [`src/cubey/`](../src/cubey) | Library implementations and private modules. |
| [`shaders/cubey/`](../shaders/cubey) | Shared shader packages owned by their rendering domains. |
| [`examples/`](../examples) | Small cube-focused proofs of foundation contracts. |
| [`projects/`](../projects) | Focused applications with local scene, simulation, configuration and presentation policy. |
| [`studies/`](../studies/README.md) | Indexed experiments, frozen references and paused applications, explicitly opt-in where runnable. |
| [`tests/`](../tests) | Core contracts and repository-tool regression fixtures; project/domain tests also live with their owners. |
| [`cmake/`](../cmake) | Application, shader, smoke, library-policy and repository-test helpers. |
| [`tools/`](../tools/README.md) | Maintained repository checks and local report browser. |
| [`third_party/`](../third_party) | Small vendored dependencies with license notes. |
| [`docs/`](README.md) | Current guides and architecture, indexed notes, evidence and explicit archives. |

The library graph is declared in [`src/cubey/CMakeLists.txt`](../src/cubey/CMakeLists.txt):
`core`, `asset`, `vulkan`, `render`, `scene` and `engine`, plus input and optional
concrete host libraries. `cubey::cubey` is the convenience aggregate for
applications that do not need narrower dependencies. The configured dependency
guard checks actual direct/interface links and wrappers; the
[Vulkan map](architecture/vulkan-abstractions.md) explains the intended boundaries.
Libraries must not depend on projects.

Shared code either graduates into its owning layer or stays local to its
consumer. For example, the shared [Ocean surface runtime](architecture/ocean-rendering.md)
owns its GPU resources and shaders; the Ocean app owns review controls and scene
orchestration. [Fluid](../projects/fluid/README.md) keeps numerical and application
modules with the simulation domain; Terrain Water has its own frontend and
external-worker boundary. These ownership contracts matter more than a manually
copied list of filenames.

CMake records the actual source and target inventories. Use the configured
build graph, [foundation checks](../tools/README.md) and local project guides
when adding modules. Install/export/package rules remain deferred until external
consumption and versioning supply a concrete need.

## What We're Not Building

- Another SDL / raylib / shadertoy
- A game engine
- A framework others build on
- A broad cross-platform compatibility layer
- Runtime shader hot-reload
- A full material/render-graph/pipeline system before its contract is clear

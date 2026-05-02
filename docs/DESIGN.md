# Cubey 2.0 — Design Document

## What It Is

A minimal C++ library and workbench for GPU-driven procedural graphics
experiments. Not a game engine, not a polished external SDK - a personal
workbench for trying graphics ideas quickly while keeping the reusable runtime
small and explicit.

## Origin

The original cubey (2015) was a learning project for C++/OpenGL/GPGPU. It featured 5 working demos: fluid simulation, particles, marching cubes, fractals, and a camera test with shadows. The fluid sim demo gained traction on YouTube/GitHub (~28 stars, ~5 forks).

This is a ground-up rewrite carrying forward the same spirit with modern tools and techniques.

## Guiding Principles

- **Minimal library, maximum project.** The C++ runtime exists to get out of the way. The interesting work happens in shaders and compute.
- **Primary target: desktop with full GPU power.** No compromises for portability. Native Vulkan is the foundation.
- **WebGPU is optional and deferred.** Dawn/WebGPU remains useful for browser-facing showcases later, but it should not shape the first renderer abstraction.
- **Headless rendering is first-class.** Every project should eventually render to image without a window. Enables automated testing and AI-assisted development.
- **Shaders compile at build time.** No runtime hot-reload complexity. GLSL → SPIR-V via glslangValidator at build time.

## Technology Stack

| Layer | Choice | Rationale |
|-------|--------|-----------|
| Language | C++20 | Concepts, ranges, std::format, std::span |
| Build | CMake + Ninja | Cross-platform, fast incremental builds |
| Primary GPU API | Vulkan | Full GPU control, async compute, no feature ceiling |
| Optional future API | WebGPU (Dawn) | Browser showcases if the project earns that need |
| Windowing | GLFW | Minimal, Vulkan-native surface creation |
| UI | ImGui | Industry standard debug UI |
| Math | GLM | Familiar, header-only, Vulkan-friendly |
| Shader compilation | glslangValidator (build time) | GLSL → SPIR-V, no runtime dependency |
| Image output | stb_image_write | PNG output for headless mode |

## Architecture

```
                    +---------------------+
                    | Projects / Examples |  <-- fluid sim, SDF sculpt, particles, etc.
                    +----------+----------+
                               |
                    +----------v----------+
                    |   Cubey Runtime     |  <-- app loop, window, camera, UI, headless mode
                    +----------+----------+
                               |
                    +----------v----------+
                    |  Vulkan Renderer    |  <-- device, swapchain, resources, pipelines,
                    |  (primary path)     |      frame state, passes
                    +---------------------+
```

The current decision is deliberately not to build a broad backend-agnostic GPU
API yet. The WebGPU/Dawn spike showed that Dawn is already an abstraction layer;
adding another one above it would add project complexity while still inheriting
WebGPU's limits. The first implementation should make Vulkan livable through
small native modules, not hide it behind a premature portability contract.

### Renderer Seams

The near-term Vulkan renderer should split around real ownership boundaries:

- `Device` — instance, physical device, logical device, queue selection, validation
- `Surface` / `Swapchain` — platform surface handoff, surface extent ownership,
  image views, present-mode constraints, and resize/out-of-date recreation
- `Buffer` / `Image` — allocation, views, staging uploads, readback
- `Shader` / `Pipeline` — build-time shader paths, compute and graphics pipelines
- `FrameResources` — command pools/buffers, semaphores, fences, and eventual
  N-frames-in-flight
- project/pass code — the actual procedural experiments

These seams should be practical C++ modules first. A future WebGPU backend can
be reconsidered if the project needs browser showcases, but it should be driven by a
real use case rather than by symmetry.

### Resource Vocabulary

Projects and examples should eventually interact with a small set of
renderer-level concepts:

- `Buffer` — GPU buffer (vertex, storage, uniform, index, indirect)
- `Texture2D`, `Texture3D` — image data
- `Pipeline` — compute or render pipeline
- `BindGroup` — resource binding set

Core operations: create resources, dispatch compute, draw, synchronize, submit,
present, and read back. In the first version, those operations map directly to
Vulkan and remain free to expose Vulkan-specific requirements where useful.

### App Interface

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
- **Web (future WebGPU):** WGSL versions only for projects that explicitly need browser builds
- Not all projects need web versions; complex experiments stay desktop-only
- Shared shader includes (noise functions, math utilities) in a common directory

## Projected Projects

| Project | Source | Notes |
|------|--------|-------|
| Fluid Simulation | cubey1 rewrite | Eulerian 3D fluid sim, compute-based, raymarched volume rendering |
| Particle System | cubey1 rewrite | GPU particles, compute + indirect draw (replacing geometry shader) |
| Marching Cubes | cubey1 rewrite | Isosurface extraction via compute + indirect draw |
| Fractal 2D | cubey1 rewrite | Mandelbrot/Julia renderer |
| SDF Sculpting | projectR port | Sparse SDF brick tree, raymarched rendering, Morton-coded spatial indexing |

## Borrowing from Filament

Where applicable, borrow architectural patterns (not code) from Google's Filament:
- Handle-based resource management (`Handle<T>` — typed IDs, not raw pointers)
- Ring buffer / N-frames-in-flight without stalling
- Uniform arena / blob allocator for batching uniforms
- Sampler caching to deduplicate identical VkSampler objects

Do not copy Filament's backend/frontend split yet. Cubey does not currently
need multiple production backends, and Dawn/WebGPU already provides an
abstraction layer where browser portability is the goal.

## Repository Structure

Cubey is becoming a small C++ monorepo. The primary target is the `cubey`
library. Runnable binaries should be named explicitly and live in either
`examples/` or `projects/`:

- `include/cubey/` - public library headers. These define the include discipline
  used by examples, projects, and tests.
- `src/cubey/` - library implementation and private headers.
- `examples/` - small, focused reference programs that prove one concept or API
  path, such as `window_clear` or `headless_render`.
- `projects/` - first-class graphics experiments and longer-lived creative
  work, such as `fluid_sim`, `particles`, `marching_cubes`, `fractal`, and
  `sdf_sculpt`.
- `tools/` - repo utilities, asset processors, shader tools, or diagnostics.
- `tests/` - unit and integration tests.
- `benchmarks/` - performance targets once there is something meaningful to
  measure.

CMake should model this as explicit targets, not source-folder convention. The
dependency direction is:

```
cubey library
  ^
  |
examples / projects / tools / tests / benchmarks
```

Projects can depend on `cubey`; `cubey` must not depend on projects. Shared code
either graduates into `cubey` or stays local to the project that needs it.
Example-specific app behavior should stay in that example. The `cubey` library
should contain reusable runtime/platform pieces, not named examples such as
`window_clear`.

The `cubey` target should expose public headers now, but it should not gain
install/export/package rules until the project genuinely needs external
consumption and versioning.

```
cubey/
  CMakeLists.txt
  CHANGELOG.md             -- release-note source
  LICENSE                  -- MIT license
  include/
    cubey/
      app_config.h         -- shared run configuration
      runtime.h            -- future reusable app/runtime entrypoints
      vulkan/
        vk_check.h         -- Vulkan result helpers
        instance.h         -- instance, validation, debug messenger
        device.h           -- physical/logical device and queue ownership
        buffer.h           -- Vulkan buffer and memory ownership
        image.h            -- Vulkan image, memory, and image-view ownership
        sampler.h          -- Vulkan sampler ownership
        swapchain.h        -- swapchain images and image views
        shader_module.h    -- shader module lifetime
        frame_resources.h  -- per-frame command/sync resources
  src/
    cubey/
      app_config.cpp
      runtime.cpp          -- future lifecycle/run-loop implementation
      vulkan/
        instance.cpp       -- instance, validation, debug messenger
        device.cpp         -- physical/logical device, queues
        window.cpp         -- GLFW window + input
        buffer.cpp         -- buffers and host-visible upload
        image.cpp          -- images, memory, and image views
        sampler.cpp        -- samplers
        swapchain.cpp      -- surface extent, swapchain images/views
        shader_module.cpp  -- shader module lifetime
        pipeline.cpp       -- future pipeline layouts and pipelines
        frame_resources.cpp -- command buffers and sync objects
      camera.cpp           -- orbit camera
      imgui_layer.cpp      -- ImGui init/frame/shutdown
  examples/
    window_clear/          -- minimal visible Vulkan clear/present path; owns
                              example-specific app code
    triangle/              -- minimal shader-backed graphics pipeline path
    spinning_cube/         -- indexed cube, push constants, depth
    textured_cube/         -- compute texture generation, descriptors, sampling
    headless_render/       -- minimal offscreen image path
  projects/
      fluid_sim/
        CMakeLists.txt
        main.cpp
        shaders/
          fluid_advect.comp.glsl
          fluid_diffuse.comp.glsl
          fluid_render.vert.glsl
          fluid_render.frag.glsl
      sdf_sculpt/
      particles/
      marching_cubes/
      fractal/
  tools/
  tests/
  benchmarks/
  shaders/                 -- shared GLSL includes (noise, math)
  assets/                  -- textures, meshes
  docs/
    DESIGN.md              -- this file
    roadmap.md             -- living implementation plan
    working-notes.md       -- progress notes, hiccups, gotchas, learnings
    spike-findings.md      -- WebGPU/Vulkan spike decision record
    cpp-style.md           -- C++ naming, formatting, and review conventions
```

## Migration Notes

- `master` branch is preserved with the original code intact
- `main` branch starts from scratch
- Old `0.1`–`0.5` branches cleaned up from remote
- `webgpu` and `vulkan` branches are spike branches used to choose the renderer direction
- README and spike findings now capture the Vulkan-first decision before mainline implementation begins

## What We're Not Building

- Another SDL / raylib / shadertoy
- A game engine
- A framework others build on
- A cross-platform compatibility layer
- A dual Vulkan/WebGPU backend abstraction before there is a concrete browser-facing project need
- Runtime shader hot-reload
- A material/render-pass pipeline system

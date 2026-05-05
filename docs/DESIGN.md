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
- **WebGPU is optional and deferred.** Dawn/WebGPU remains useful for browser-facing showcases later, but it should not shape the first Vulkan layer.
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
| UI | None yet; ImGui is the likely debug UI | Current telemetry stays lightweight until UI earns the dependency |
| Math | Project-local helpers now; GLM remains a candidate | Keep dependencies narrow until shared math pressure appears |
| Shader compilation | glslangValidator (build time) | GLSL → SPIR-V, no runtime dependency |
| Image output | `stb_image_write` planned for headless PNG output | Single-header dependency, enough for inspectable artifacts |

## Architecture

```
                    +---------------------+
                    | Projects / Examples |  <-- fluid sim, SDF sculpt, particles, etc.
                    +----------+----------+
                               |
                    +----------v----------+
                    |   Cubey Runtime     |  <-- frame/input/resource helpers now;
                    |                     |      app/window/UI/headless layers later
                    +----------+----------+
                               |
                    +----------v----------+
                    |    Vulkan Layer      |  <-- device, swapchain, resources, pipelines,
                    |  (primary path)     |      dynamic rendering, frame state
                    +---------------------+
```

The current decision is deliberately not to build a broad backend-agnostic GPU
API yet. The WebGPU/Dawn spike showed that Dawn is already an abstraction layer;
adding another one above it would add project complexity while still inheriting
WebGPU's limits. The first implementation should make Vulkan livable through
small native modules, not hide it behind a premature portability contract.

### Vulkan Layer Seams

The near-term Vulkan layer should split around real ownership boundaries:

- `Device` — instance, physical device, logical device, queue selection, validation
- `Surface` / `Swapchain` — platform surface handoff, surface extent ownership,
  image views, present-mode constraints, and resize/out-of-date recreation
- `Buffer` / `Image` — allocation, views, staging uploads, readback
- `Shader` / `Pipeline` — build-time shader paths, compute and graphics pipelines
- `CommandPool` / `FrameResources` — command pool ownership, command-buffer
  allocation, semaphores, fences, and eventual N-frames-in-flight
- project/pass code — the actual procedural experiments

These seams should be practical C++ modules first. A future WebGPU backend can
be reconsidered if the project needs browser showcases, but it should be driven by a
real use case rather than by symmetry.

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

### Future App Interface

This is not implemented yet. Current examples own their app loops and GLFW
callbacks directly; the library only promotes reusable pieces once repeated
shape is clear.

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
- Shared shader includes (lighting, noise functions, math utilities) in a common directory

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
  path. Current examples are `window_clear`, `triangle`, `spinning_cube`, and
  `textured_cube`; `headless_render` is a planned path.
- `projects/` - first-class graphics experiments and longer-lived creative
  work, such as `fluid_sim`, `particles`, `marching_cubes`, `fractal`, and
  `sdf_sculpt`.
- `third_party/` - small vendored dependencies with explicit license notes.
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
      frame_clock.h        -- frame timing
      frame_stats.h        -- lightweight telemetry formatting
      orbit_controller.h   -- basic orbit input state
      vulkan/
        vk_check.h         -- Vulkan result helpers
        instance.h         -- instance, validation, debug messenger
        device.h           -- physical/logical device and queue ownership
        buffer.h           -- Vulkan buffer and memory ownership
        command_pool.h     -- command pool ownership and command-buffer begin
        descriptors.h      -- descriptor set layout/pool ownership
        frame_resources.h  -- per-frame command/sync resources
        image.h            -- Vulkan image, memory, and image-view ownership
        immediate_commands.h -- one-shot setup command submission
        pipeline.h         -- pipeline ownership and graphics setup helpers
        rendering.h        -- image transitions and dynamic-rendering helpers
        render_context.h   -- surface-backed begin/end frame lifecycle
        sampler.h          -- Vulkan sampler ownership
        shader_module.h    -- shader module lifetime
        swapchain.h        -- swapchain images and image views
  src/
    cubey/
      app_config.cpp
      frame_clock.cpp
      frame_stats.cpp
      orbit_controller.cpp
      vulkan/
        instance.cpp       -- instance, validation, debug messenger
        device.cpp         -- physical/logical device, queues
        buffer.cpp         -- buffers and host-visible upload
        command_pool.cpp   -- command pool ownership and command-buffer begin
        descriptors.cpp    -- descriptor set layout/pool ownership
        frame_resources.cpp -- command buffers and sync objects
        image.cpp          -- images, memory, and image views
        immediate_commands.cpp -- one-shot setup command submission
        pipeline.cpp       -- pipeline ownership and graphics setup helpers
        rendering.cpp      -- image transitions and dynamic-rendering helpers
        render_context.cpp -- surface-backed begin/end frame lifecycle
        sampler.cpp        -- samplers
        shader_module.cpp  -- shader module lifetime
        swapchain.cpp      -- surface extent, swapchain images/views
  examples/
    window_clear/          -- minimal windowed Vulkan clear/present path; owns
                              example-specific app code
    triangle/              -- minimal shader-backed graphics pipeline path
    spinning_cube/         -- indexed cube, push constants, depth
    textured_cube/         -- compute texture generation, uniforms, descriptors,
                              sampling
    headless_render/       -- planned minimal offscreen image path
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
  shaders/                 -- shared GLSL includes (lighting, noise, math)
  assets/                  -- textures, meshes
  third_party/             -- vendored single-header dependencies and notices
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
- `webgpu` and `vulkan` branches are spike branches used to choose the Vulkan-first direction
- README and spike findings capture the initial Vulkan-first decision;
  roadmap and working notes track the mainline implementation as it evolves

## What We're Not Building

- Another SDL / raylib / shadertoy
- A game engine
- A framework others build on
- A cross-platform compatibility layer
- A dual Vulkan/WebGPU backend abstraction before there is a concrete browser-facing project need
- Runtime shader hot-reload
- A material/render-graph/pipeline system

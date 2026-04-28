# Cubey 2.0 — Design Document

## What It Is

A minimal C++ framework for GPU-driven procedural graphics experiments and demos. Not a game engine, not a framework for others to build on — a personal workbench for trying things out quickly.

## Origin

The original cubey (2015) was a learning project for C++/OpenGL/GPGPU. It featured 5 working demos: fluid simulation, particles, marching cubes, fractals, and a camera test with shadows. The fluid sim demo gained traction on YouTube/GitHub (~28 stars, ~5 forks).

This is a ground-up rewrite carrying forward the same spirit with modern tools and techniques.

## Guiding Principles

- **Minimal framework, maximum demo.** The C++ framework exists to get out of the way. The interesting work happens in shaders and compute.
- **Primary target: desktop with full GPU power.** No compromises for portability. Native Vulkan is the foundation.
- **WebGPU is optional and deferred.** Dawn/WebGPU remains useful for browser-facing demos later, but it should not shape the first renderer abstraction.
- **Headless rendering is first-class.** Every demo can render to image without a window. Enables automated testing and AI-assisted development.
- **Shaders compile at build time.** No runtime hot-reload complexity. GLSL → SPIR-V via glslangValidator at build time.

## Technology Stack

| Layer | Choice | Rationale |
|-------|--------|-----------|
| Language | C++20 | Concepts, ranges, std::format, std::span |
| Build | CMake + Ninja | Cross-platform, fast incremental builds |
| Primary GPU API | Vulkan | Full GPU control, async compute, no feature ceiling |
| Optional future API | WebGPU (Dawn) | Browser demos if the project earns that need |
| Windowing | GLFW | Minimal, Vulkan-native surface creation |
| UI | ImGui | Industry standard debug UI |
| Math | GLM | Familiar, header-only, Vulkan-friendly |
| Shader compilation | glslangValidator (build time) | GLSL → SPIR-V, no runtime dependency |
| Image output | stb_image_write | PNG output for headless mode |

## Architecture

```
                    +---------------------+
                    |     Demo Layer      |  <-- fluid sim, SDF sculpt, particles, etc.
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
- `Surface` / `Swapchain` — GLFW surface, extent ownership, present, resize/out-of-date recreation
- `Buffer` / `Image` — allocation, views, staging uploads, readback
- `Shader` / `Pipeline` — build-time shader paths, compute and graphics pipelines
- `FrameResources` — command buffers, semaphores, fences, N-frames-in-flight
- demo/pass code — the actual procedural experiments

These seams should be practical C++ modules first. A future WebGPU backend can
be reconsidered if the project needs browser demos, but it should be driven by a
real use case rather than by symmetry.

### Resource Vocabulary

Demos should eventually interact with a small set of renderer-level concepts:

- `Buffer` — GPU buffer (vertex, storage, uniform, index, indirect)
- `Texture2D`, `Texture3D` — image data
- `Pipeline` — compute or render pipeline
- `BindGroup` — resource binding set

Core operations: create resources, dispatch compute, draw, synchronize, submit,
present, and read back. In the first version, those operations map directly to
Vulkan and remain free to expose Vulkan-specific requirements where useful.

### Demo Interface

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
    return cubey::run<MyDemo>(Config{.title = "fluid sim", .width = 1280, .height = 720});
}
```

## Testing & Feedback Loop

Every demo supports two modes:

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
- **Web (future WebGPU):** WGSL versions only for demos that explicitly need browser builds
- Not all demos need web versions; complex experiments stay desktop-only
- Shared shader includes (noise functions, math utilities) in a common directory

## Projected Demos

| Demo | Source | Notes |
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

## Directory Structure (Target)

```
cubey/
  CMakeLists.txt
  src/
    cubey/
      app.h/cpp           -- lifecycle, run loop
      renderer.h/cpp       -- high-level renderer facade used by demos
      device_vk.h/cpp      -- instance/device/queue/validation
      swapchain_vk.h/cpp   -- surface, swapchain, resize/present
      resources_vk.h/cpp   -- buffers, images, views, staging/readback
      pipeline_vk.h/cpp    -- shader modules, pipeline layouts, pipelines
      frame_vk.h/cpp       -- command buffers, sync objects, frame state
      window.h/cpp         -- GLFW window + input
      camera.h/cpp         -- orbit camera
      imgui_layer.h/cpp    -- ImGui init/frame/shutdown
    demos/
      fluid_sim/
        fluid_sim.h/cpp
        shaders/
          fluid_advect.comp.glsl
          fluid_diffuse.comp.glsl
          fluid_render.vert.glsl
          fluid_render.frag.glsl
      sdf_sculpt/
      particles/
      marching_cubes/
      fractal/
  shaders/                 -- shared GLSL includes (noise, math)
  assets/                  -- textures, meshes
  docs/
    DESIGN.md              -- this file
    spike-findings.md      -- WebGPU/Vulkan spike decision record
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
- A dual Vulkan/WebGPU backend abstraction before there is a concrete web-demo need
- Runtime shader hot-reload
- A material/render-pass pipeline system

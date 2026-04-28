# Roadmap

This is the living roadmap for Cubey 2.0. It should reflect the current plan,
not a frozen promise. When implementation changes the plan, update this file
before the old assumptions become tribal knowledge.

## Current Direction

Cubey 2.0 is a native Vulkan-first GPU workbench for procedural graphics
experiments. WebGPU/Dawn remains useful as an optional future presentation path,
but it is not the architecture driver for the main renderer.

## Phase 0: Repo Foundation

Status: in progress.

Goal: make `main` a clean place to build from before porting spike code.

- CMake presets for development, release, sanitizer, and clang-tidy builds.
- Formatting, linting, editor, Git text-normalization, and warning defaults.
- License, changelog/release-note source, roadmap, and working notes.
- C++ style guide for formatting, naming, ownership, and Vulkan structure.

Exit criteria:

- A new contributor or future agent can configure, build, and understand the
  project direction from the repo docs.
- Empty-project build and tooling presets work cleanly.

## Phase 1: Vulkan Runtime Skeleton

Goal: reshape the successful Vulkan spike into maintainable mainline modules.

- CLI/config surface for visible and headless modes.
- GLFW window creation and Vulkan surface ownership.
- Vulkan instance, physical device, logical device, queues, validation layers,
  and debug messenger setup.
- Swapchain acquisition, presentation, out-of-date handling, and compositor
  resize recovery.
- Frame resources: command buffers, semaphores, fences, and N-frames-in-flight.
- Build-time GLSL to SPIR-V shader flow.

Exit criteria:

- Visible smoke opens a window and renders a simple GPU result.
- Headless smoke renders and writes an inspectable image.
- Validation-layer smoke can be required from the command line.
- Resize and swapchain recreation remain first-class tested behavior.

## Phase 2: Resource Layer and Demo API

Goal: make demos concise without hiding important Vulkan constraints.

- RAII wrappers for buffers, images, image views, shader modules, pipelines, and
  frame-owned synchronization.
- Staging upload and readback paths for meshes, textures, uniforms, and compute
  outputs.
- A small demo interface for setup, update, render, UI, and input hooks.
- Practical renderer vocabulary for buffers, textures, pipelines, bind groups,
  dispatch, draw, submit, present, and readback.

Exit criteria:

- Demo code can focus on shader/data behavior instead of raw setup boilerplate.
- Vulkan synchronization and image layout requirements remain visible where
  they matter.
- Headless and visible runs share the same demo code path where practical.

## Phase 3: First Real Demo

Goal: prove the framework with one non-trivial procedural graphics demo.

Candidate demos:

- Fractal renderer for the fastest end-to-end visual loop.
- GPU particle system for compute plus graphics pipeline pressure.
- Fluid simulation rewrite for the strongest connection to the original cubey.

Exit criteria:

- The demo runs interactively with a window.
- The same demo can run headlessly for a fixed number of frames and produce a
  deterministic output artifact.
- README contains the exact commands for local smoke testing.

## Later

- ImGui debug controls.
- Orbit camera and common interaction helpers.
- Ports of original cubey demos: fluid simulation, particles, marching cubes,
  fractals, and camera/shadow tests.
- SDF sculpting experiments from `projectR` if the resource model holds up.
- WebGPU/browser revisit only after a concrete browser-facing demo earns the
  extra shader and platform surface area.

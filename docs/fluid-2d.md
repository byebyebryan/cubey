# Fluid 2D Project

`projects/fluid_2d` is the first Cubey project target. It starts as a compact
2D dye-and-velocity simulation so the project/runtime boundary is exercised
before moving to a full 3D fluid rewrite.

## Checkpoint 1

Status: initial pass complete.

Goal: render a deterministic compute-updated dye field in both windowed and
headless modes.

- Add a `projects/` CMake lane and `fluid_2d` binary.
- Use a fixed-size 2D grid with ping-pong GPU fields.
- Start with injection plus advection/fade compute passes.
- Render dye through a fullscreen graphics pass.
- Support a deterministic headless run that writes a PNG artifact.
- Deliberately defer pressure projection, richer controls, and reusable
  headless/project hosting until the first visible project path exists.

## Checkpoint 2

Status: pressure projection complete.

Goal: improve solver quality without extracting a renderer, scene system, or
generic simulation abstraction.

- Add scalar storage buffers for divergence and pressure ping-pong.
- Compute divergence from the advected velocity field and reset pressure each
  frame.
- Run fixed-count Jacobi pressure iterations.
- Subtract the pressure gradient from the velocity field in place so field A
  remains the next frame's source and the render source.
- Keep pressure resources and dispatch policy project-local until another
  project repeats the shape.

## Checkpoint 3

Status: interaction and debug views complete.

Goal: make the first project steerable and inspectable while keeping headless
output deterministic.

- Left-drag injects dye and cursor-derived force into the fluid field.
- Space pauses/resumes simulation without closing the window.
- `R` clears dye, velocity, divergence, and pressure buffers.
- `D` cycles render modes: dye, velocity, divergence, pressure.
- Headless mode continues to use the procedural injector and fixed timing so
  smoke output remains stable.
- Headless output runs through `cubey::HeadlessPngHost`; the project still owns
  field resources, compute simulation, render pipeline setup, and the fullscreen
  capture draw.

## Checkpoint 4

Status: project frame integration complete.

Goal: make the first project consume Cubey's runtime service vocabulary without
creating a generic project host.

- `fluid_2d` owns a `cubey::ProjectRuntimeServices` instance.
- Windowed and headless simulation steps now use `cubey::ProjectFrame` for
  delta time, elapsed time, frame index, and frame tickets.
- Vulkan resource setup, compute dispatch recording, fullscreen draw recording,
  input handling, and shutdown remain project-local callbacks.
- A broader project adapter remains deferred until another `projects/` target
  repeats the same lifecycle bridge.

Smoke commands:

```bash
./build/dev/projects/fluid_2d/fluid_2d --require-validation --frames 300 --width 1280 --height 720
./build/dev/projects/fluid_2d/fluid_2d --headless --require-validation --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-2d.png
```

## Current Solver Shape

The current slice still values predictable runtime/framework pressure over a
physically complete fluid, but it now includes the standard projection step:

```text
field A -> inject -> field B
field B -> advect/fade -> field A
field A -> divergence + pressure reset
pressure A/B -> Jacobi iterations
field A + pressure -> subtract gradient in place
field A -> fullscreen render
```

Each field cell stores dye and velocity. Divergence and pressure are separate
scalar buffers, which keeps the field layout stable while pressure solve details
remain local to this project.

## Next Slices

- Tune the solver now that velocity, divergence, and pressure are visible.
- Consider a minimal project-local HUD only if title-bar stats are not enough.
- Revisit reusable helpers only after a second project repeats buffer
  ping-pong descriptors, fixed-step simulation orchestration, or GPU capture
  polling.

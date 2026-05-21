# Smoke 2D History

This archive preserves the checkpoint record that previously lived in the main
`smoke_2d` README. Keep current design and usage notes in `README.md`; use this
file for historical context.

## Checkpoint 1

Status: initial pass complete.

Goal: render a deterministic compute-updated dye field in both windowed and
headless modes.

- Add a `projects/` CMake lane and `smoke_2d` binary.
- Use a fixed-size 2D grid with ping-pong GPU fields.
- Start with injection plus advection/fade compute passes.
- Render dye through a fullscreen graphics pass.
- Support a deterministic headless run that writes an artifact.
- Deliberately defer pressure projection, richer controls, and reusable
  headless/project hosting until the first visible project path exists.

## Checkpoint 2

Status: pressure projection complete.

Goal: improve solver quality without turning this checkpoint into a renderer,
scene system, or generic simulation abstraction.

- Add scalar storage buffers for divergence and pressure ping-pong.
- Compute divergence from the advected velocity field and reset pressure each
  frame.
- Run fixed-count Jacobi pressure iterations.
- Subtract the pressure gradient from the velocity field in place so field A
  remains the next frame's source and the render source.
- Keep pressure resources and dispatch policy project-local until the reusable
  solver/resource boundary is clear enough to test.

## Checkpoint 3

Status: interaction and debug views complete.

Goal: make the first project steerable and inspectable while keeping headless
output deterministic.

- Left-drag injects dye and cursor-derived force into the smoke field.
- Space pauses/resumes simulation without closing the window.
- `R` clears dye, velocity, divergence, and pressure buffers.
- `D` cycles render modes: dye, velocity, divergence, pressure.
- Headless mode continues to use the procedural injector and fixed timing so
  smoke output remains stable.
- Headless output runs through `cubey::host::HeadlessPngHost`; the project still
  owns field resources, compute simulation, render pipeline setup, and the
  fullscreen capture draw.

## Checkpoint 4

Status: project runtime adapter integration complete.

Goal: make the first project consume Cubey's runtime service vocabulary without
creating a generic project host.

- `smoke_2d` owns a `cubey::ProjectRuntimeAdapter` instance.
- Windowed and headless simulation steps now use `cubey::ProjectFrame` for
  delta time, elapsed time, frame index, and GPU submission tickets.
- The adapter owns runtime services, caches one project frame per host frame,
  exposes project context, and retires deferred destruction during shutdown.
- Vulkan resource setup, compute dispatch recording, fullscreen draw recording,
  input handling, and shutdown remain project-local callbacks.
- A broader project host remains deferred until another `projects/` target
  repeats the same lifecycle bridge.

## Checkpoint 5

Status: project GPU services integration complete.

Goal: keep `smoke_2d` on the async-ready runtime path without introducing a
generic project host.

- The project runtime adapter now attaches to the host `GpuRuntime` in windowed
  and headless modes.
- Project-owned field uploads run through `cubey::ProjectGpuServices` instead
  of using the host GPU runtime directly.
- Headless simulation frame work runs through `ProjectGpuServices`; the
  headless host still owns the offscreen target, capture transition, PNG output,
  and optional MP4 output path.
- Windowed frame command recording remains project-local because pass order,
  barriers, descriptors, and shader policy are still part of the fluid project.

## Checkpoint 6

Status: coarse render graph declaration and frame-boundary sync complete.

Goal: prove the render graph sync boundary on a compute-plus-graphics project
without moving fluid simulation policy into the renderer.

- Windowed frames now declare a coarse graph: simulation compute pass followed
  by fullscreen render pass.
- The compute pass still owns solver-internal storage-buffer barriers.
- The render pass records graph-derived buffer barriers for the
  compute-to-fragment-read boundary before drawing.
- The render pass also records graph-derived backbuffer acquire/release
  barriers instead of spelling out the color attachment and present transitions
  locally.
- Headless simulation keeps its direct project GPU services path and explicit
  final visibility barrier because capture rendering happens in a separate
  host-owned command path.

## Checkpoint 7

Status: sharper source ordering complete.

Goal: make source injection read as intentional input instead of being
immediately blurred by the advection pass.

- The simulation now advects the previous field first, then injects fresh dye
  and velocity into the visible field before divergence, pressure solve, and
  projection.
- The render graph declaration now names both ping-pong field buffers so graph
  sync sees the full compute write set.

## Checkpoint 8

Status: MacCormack advection complete.

Goal: reduce numerical diffusion so solver-driven motion stays crisp instead of
quickly becoming blurry dye.

- Advection now records a prediction pass into a temporary field, then a
  correction pass that reverse-advects the predicted field and applies a local
  neighborhood clamp.
- Dye and velocity decay moved to the correction pass so both use the corrected
  value.
- The render graph now imports the temporary field explicitly as simulation
  scratch storage.

## Checkpoint 9

Status: vorticity confinement complete.

Goal: make the fluid read less like blurred noise by reinforcing rotational
motion from the velocity field itself.

- The solver now computes a scalar curl field after injection and applies a
  confinement force before the pressure projection.
- Curl is available as the vorticity debug view in the existing debug-view
  cycle.
- Push constants now reserve a solver-options vector so simulation controls can
  grow without stealing fields from source/injection state.

## Checkpoint 10

Status: static obstacles and boundary handling complete.

Goal: make the CFD nature more legible by giving the flow geometry to move
around instead of only fading across an open rectangle.

- The project now uploads a static obstacle mask with solid borders and a few
  interior shapes when `--smoke-obstacles` is set.
- Injection, advection, curl, vorticity, divergence, pressure, and projection
  all read the mask so solid cells stay empty and pressure solve neighbors use a
  wall-aware fallback.
- The obstacle mask is available in the debug-view cycle and is lightly visible
  in the default dye view.

## Checkpoint 11

Status: render diagnostics and documentation sync complete.

Goal: keep the solver changes inspectable and make the default image read a bit
sharper without adding another simulation pass.

- The default dye view now applies a small render-only edge highlight from the
  dye gradient.
- Status, controls, current direction, and cross-project fluid docs now describe
  the MacCormack, vorticity, obstacle, and expanded debug-view state.

## Checkpoint 12

Status: multi-source visual injection complete.

Goal: make the default windowed and headless output read more like a deliberate
fluid demo without adding another solver stage.

- The old single fallback source was replaced with configurable stateful
  procedural sources, spread evenly around the hue wheel and driven by one
  orbit model.
- Built-in source motion is updated as simple project-local physics: each
  injector chases its orbit target with damping, boundary repulsion, and light
  separation from neighboring sources.
- Injector orbits are configurable by base radius, radius spread, signed base
  angular speed, signed speed spread, and phase spread. A zero base speed with a
  nonzero speed spread sends sources in both directions without needing a preset.
- Built-in sources inject dye plus velocity. New dye carries the source's
  current velocity, while an opposite-direction propulsion term pushes a wake
  behind the moving injector. Injection force scales both dye and velocity
  injection; propulsion controls the opposite-direction wake term.
- Dye and velocity decay are tuned for a controlled linger, while procedural
  sources stay narrow enough to keep the color streams separated.

## Checkpoint 13

Status: pointer injector removed.

Goal: keep `smoke_2d` focused on configurable procedural sources now that the
demo UI owns live tuning controls.

- Manual pointer splats were removed from the app input path, simulation push
  constants, injector shader, and tests.
- The remaining source controls now tune procedural injector radius and strength
  directly instead of carrying the old fallback-source naming.

## Checkpoint 14

Status: injector motion tuning expanded.

Goal: make the procedural sources easier to art-direct without changing the
solver.

- The old named injector motion presets were folded into one orbit model with
  radius spread, angular speed spread, and phase spread.
- Injection force and propulsion are configurable from the demo UI,
  `--smoke-injector-force`, and `--smoke-injector-propulsion`.

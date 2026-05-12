# Fluid 2D

`fluid_2d` is Cubey's first project target. It is a compact GPU dye-and-velocity
fluid simulation used to exercise the project/runtime boundary before moving to
larger simulation work.

The project is intentionally more substantial than an example, but it still owns
its simulation policy locally. Cubey provides the Vulkan/runtime pieces; this
project owns the field layout, compute passes, interaction model, render modes,
and tuning.

This README is the source of truth for `fluid_2d` design notes and checkpoint
history. Cross-project runtime decisions still belong under `docs/`.
The broader fluid technique map lives in
[`docs/architecture/fluid-simulation.md`](../../docs/architecture/fluid-simulation.md).

## Current Status

Implemented:

- Windowed and deterministic headless PNG modes.
- GPU storage-buffer fields for dye and velocity.
- Procedural and pointer-driven dye/force injection.
- Advection/fade compute pass.
- Divergence, Jacobi pressure solve, and pressure-gradient projection.
- Fullscreen rendering with dye, velocity, divergence, and pressure debug views.
- Integration with `cubey::host::WindowedHost`, `cubey::host::HeadlessPngHost`, and
  `cubey::ProjectRuntimeAdapter`.

Deferred:

- A reusable simulation abstraction.
- A full renderer-owned render graph or scene system.
- Rich UI beyond title-bar stats and keyboard debug views.
- External asset loading.

## Controls

- Left-drag: inject dye and cursor-derived force.
- Space: pause or resume simulation.
- `R`: reset dye, velocity, divergence, and pressure buffers.
- `D`: cycle dye, velocity, divergence, and pressure views.
- Escape: close the window.

## Runtime Shape

```text
field A -> inject -> field B
field B -> advect/fade -> field A
field A -> divergence + pressure reset
pressure A/B -> Jacobi iterations
field A + pressure -> subtract gradient in place
field A -> fullscreen render or headless capture
```

Each field cell stores dye and velocity. Divergence and pressure are separate
scalar buffers so pressure-solve details can evolve without changing the main
field layout.

## Technique Direction

`fluid_2d` should stay the incompressible grid-fluid lab. It is the right place
to improve the classic GPU Gems / Stable Fluids style solver, but it should not
become the general answer to all water simulation.

Near-term improvements worth trying:

- Better advection: MacCormack or BFECC to reduce numerical diffusion.
- Obstacles and boundaries: solid masks, moving obstacle velocity injection, and
  no-slip/free-slip boundary modes.
- Vorticity confinement: cheap visual energy for smoke-like dye motion.
- Pressure solver upgrades: red-black Gauss-Seidel, conjugate gradient, or later
  multigrid instead of only fixed-count Jacobi.

Separate experiments worth considering once the current grid path is cleaner:

- 2D level-set liquid: signed distance field plus marching-squares surface,
  useful for free-surface blobs and sloshing in cross-section.
- Particle level set: level set corrected by marker particles to reduce mass
  loss.
- Volume of fluid: better mass conservation than pure level set, with a more
  awkward interface reconstruction path.
- 2D PIC/FLIP/APIC: grid pressure solve plus particles for advection, likely
  more visually rewarding for liquid than pure level set.
- Lattice Boltzmann: useful for flow-around-obstacle experiments, less directly
  aligned with free-surface water.
- Reaction, buoyancy, or combustion: good if this project becomes a smoke/fire
  lab rather than a liquid lab.

Level-set water should be treated as a distinct mode or later project slice, not
as a tiny tweak to the current dye solver:

```text
velocity grid
signed distance field phi
phi < 0 = liquid
phi > 0 = air
advect phi by velocity
solve pressure only in liquid cells
use a free-surface pressure boundary near phi = 0
render phi = 0 with marching squares
```

Scaling guidance:

- `fluid_2d` is useful for solver learning, diagnostics, and stylized
  smoke/dye/liquid cross-sections.
- `fluid_25d` is the better path for scalable terrain water, rivers, and
  flooding.
- A future sparse 3D gas/smoke project is the better path for a modernized
  GPU-Gems-style volumetric demo.

## Historical Checkpoints

### Checkpoint 1

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

### Checkpoint 2

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

### Checkpoint 3

Status: interaction and debug views complete.

Goal: make the first project steerable and inspectable while keeping headless
output deterministic.

- Left-drag injects dye and cursor-derived force into the fluid field.
- Space pauses/resumes simulation without closing the window.
- `R` clears dye, velocity, divergence, and pressure buffers.
- `D` cycles render modes: dye, velocity, divergence, pressure.
- Headless mode continues to use the procedural injector and fixed timing so
  smoke output remains stable.
- Headless output runs through `cubey::host::HeadlessPngHost`; the project still
  owns field resources, compute simulation, render pipeline setup, and the
  fullscreen capture draw.

### Checkpoint 4

Status: project runtime adapter integration complete.

Goal: make the first project consume Cubey's runtime service vocabulary without
creating a generic project host.

- `fluid_2d` owns a `cubey::ProjectRuntimeAdapter` instance.
- Windowed and headless simulation steps now use `cubey::ProjectFrame` for
  delta time, elapsed time, frame index, and GPU submission tickets.
- The adapter owns runtime services, caches one project frame per host frame,
  exposes project context, and retires deferred destruction during shutdown.
- Vulkan resource setup, compute dispatch recording, fullscreen draw recording,
  input handling, and shutdown remain project-local callbacks.
- A broader project host remains deferred until another `projects/` target
  repeats the same lifecycle bridge.

### Checkpoint 5

Status: project GPU services integration complete.

Goal: keep `fluid_2d` on the async-ready runtime path without introducing a
generic project host.

- The project runtime adapter now attaches to the host `GpuRuntime` in windowed
  and headless modes.
- Project-owned field uploads run through `cubey::ProjectGpuServices` instead
  of using the host GPU runtime directly.
- Headless simulation frame work runs through `ProjectGpuServices`; the
  headless host still owns the offscreen target, capture transition, and PNG
  artifact path.
- Windowed frame command recording remains project-local because pass order,
  barriers, descriptors, and shader policy are still part of the fluid project.

### Checkpoint 6

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

## Commands

```bash
./build/dev/projects/fluid_2d/fluid_2d --require-validation --frames 300 --width 1280 --height 720
./build/dev/projects/fluid_2d/fluid_2d --headless --require-validation --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-2d.png
```

## Next Slices

- Improve advection quality before adding more visual polish.
- Add obstacle masks and boundary-condition debug views.
- Consider a project-local HUD only if title-bar stats are not enough.
- Revisit reusable helpers when buffer ping-pong descriptors, fixed-step
  simulation orchestration, or capture/readback polling have a clear shared
  contract across the fluid projects.

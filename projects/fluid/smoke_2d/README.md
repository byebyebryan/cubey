# Smoke 2D

`smoke_2d` is Cubey's first project target. It is a compact GPU dye-and-velocity
fluid simulation used to exercise the project/runtime boundary before moving to
larger simulation work.

The project is intentionally more substantial than an example, but it still owns
its simulation policy locally. Cubey provides the Vulkan/runtime pieces; this
project owns the field layout, compute passes, interaction model, render modes,
and tuning.

This README is the current design note for `smoke_2d`. Older checkpoint notes
are archived in [`HISTORY.md`](HISTORY.md). Cross-project runtime decisions
still belong under `docs/`.
The broader fluid technique map lives in
[`docs/architecture/fluid-simulation.md`](../../../docs/architecture/fluid-simulation.md).

## Current Status

Implemented:

- Windowed mode plus deterministic headless PNG and optional MP4 capture modes.
- Configurable GPU storage-buffer grid for dye and velocity.
- Configurable moving procedural dye/force injectors.
- MacCormack advection with local clamping and fade.
- Optional static obstacle mask plus solid-cell boundary handling.
- Curl, vorticity confinement, divergence, Jacobi pressure solve, and
  pressure-gradient projection.
- Fullscreen rendering with dye, velocity, divergence, pressure, speed,
  vorticity, and obstacle debug views.
- Integration with `cubey::host::WindowedHost`, `cubey::host::HeadlessPngHost`, and
  `cubey::ProjectRuntimeAdapter`.

Deferred:

- A reusable simulation abstraction.
- A full renderer-owned render graph or scene system.
- Editor-style UI beyond the compact live-tuning panel.
- External asset loading.

## Controls

- Space: pause or resume simulation.
- `R`: reset dye, velocity, divergence, and pressure buffers.
- `D`: cycle dye, velocity, divergence, pressure, speed, vorticity, and
  obstacle views.
- Escape: close the window.

The windowed build also exposes a small debug UI for live demo tuning: pause,
reset, debug view, injector count/orbits, pressure iterations, vorticity,
decay, and injection radius/force/propulsion.

## Runtime Shape

The default solver grid is `1024x1024`. Override it with `--grid-width` and
`--grid-height` when comparing quality or performance. The default procedural
injector count is three; override it with `--smoke-injectors` from `1` to `16` to
spread more sources around the hue wheel. Procedural sources use one orbit model
with configurable radius, radius spread, signed angular speed, angular speed
spread, and phase spread. Use `--smoke-injector-orbit-radius`,
`--smoke-injector-orbit-radius-spread`, `--smoke-injector-orbit-angular-speed`,
`--smoke-injector-orbit-angular-speed-spread`, `--smoke-injector-orbit-phase-spread`,
`--smoke-injector-force`, and `--smoke-injector-propulsion` to tune captures.
Static obstacles are disabled by default; enable them with `--smoke-obstacles`.
Use `--debug-view dye|velocity|divergence|pressure|speed|vorticity|obstacle` to
start windowed or headless runs in a diagnostic view.

```text
field A -> advect predict -> field temp
field A + field temp -> MacCormack correction/clamp/fade -> field B
field B -> inject fresh dye/force -> field A
optional static obstacle mask constrains injection/advection/pressure/projection
field A -> curl -> vorticity confinement -> field A
field A -> divergence + pressure reset
pressure A/B -> Jacobi iterations
field A + pressure -> subtract gradient in place
field A -> fullscreen render or headless capture
```

Each field cell stores dye and velocity. Divergence and pressure are separate
scalar buffers so pressure-solve details can evolve without changing the main
field layout.

## Technique Direction

`smoke_2d` should stay the incompressible grid-fluid lab. It is the right place
to improve the classic GPU Gems / Stable Fluids style solver, but it should not
become the general answer to all water simulation.

Near-term improvements worth trying:

- Pressure solver upgrades: red-black Gauss-Seidel, conjugate gradient, or later
  multigrid instead of only fixed-count Jacobi.
- Moving obstacle velocity injection and explicit no-slip/free-slip boundary
  modes.
- Richer injectors, turbulence/detail synthesis, and more deliberate smoke or
  dye shading.

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

- `smoke_2d` is useful for solver learning, diagnostics, and stylized
  smoke/dye/liquid cross-sections.
- `fluid_25d` is the better path for scalable terrain water, rivers, and
  flooding.
- A future sparse 3D gas/smoke project is the better path for a modernized
  GPU-Gems-style volumetric demo.

## Commands

```bash
./build/dev/projects/fluid/smoke_2d/smoke_2d --require-validation --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/smoke_2d/smoke_2d --frames 300 --print-frame-stats --grid-width 512 --grid-height 512 --width 1280 --height 720
./build/dev/projects/fluid/smoke_2d/smoke_2d --frames 300 --smoke-injectors 8 --smoke-injector-force 7.5 --smoke-injector-propulsion 1.4 --width 1280 --height 720
./build/dev/projects/fluid/smoke_2d/smoke_2d --frames 300 --smoke-injectors 8 \
    --smoke-injector-orbit-radius 0.25 --smoke-injector-orbit-radius-spread 0.24 \
    --smoke-injector-orbit-angular-speed 0.0 --smoke-injector-orbit-angular-speed-spread 1.2 \
    --smoke-injector-orbit-phase-spread 1.0 \
    --width 1280 --height 720
./build/dev/projects/fluid/smoke_2d/smoke_2d --frames 300 --smoke-obstacles --width 1280 --height 720
./build/dev/projects/fluid/smoke_2d/smoke_2d --headless --require-validation --frames 120 --width 640 --height 360 --output /tmp/cubey-smoke-2d.png
./build/dev/projects/fluid/smoke_2d/smoke_2d --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-smoke-2d.mp4
```

## Next Slices

- Upgrade the pressure solver beyond fixed-count Jacobi.
- Add moving obstacles or obstacle velocity coupling.
- Decide whether `smoke_2d` should lean smoke/dye, free-surface liquid, or split
  those into separate project modes.
- Consider a project-local HUD only if title-bar stats are not enough.
- Revisit reusable helpers when buffer ping-pong descriptors, fixed-step
  simulation orchestration, or capture/readback polling have a clear shared
  contract across the fluid projects.

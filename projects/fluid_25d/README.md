# Fluid 2.5D

Status: design first. No CMake target or implementation exists yet.

`fluid_25d` is the filesystem and target-friendly name for a 2.5D fluid project:
a 2D shallow-water simulation over a heightmap terrain. The goal is water that
flows downhill, pools in basins, drains through sinks, and produces useful
debug views without jumping straight to a full 3D fluid solver.

## Priority

1. River/source/sink demo.
2. Flooding over terrain.
3. Interactive terrain/water toy.

The first checkpoint should optimize for a controlled river-like setup because
it gives clearer signal than an open-ended flood: a static terrain, one or more
water sources, one or more sinks/outflow regions, visible flow direction, and a
headless PNG smoke path.

Flooding comes next once source/sink behavior, mass handling, wet/dry cells, and
stability are inspectable. Interactive terrain or source editing should wait
until the solver and debug views are understandable enough to tune.

## Simulation Model

Use a shallow-water heightfield model rather than extending `fluid_2d`'s
incompressible dye solver.

Core state:

- `terrain_height`: static bed elevation.
- `water_depth`: water column height at each cell.
- `surface_height`: derived value, `terrain_height + water_depth`.
- `flow_x`, `flow_y`: edge or face flow/flux buffers.
- Optional `velocity`: derived from flux for rendering and debug views.
- Optional `source`, `sink`, and `mask` fields for authored scenarios.

Recommended first solver: virtual-pipes shallow water.

The virtual-pipes shape maps well to compute shaders because each cell exchanges
water with neighboring cells through local flow values. It is less exact than a
formal finite-volume Saint-Venant solver, but it is a better first fit for a
Cubey project: compact buffers, clear debug modes, good visual feedback, and a
straight path to source/sink/flooding scenarios.

Initial solve sketch:

```text
apply sources/sinks/rain
derive surface height from terrain + water depth
update outflow from surface-height differences
scale outgoing flow so a cell cannot export more water than it owns
update water depth from inflow - outflow
derive velocity/flow magnitude for rendering
render terrain + water/debug view
```

Stability knobs should be explicit from the start:

- Fixed simulation time step.
- Substeps.
- Gravity scale.
- Flow damping/friction.
- Minimum wet depth.
- Maximum wave or flow speed clamp.
- Boundary mode: closed, open/draining, or damping band.

## Math Context

Shallow-water simulation is related to Navier-Stokes, but it is not the same
solver shape as `fluid_2d`.

`fluid_2d` is an incompressible 2D projection solver:

```text
advect velocity/dye
solve pressure
subtract pressure gradient
enforce div(velocity) = 0
```

That is useful for smoke-like dye motion, but it is the wrong primary model for
water flowing over terrain because horizontal divergence should change the water
column height rather than being projected away.

`fluid_25d` should treat shallow water as a depth-averaged free-surface model:

```text
b = terrain height
h = water depth
eta = b + h = water surface height
v = depth-averaged horizontal velocity
q = h * v = horizontal water flux

d h / d t = -div(q) + sources - sinks
acceleration is driven mainly by -g * grad(eta), plus friction/damping
```

So the important behavior is conservation of water depth: if more water leaves a
cell than enters it, `h` decreases; if more water enters than leaves, `h`
increases. Pressure is mostly represented through hydrostatic free-surface slope
instead of a Poisson pressure projection.

Virtual pipes are the near-term discretization because they are compact and
GPU-friendly. A more formal finite-volume Saint-Venant solver is worth a later
experiment once the project has baseline visuals and test scenarios. That slice
should compare:

- Mass conservation.
- Wet/dry front behavior.
- Stability limits and CFL/substep requirements.
- River/source/sink quality.
- Flooding over uneven terrain.
- Cost and shader complexity versus virtual pipes.
- Whether the scheme is well-balanced, meaning still water over uneven terrain
  stays still instead of generating numerical flow.

## Rendering And Debug Views

The first render path can be top-down or lightly oblique. A full terrain mesh is
not required for checkpoint 1 if a fullscreen pass can show the fields clearly.

Expected views:

- Terrain height.
- Water depth.
- Surface height.
- Flow magnitude.
- Flow direction.
- Wet/dry mask.

The visual target is not photoreal water. The important signal is whether water
moves according to terrain, conserves mass well enough to trust the result, and
stays stable under simple scenarios.

## Project Boundaries

Create this as `projects/fluid_25d`, not as an example. It should reuse the same
runtime pieces as `fluid_2d`:

- `cubey::WindowedHost` for visible runs.
- `cubey::HeadlessPngHost` for deterministic artifact output.
- `cubey::ProjectRuntimeAdapter` for frame timing, frame tickets, and runtime
  services.

Keep the first solver buffers, shader schedule, terrain generator, and render
policy project-local. Promote shared helpers only after `fluid_2d` and
`fluid_25d` reveal a repeated shape.

## First Checkpoint

Target behavior:

- Build a `fluid_25d` project target.
- Generate deterministic static terrain on the GPU or CPU.
- Add one source and one sink/outflow region.
- Simulate shallow-water flow entirely on the GPU.
- Render at least water depth and flow magnitude.
- Support a deterministic headless PNG smoke command.
- Add config parsing/tests before expanding controls.

Non-goals:

- Erosion.
- Sediment transport.
- Full 3D water volume.
- Spray, splashes, foam, or breaking waves.
- High-order flood modeling accuracy.
- Finite-volume Saint-Venant solving in checkpoint 1.
- Generic simulation framework extraction.

## References

- SideFX Houdini Shallow Water Solver docs describe the same broad problem
  class: bulk water over heightfields, source/sink layers, exported velocity,
  and limits such as no spray or breaking waves.
  <https://www.sidefx.com/docs/houdini/nodes/sop/shallowwatersolver.html>
- SideFX's shallow-water introduction frames this as a practical middle ground
  for ponds, streams, terrain flooding, and distant/stylized water over
  heightfields.
  <https://www.sidefx.com/docs/houdini/heightfields/shallowintro.html>
- Dagenais et al., "Real-Time Virtual Pipes Simulation and Modeling for
  Small-Scale Shallow Water", is a useful research anchor for virtual-pipes
  shallow-water simulation.
  <https://diglib.eg.org/items/18a6249d-843a-46a7-8f65-54b7baf81405>
- `webgpu-shallow-water` is a compact GPU-oriented reference for virtual-pipes
  buffer layout and outflow scaling.
  <https://github.com/lisyarus/webgpu-shallow-water>

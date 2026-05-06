# Fluid 2D Project

`projects/fluid_2d` is the first Cubey project target. It starts as a compact
2D dye-and-velocity simulation so the project/runtime boundary is exercised
before moving to a full 3D fluid rewrite.

## Checkpoint 1

Goal: render a deterministic compute-updated dye field in both windowed and
headless modes.

- Add a `projects/` CMake lane and `fluid_2d` binary.
- Use a fixed-size 2D grid with ping-pong GPU fields.
- Start with injection plus advection/fade compute passes.
- Render dye through a fullscreen graphics pass.
- Support a deterministic headless run that writes a PNG artifact.
- Defer pressure projection, richer controls, and reusable headless/project
  hosting until the first visible project path exists.

## First Solver Shape

The first slice intentionally uses a simple visual solver:

```text
field A -> inject -> field B
field B -> advect/fade -> field A
field A -> fullscreen render
```

Each field cell stores dye and velocity. The first version values predictable
runtime/framework pressure over physically complete incompressible flow. The
next solver slice can add divergence, pressure Jacobi iterations, and gradient
subtraction once the project shell and headless artifact path are working.

# Fluid 2D

`fluid_2d` is Cubey's first project target. It is a compact GPU dye-and-velocity
fluid simulation used to exercise the project/runtime boundary before moving to
larger simulation work.

The project is intentionally more substantial than an example, but it still owns
its simulation policy locally. Cubey provides the Vulkan/runtime pieces; this
project owns the field layout, compute passes, interaction model, render modes,
and tuning.

## Current Status

Implemented:

- Windowed and deterministic headless PNG modes.
- GPU storage-buffer fields for dye and velocity.
- Procedural and pointer-driven dye/force injection.
- Advection/fade compute pass.
- Divergence, Jacobi pressure solve, and pressure-gradient projection.
- Fullscreen rendering with dye, velocity, divergence, and pressure debug views.
- Integration with `cubey::WindowedHost`, `cubey::HeadlessPngHost`, and
  `cubey::ProjectRuntimeAdapter`.

Deferred:

- A reusable simulation abstraction.
- A reusable render graph or scene system.
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

## Commands

```bash
./build/dev/projects/fluid_2d/fluid_2d --require-validation --frames 300 --width 1280 --height 720
./build/dev/projects/fluid_2d/fluid_2d --headless --require-validation --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-2d.png
```

## Notes

The checkpoint log and historical decisions live in
[`docs/fluid-2d.md`](../../docs/fluid-2d.md). This README is the project-local
entrypoint for the current shape.

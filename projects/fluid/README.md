# Fluid

Fluid projects are Cubey's simulation-focused demos. This directory groups the
demos by the physical effect they present, while keeping the solver code close
to each demo until shared code is clearly useful.

Current projects:

- `smoke_2d`: 2D incompressible smoke/dye simulation.
- `fire_3d`: 3D dense-grid pyro fire simulation.
- `explosion_3d`: 3D dense-grid pyro explosion simulation.

Shared 3D pyro code lives under `sim/pyro_3d`. The 2D smoke solver remains
project-local for now because its data model and rendering path are
intentionally different from the 3D volume solver.

# Fluid

Fluid projects are Cubey's simulation-focused demos. This directory groups the
demos by the physical effect they present, while keeping the solver code close
to each demo until shared code is clearly useful.

Current projects:

- `smoke_2d`: 2D incompressible smoke/dye simulation.
- `water_2d`: 2D APIC free-surface liquid simulation on a MAC grid, with a
  PIC/FLIP fallback, reset presets, hose/drain material flow, obstacle shapes,
  and particle-splat surface rendering plus live frame/memory diagnostics.
- `water_3d`: 3D APIC/PIC-FLIP liquid simulation foundation on a MAC grid,
  currently rendered as particle splats with slice diagnostics.
- `fire_3d`: 3D dense-grid pyro fire simulation.
- `explosion_3d`: 3D dense-grid pyro explosion simulation.

Shared 3D pyro code lives under `sim/pyro_3d`; the 3D liquid foundation lives
under `sim/water_3d`. The 2D smoke and water solvers remain separate because
smoke uses collocated dye/velocity fields while water uses particles for liquid
motion plus a MAC grid for pressure projection.

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
  with a long-tank screen-space surface renderer, hose/drain flow, wave forcing,
  optional rain, whitewater, and diagnostics.
- `fluid_25d`: standalone builtin Vulkan shallow-water experiments with
  virtual-pipes and finite-volume solvers. See its [quickstart](fluid_25d/README.md).
  The featured external terrain-water experience is now the sibling
  [Terrain Water project](../terrain_water/README.md); shared implementation
  components and legacy study/native artifact paths remain retained here.
- `fire_3d`: 3D dense-grid pyro fire simulation.
- `explosion_3d`: 3D dense-grid pyro explosion simulation.

Shared 3D pyro code lives under `sim/pyro_3d`; the 3D liquid foundation lives
under `sim/water_3d`. The 2D smoke and water solvers remain separate because
smoke uses collocated dye/velocity fields while water uses particles for liquid
motion plus a MAC grid for pressure projection.

Builtin Fluid 2.5D separates host flow, backend status/forcing, presentation,
frame recording, diagnostics and oracle validation into private modules under
`sim/fluid_25d`. Both Fluid and Terrain Water compile that implementation with
their own entry point and shader directory. Recording/live playback retains its
separate private modules and external-worker boundary.

Fluid's CPU suite keeps its aggregate entry point and offers six focused domain
selectors through `ctest --preset dev-fluid-domains`. Test registrations live
under `tests/fluid_25d`, included from the frontend directory so existing test
names, commands and artifact paths remain stable. The project-owned GPU
resource fixture checks partial failure, direct retry, recreation and re-enable
for all five resource families in ordinary and synchronization-validation lanes.

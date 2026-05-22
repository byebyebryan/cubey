# Water 2D

`water_2d` is Cubey's first free-surface liquid project. It uses a particle
PIC/FLIP layer for material motion and a face-centered MAC grid for pressure,
boundaries, and incompressibility.

The project is deliberately separate from `smoke_2d`. Smoke uses collocated
dye/velocity fields; water tracks liquid with particles, transfers velocity to a
staggered grid, solves pressure on occupied cells, then transfers the projected
velocity back to particles.

## Current Target

The implementation is a live 2D dam-break sandbox:

```sh
./build/dev/projects/fluid/water_2d/water_2d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/water_2d/water_2d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-water-2d.png
./build/dev/projects/fluid/water_2d/water_2d --headless --debug-view particles --frames 120 --width 640 --height 360 --output /tmp/cubey-water-2d-particles.png
```

Controls:

- Space pauses/resumes.
- `R` resets the tank.
- `D` cycles debug views.

Debug views:

- `surface`: shaded particle-splat water surface.
- `particles`: sharper particle splat visualization.
- `cells`: occupied liquid cells from the particle bins.
- `velocity`: projected face velocity sampled to pixels.
- `divergence`: occupied-cell divergence before projection.
- `pressure`: pressure solve output.
- `solid`: walls and optional obstacle mask.

## Solver Shape

Each frame clears the grid and particle bins, bins particles into fixed-capacity
cell slots, transfers particle velocity to `u` and `v` MAC faces, applies
gravity, computes occupied-cell divergence, solves pressure with Jacobi,
projects face velocity, transfers the current-vs-previous grid delta back to
particles with a configurable PIC/FLIP blend, then advects and collides the
particles.

Main buffers:

- `particle_positions` and `particle_velocities`: `vec4` particle state.
- `cell_counts` and `cell_particle_indices`: fixed-capacity particle bins.
- `u` and `v`: face velocity on vertical and horizontal grid faces.
- `u_previous` and `v_previous`: pre-force/projection velocity for FLIP deltas.
- `pressure` and `divergence`: cell-centered scalar fields.
- `solid`: cell-centered obstacle/wall mask.

This is still a foundation slice. It intentionally skips APIC, viscosity,
surface tension, meshing, and sparse/adaptive particle storage until the basic
PIC/FLIP contract is easier to inspect.

The fill controls are runtime-editable. GPU particle buffers are allocated for
the maximum editable fill area, while each reset computes an active particle
count from the current fill width and height.

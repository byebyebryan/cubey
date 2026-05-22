# Water 2D

`water_2d` is Cubey's first free-surface liquid project. It uses a particle
PIC/FLIP layer for material motion and a face-centered MAC grid for pressure,
boundaries, and incompressibility.

The project is deliberately separate from `smoke_2d`. Smoke uses collocated
dye/velocity fields; water tracks liquid with particles, transfers velocity to a
staggered grid, solves pressure on occupied cells, then transfers the projected
velocity back to particles.

## Current Target

The implementation is a live 2D PIC/FLIP sandbox. The default reset is a
dam-break slab, with additional obstacle-splash, wave-slab, and hose-fill
presets available from the runtime UI:

```sh
./build/dev/projects/fluid/water_2d/water_2d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/water_2d/water_2d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-water-2d.png
./build/dev/projects/fluid/water_2d/water_2d --headless --debug-view particles --frames 120 --width 640 --height 360 --output /tmp/cubey-water-2d-particles.png
```

Controls:

- Space pauses/resumes.
- `R` resets the tank.
- `D` cycles debug views.
- The UI edits reset preset, fill volume, hose emission, bottom drain,
  obstacle shape, solver substeps, pressure iterations, PIC/FLIP blend,
  particle separation, collision tuning, surface/foam shading, and live
  frame/memory diagnostics.

Debug views:

- `surface`: shaded particle-splat water surface.
- `particles`: sharper particle splat visualization.
- `cells`: occupied liquid cells from the particle bins.
- `velocity`: projected face velocity sampled to pixels.
- `divergence`: occupied-cell divergence before projection.
- `pressure`: pressure solve output.
- `solid`: walls and optional obstacle mask.
- `foam`: free-surface/speed highlight used by the shaded view.

## Solver Shape

Each substep clears the grid, emits any hose particles into the hose ring over
currently inactive particle slots, bins active particles into fixed-capacity cell slots,
transfers particle velocity to `u` and `v` MAC faces, applies gravity, computes
occupied-cell divergence, solves pressure with Jacobi, projects face velocity,
transfers the current-vs-previous grid delta back to particles with a
configurable PIC/FLIP blend, adds a bounded neighbor-bin particle separation
velocity only for overpacked cells to keep material volume from collapsing
without disturbing settled regions, then advects and collides the particles.
Particles that enter the optional bottom drain are marked inactive; no
compaction or readback free-list is involved.

Main buffers:

- `particle_positions` and `particle_velocities`: `vec4` particle state. The
  position `.w` lane is the active flag.
- `cell_counts` and `cell_particle_indices`: fixed-capacity particle bins.
- `u` and `v`: face velocity on vertical and horizontal grid faces.
- `u_previous` and `v_previous`: pre-force/projection velocity for FLIP deltas.
- `pressure` and `divergence`: cell-centered scalar fields.
- `solid`: cell-centered obstacle/wall mask.

The renderer reconstructs a lightweight surface from the particle bins. It uses
particle density, a density-gradient fake normal, speed, and free-surface
highlighting for a readable real-time liquid view without adding a meshing pass.

This is still a foundation slice. It intentionally skips APIC, viscosity,
surface tension, meshing, and sparse/adaptive particle storage until the basic
PIC/FLIP contract is easier to inspect.

The fill controls are runtime-editable. GPU particle buffers are allocated for
the maximum editable fill area plus a larger explicit hose reserve, while each
reset computes a reset-fill particle count from the current fill width and
height. The hose pool starts after that reset-fill range, so smaller initial
fills leave more room for continuous emission before the hose ring wraps. The UI
reports average FPS/frame time, Water2D buffer allocation size, and device-local
memory usage when the Vulkan driver exposes `VK_EXT_memory_budget`.

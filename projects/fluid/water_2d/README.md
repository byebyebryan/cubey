# Water 2D

`water_2d` is Cubey's first free-surface liquid project. It uses particles for
material motion, APIC particle-grid transfer by default, a PIC/FLIP fallback for
comparison, and a face-centered MAC grid for pressure, boundaries, and
incompressibility.

The project is deliberately separate from `smoke_2d`. Smoke uses collocated
dye/velocity fields; water tracks liquid with particles, transfers velocity to a
staggered grid, solves pressure on occupied cells, then transfers the projected
velocity back to particles.

## Current Target

The implementation is a live 2D particle-grid water sandbox. The default reset
is a dam-break slab, with additional obstacle-splash, wave-slab, and hose-fill
presets available from the runtime UI:

```sh
./build/dev/projects/fluid/water_2d/water_2d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/water_2d/water_2d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-water-2d.png
./build/dev/projects/fluid/water_2d/water_2d --headless --debug-view particles --frames 120 --width 640 --height 360 --output /tmp/cubey-water-2d-particles.png
./build/dev/projects/fluid/water_2d/water_2d --water2d-transfer pic-flip --water2d-transfer-limit 48 --water2d-hose --water2d-drain
```

Controls:

- Space pauses/resumes.
- `R` resets the tank.
- `D` cycles debug views.
- The UI edits reset preset, transfer mode, fill volume, hose emission, bottom
  drain, obstacle shape, solver substeps, pressure iterations, PIC/FLIP blend,
  particle separation, particle volume expansion, collision tuning, surface/foam
  shading, and live frame/memory diagnostics.

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
occupied-cell divergence, adds a bounded positive volume source for overpacked
particle cells, solves pressure with Jacobi, projects face velocity, then
transfers velocity back to particles. APIC mode stores a local affine velocity
field per particle and uses it during the next particle-to-grid transfer, which
preserves rotational/local motion better than raw PIC/FLIP. The fallback mode
keeps the old current-vs-previous grid delta path with a configurable PIC/FLIP
blend. After transfer, the solver adds a bounded neighbor-bin particle
separation velocity only for overpacked cells to keep material volume from
collapsing without disturbing settled regions, then advects and collides the
particles. Particles that enter the optional bottom drain are marked inactive;
no compaction or readback free-list is involved.

Main buffers:

- `particle_positions` and `particle_velocities`: `vec4` particle state. The
  position `.w` lane is the active flag.
- `particle_affine`: `vec4` particle APIC affine velocity state. It is zeroed
  on reset, emission, drain, collision, and while running in PIC/FLIP transfer
  mode.
- `cell_counts` and `cell_particle_indices`: fixed-capacity particle bins.
  `cell_counts` tracks raw occupancy, while particle-grid transfer, separation,
  and rendering are bounded by the stored `max_particles_per_cell` slots.
  Volume pressure uses raw occupancy so overpacked cells still push back instead
  of disappearing behind the transfer cap.
- `u` and `v`: face velocity on vertical and horizontal grid faces.
- `u_previous` and `v_previous`: pre-force/projection velocity for FLIP deltas.
- `pressure` and `divergence`: cell-centered scalar fields.
- `solid`: cell-centered obstacle/wall mask.

The renderer reconstructs a lightweight surface from the particle bins. It uses
particle density, a density-gradient fake normal, speed, and free-surface
highlighting for a readable real-time liquid view without adding a meshing pass.

This is still a foundation slice. It intentionally skips viscosity, surface
tension, meshing, and sparse/adaptive particle storage until the basic
particle-grid contract is easier to inspect.

The fill controls are runtime-editable. GPU particle buffers are allocated for
the maximum editable fill area plus a larger explicit hose reserve, while each
reset computes a reset-fill particle count from the current fill width and
height. The hose pool starts after that reset-fill range, so smaller initial
fills leave more room for continuous emission before the hose ring wraps. Runtime
particle kernels scan only the reset range plus hose slots that have actually
been touched; after the hose ring wraps, the scan range expands to the full
allocated particle buffer. The UI reports this compute-particle scan count,
average FPS/frame time, Water2D buffer allocation size, and device-local memory
usage when the Vulkan driver exposes `VK_EXT_memory_budget`.

Current correctness boundaries: particles are not compacted after drain, cells
only store a fixed number of particle indices, and the pressure solver remains
fixed-iteration Jacobi. These are deliberate for now so the particle-grid
contract remains simple to inspect.

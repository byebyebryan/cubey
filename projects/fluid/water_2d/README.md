# Water 2D

`water_2d` is Cubey's first free-surface liquid project. It starts as a 2D
MAC-grid solver with a signed-distance level set surface, then leaves room for
PIC/FLIP/APIC particles once the grid projection and boundary behavior are
worth extending.

The project is deliberately separate from `smoke_2d`. Smoke uses a collocated
dye/velocity field; water uses face-centered velocity and a liquid/air
interface:

- `phi`: cell-centered signed distance; negative values are liquid.
- `pressure` and `divergence`: cell-centered scalar fields.
- `solid`: cell-centered obstacle/wall mask.
- `u`: x velocity on vertical cell faces, `(width + 1) * height`.
- `v`: y velocity on horizontal cell faces, `width * (height + 1)`.

## Current Target

The first implementation is a live 2D liquid sandbox:

```sh
./build/dev/projects/fluid/water_2d/water_2d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/water_2d/water_2d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-water-2d.png
./build/dev/projects/fluid/water_2d/water_2d --headless --debug-view phi --frames 120 --width 640 --height 360 --output /tmp/cubey-water-2d-phi.png
```

Controls:

- Space pauses/resumes.
- `R` resets the tank.
- `D` cycles debug views.

Debug views:

- `surface`: shaded water fill plus the `phi = 0` contour.
- `phi`: signed distance.
- `velocity`: face velocity sampled to pixels.
- `divergence`: projected-cell divergence.
- `pressure`: pressure solve output.
- `solid`: walls and optional obstacle mask.

## Solver Shape

Each frame applies gravity, advects face velocity, advects `phi`, reinitializes
the level set near the interface, computes liquid-cell divergence, solves
pressure with free-surface boundary conditions, then projects the face velocity.

This is not the final high-detail liquid path. Pure level sets are simple and
inspectable but lose volume and smooth thin features. The intended next step is
a PIC/FLIP layer that keeps liquid material detail on particles while retaining
the MAC grid for pressure, boundaries, and incompressibility.

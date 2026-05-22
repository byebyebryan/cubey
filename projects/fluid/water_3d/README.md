# Water 3D

`water_3d` is the first 3D liquid sandbox for Cubey. It uses the same particle-grid
direction as `water_2d`: particles carry water volume, a staggered MAC grid solves
velocity and pressure, and APIC is the default particle transfer mode.

The default renderer is now a minimal screen-space surface path: particles write
front depth and thickness into render-graph transients, a separable bilateral pass
smooths the surface, and a composite pass shades the water with Fresnel, absorption,
and a procedural environment. The old particle splats remain as an opaque debug
view. Marching cubes, mesh generation, foam, hose injection, and draining are still
deferred until the 3D solver and renderer contract are stable.

Useful render views:

- `surface`: default screen-space water surface.
- `particles`: camera-facing particle splats.
- `cells`: center slice occupancy.
- `velocity`: center slice velocity magnitude.
- `pressure`: center slice pressure.
- `solid`: tank boundary mask.
- `overpack`: particle bin pressure/overfill diagnostic.
- `surface-depth`: raw smoothed surface depth.
- `surface-thickness`: accumulated surface thickness.
- `surface-normals`: reconstructed screen-space normals.

Common runs:

```sh
./build/dev/projects/fluid/water_3d/water_3d
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 24 --output outputs/water-3d.png
./build/dev/projects/fluid/water_3d/water_3d --grid-width 48 --grid-height 48 --grid-depth 48
```

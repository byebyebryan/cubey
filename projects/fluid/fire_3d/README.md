# Fire 3D

`fire_3d` is the default presentation of the shared dense 3D pyro solver. The
app fixes the solver mode to fire, while `projects/fluid/sim/pyro_3d` owns the
volume resources, source model, combustion pass, shadow volume, and raymarch
renderer shared with `explosion_3d`. A configurable ball obstacle is embedded in
the volume so the plume has a solid object to flow around and shadow against.

By default, the raymarch and shadow passes consume the shared procedural
atmosphere for dynamic light direction, color, sky tint, and exposure; pass
`--pbr-environment-source static` for the legacy fixed-light fallback.

Useful controls:

- Left-drag: orbit camera.
- Mouse wheel: zoom.
- Space: pause or resume simulation.
- `R`: reset the volume.
- `D`: cycle smoke, density slice, and velocity debug views.
- Escape: close.
- The runtime UI groups simulation, source, fire model, obstacle, environment,
  rendering, shadow, and diagnostics controls. The Rendering group owns the
  local presentation style: exposure bias, backdrop, rim/scatter, smoke warmth,
  and flame shaping.

Useful commands:

```bash
./build/dev/projects/fluid/fire_3d/fire_3d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/fire_3d/fire_3d --frames 300 --pyro-sources 1 --pyro-source-radius 0.125 --pyro-fuel 4 --pyro-soot 16 --pyro-temperature 2.8
./build/dev/projects/fluid/fire_3d/fire_3d --frames 300 --pyro-obstacle-height 0.58 --pyro-obstacle-radius 0.105
./build/dev/projects/fluid/fire_3d/fire_3d --headless --debug-view density-slice --frames 120 --width 640 --height 360 --output /tmp/cubey-fire-3d-density.png
./build/dev/projects/fluid/fire_3d/fire_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-fire-3d.png
```

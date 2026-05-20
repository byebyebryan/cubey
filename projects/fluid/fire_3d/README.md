# Fire 3D

`fire_3d` is the default presentation of the shared dense 3D pyro solver. The
app fixes the solver mode to fire, while `projects/fluid/sim/pyro_3d` owns the
volume resources, source model, combustion pass, shadow volume, and raymarch
renderer shared with `explosion_3d`.

Useful controls:

- Left-drag: orbit camera.
- Mouse wheel: zoom.
- Space: pause or resume simulation.
- `R`: reset the volume.
- `D`: cycle smoke, density slice, and velocity debug views.
- Escape: close.

Useful commands:

```bash
./build/dev/projects/fluid/fire_3d/fire_3d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/fire_3d/fire_3d --frames 300 --pyro-sources 1 --pyro-source-radius 0.085 --pyro-fuel 2.5 --pyro-soot 6 --pyro-temperature 1.5
./build/dev/projects/fluid/fire_3d/fire_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-fire-3d.png
```

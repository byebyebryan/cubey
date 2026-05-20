# Explosion 3D

`explosion_3d` presents the shared dense 3D pyro solver as repeated impulse
bursts. It shares simulation, GPU resources, lighting, and raymarch rendering
with `fire_3d`, but fixes the app mode to explosion and exposes
explosion-specific interval, duration, and boost controls.

Useful controls:

- Left-drag: orbit camera.
- Mouse wheel: zoom.
- Space: pause or resume simulation.
- `R`: reset the volume.
- `D`: cycle smoke, density slice, and velocity debug views.
- Escape: close.

Useful commands:

```bash
./build/dev/projects/fluid/explosion_3d/explosion_3d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/explosion_3d/explosion_3d --frames 300 --pyro-sources 4 --pyro-source-radius 0.06 --explosion-interval 2.5 --explosion-duration 0.16 --explosion-boost 22
./build/dev/projects/fluid/explosion_3d/explosion_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-explosion-3d.png
```

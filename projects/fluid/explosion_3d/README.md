# Explosion 3D

`explosion_3d` presents the shared dense 3D pyro solver as repeated fireball
impulses. It shares simulation, GPU resources, lighting, and raymarch rendering
with `fire_3d`, but fixes the app mode to explosion. The source model stages a
short hot flash, an expanding smoke shell, and the buoyant plume that rises from
the injected heat and soot. A configurable ball obstacle is embedded in the
volume so the impulse can break around a solid shape.

Useful controls:

- Left-drag: orbit camera.
- Mouse wheel: zoom.
- Space: pause or resume simulation.
- `R`: reset the volume.
- `D`: cycle smoke, density slice, and velocity debug views.
- Escape: close.
- The runtime UI groups simulation, source, explosion model, obstacle,
  rendering, shadow, and diagnostics controls. The Rendering group owns the
  presentation style: exposure, backdrop, rim/scatter, smoke warmth, and flame
  shaping.

Useful commands:

```bash
./build/dev/projects/fluid/explosion_3d/explosion_3d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/explosion_3d/explosion_3d --frames 300 --pyro-sources 9 --pyro-source-radius 0.06 --explosion-interval 2.5 --explosion-duration 0.16 --explosion-boost 22
./build/dev/projects/fluid/explosion_3d/explosion_3d --frames 300 --pyro-obstacle-height 0.55 --pyro-obstacle-radius 0.18
./build/dev/projects/fluid/explosion_3d/explosion_3d --headless --debug-view velocity --frames 120 --width 640 --height 360 --output /tmp/cubey-explosion-3d-velocity.png
./build/dev/projects/fluid/explosion_3d/explosion_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-explosion-3d.png
```

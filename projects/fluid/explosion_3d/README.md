# Explosion 3D

`explosion_3d` presents the shared dense 3D pyro solver as repeated fireball
impulses. It shares simulation, GPU resources, lighting, and raymarch rendering
with `fire_3d`, but fixes the app mode to explosion. The source model stages a
hot flash, an expanding smoke shell, and the buoyant plume that rises from the
injected heat and soot. The obstacle is disabled by default, but a configurable
ball obstacle can be enabled when the impulse should break around a solid shape.

By default, the raymarch and shadow passes consume the shared procedural
atmosphere for dynamic light direction, color, sky tint, and exposure; pass
`--pbr-environment-source static` for the legacy fixed-light fallback.
The shared Pyro3D presentation also accepts
`--terrain-heightfield <field-or-directory>`. Terrain contributes HDR scene
color and depth before the explosion raymarch, so opaque terrain terminates the
volume correctly. Terrain requires the procedural atmosphere environment.

Useful controls:

- Left-drag: orbit camera.
- Mouse wheel: zoom.
- Space: pause or resume simulation.
- `R`: reset the volume.
- `D`: cycle smoke, density slice, and velocity debug views.
- Escape: close.
- The runtime UI groups simulation, source, explosion model, obstacle,
  environment, rendering, shadow, and diagnostics controls. The Rendering group
  owns the local presentation style: exposure bias, backdrop, rim/scatter, smoke
  warmth, and flame shaping.

Useful commands:

```bash
./build/dev/projects/fluid/explosion_3d/explosion_3d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/explosion_3d/explosion_3d --frames 300 --pyro-sources 9 --pyro-source-height 0.10 --pyro-source-radius 0.025 --explosion-interval 2.5 --explosion-duration 0.5 --explosion-boost 20
./build/dev/projects/fluid/explosion_3d/explosion_3d --frames 300 --pyro-obstacle-height 0.55 --pyro-obstacle-radius 0.18
./build/dev/projects/fluid/explosion_3d/explosion_3d --headless --debug-view velocity --frames 120 --width 640 --height 360 --output /tmp/cubey-explosion-3d-velocity.png
./build/dev/projects/fluid/explosion_3d/explosion_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-explosion-3d.png
./build/dev/projects/fluid/explosion_3d/explosion_3d --headless --capture video --frames 240 --fps 30 --capture-video-orbit-degrees 0 --capture-camera-distance 2.1 --output /tmp/cubey-explosion-3d.mp4
./build/dev/projects/fluid/explosion_3d/explosion_3d --terrain-heightfield cache/terrain/sources/v1/default
```

For headless video, `--capture-video-orbit-degrees N` authors an eased bounded
move over the complete frame range; use `0` for a fixed camera or omit the
option to preserve the historical automatic orbit. `--capture-camera-distance`
overrides the absolute capture framing. These controls are owned by the shared
Pyro 3D project facade used by both Fire and Explosion.

# Fire 3D

`fire_3d` is the default presentation of the shared dense 3D pyro solver. The
app fixes the solver mode to fire, while `projects/fluid/sim/pyro_3d` owns the
volume resources, source model, combustion pass, shadow volume, and raymarch
renderer shared with `explosion_3d`. A configurable ball obstacle is embedded in
the volume so the plume has a solid object to flow around and shadow against.

By default, the raymarch and shadow passes consume the shared procedural
atmosphere for dynamic light direction, color, sky tint, and exposure; pass
`--pbr-environment-source static` for the legacy fixed-light fallback.
Pass `--terrain-heightfield <field-or-directory>` to render the shared terrain
behind the volume. The atmosphere, moon, and terrain first produce HDR scene
color and depth; the fire raymarch then stops at that depth and composites once
in linear space. Terrain requires the procedural atmosphere environment.

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
./build/dev/projects/fluid/fire_3d/fire_3d --headless --capture video --frames 480 --fps 60 --capture-video-orbit-degrees 0 --capture-camera-distance 2.1 --output /tmp/cubey-fire-3d.mp4
./build/dev/projects/fluid/fire_3d/fire_3d --terrain-heightfield cache/terrain/sources/v1/default
```

## Showcase highlight

[![Fire 3D showcase poster](../../../docs/media/showcase/fire-3d.png)](../../../docs/media/showcase/fire-3d.mp4)

The committed highlight uses one source, no obstacle, no terrain or clouds, a
fixed camera at distance `1.65`, three seconds of source warm-up, and the
15:30-to-22:30 retained window. Its Balanced simulation overrides are
`source-radius 0.11`, `soot 13`, `soot-yield 0.35`, `expansion 1.10`,
`flame-cooling 3.4`, `shredding 4.2`, `turbulence 1.2`, `buoyancy 2.1`, and
`source-force 8.0`. The source capture is:

```bash
./build/dev/projects/fluid/fire_3d/fire_3d --headless --capture video --frames 660 --fps 60 --width 1280 --height 720 --capture-video-orbit-degrees 0 --capture-camera-distance 1.65 --pyro-sources 1 --pyro-obstacle-radius 0 --time-of-day-mode solar --time-hours 12.875 --time-speed-hours-per-second 0.875 --pyro-source-radius 0.11 --pyro-soot 13 --pyro-soot-yield 0.35 --pyro-expansion 1.10 --pyro-flame-cooling 3.4 --pyro-shredding 4.2 --pyro-turbulence 1.2 --pyro-buoyancy 2.1 --pyro-source-force 8.0 --output outputs/showcase/audition-2/pyro/fire-3d-refine-sim-balanced-source.mp4
```

The frame-180 trim, publication command, exact hash, and poster timestamp are
recorded in the [showcase media manifest](../../../docs/media/showcase/manifest.json).

For headless video, `--capture-video-orbit-degrees N` authors an eased bounded
move over the complete frame range; use `0` for a fixed camera or omit the
option to preserve the historical automatic orbit. `--capture-camera-distance`
overrides the absolute capture framing. The same controls are shared with
`explosion_3d`.

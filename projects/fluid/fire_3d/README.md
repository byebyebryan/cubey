# Fire 3D

`fire_3d` is the default presentation of the shared dense 3D pyro solver. The
app fixes the solver mode to fire, while `projects/fluid/sim/pyro_3d` owns the
volume resources, source model, combustion pass, shadow volume, and raymarch
renderer shared with `explosion_3d`. A configurable ball obstacle is embedded in
the volume so the plume has a solid object to flow around and shadow against.
The current visual overview is the
[Fire 3D page](https://dev.byebyebryan.com/cubey/demos/simulation/fire-3d/);
this guide owns the native simulation and renderer behavior.

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

The Simulation panel offers two independent options. **Half-resolution flow**
restarts the plume with velocity, divergence and pressure at half the material
resolution along each axis. Material dimensions must be even. **Skip empty ray
segments** updates an 8³-voxel brick mask and skips empty space along camera
and shadow rays. It can be toggled while the plume is paused or moving.
The original cube, full flow and dense rays are the startup defaults.

Material stores soot, heat, flame and fuel in RGBA16F. Velocity uses XYZ in
another RGBA16F texture. Divergence, pressure, shadow transmittance and the brick
mask use R32F. Shader arithmetic uses 32-bit floats.

For the tall, open-top torch with 256 × 512 × 256 material, 128 × 256 × 128
flow and 64 × 128 × 64 shadows, run:

```bash
./build/dev/projects/fluid/fire_3d/fire_3d --pyro-tall-domain --pyro-coarse-flow --pyro-ray-skipping \
    --grid-width 256 --grid-height 512 --grid-depth 256 --pyro-obstacle-radius 0
```

To reproduce the seven-second high-detail comparison, use the Balanced torch
settings and a fixed sunset camera:

```bash
./build/dev/projects/fluid/fire_3d/fire_3d --headless --capture video --frames 420 --fps 60 \
    --width 1280 --height 720 --anti-aliasing off --capture-video-orbit-degrees 0 \
    --capture-camera-distance 2.75 --time-of-day-mode solar --time-hours 17.72 \
    --time-speed-hours-per-second 0 --pyro-tall-domain --pyro-coarse-flow --pyro-ray-skipping \
    --grid-width 256 --grid-height 512 --grid-depth 256 --shadow-grid-height 128 \
    --pyro-sources 1 --pyro-obstacle-radius 0 --pyro-source-radius 0.11 --pyro-soot 13 \
    --pyro-soot-yield 0.35 --pyro-expansion 1.10 --pyro-flame-cooling 3.4 --pyro-shredding 4.2 \
    --pyro-turbulence 1.2 --pyro-buoyancy 2.1 --pyro-source-force 8.0 --output /tmp/fire-3d-tall.mp4
```

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

The current site page above presents the recorded fire-and-smoke clip. Its
trim, hash and poster timestamp remain in the [showcase archive package](../../../docs/media/showcase/README.md)
and [media manifest](../../../docs/media/showcase/manifest.json); the exact
source command is retained in the [capture history](docs/showcase-history.md).

For headless video, `--capture-video-orbit-degrees N` authors an eased bounded
move over the complete frame range; use `0` for a fixed camera or omit the
option to preserve the historical automatic orbit. `--capture-camera-distance`
overrides the absolute capture framing. The same controls are shared with
`explosion_3d`.

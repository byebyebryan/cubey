# Fire 3D

`fire_3d` is the default presentation of the shared dense 3D pyro solver. The
app fixes the solver mode to fire, while `projects/fluid/sim/pyro_3d` owns the
volume resources, source model, combustion pass, shadow volume, and raymarch
renderer shared with `explosion_3d`. The Rolling burner is the default Fire 3D
scene. Its two overlapping fuel feeds take turns rising into a common smoke plume.
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

The **Burner** menu restarts the plume with the selected source, combustion
and emission treatment. Reset replays the selected burner. The same choice is
available as `--fire-burner` or `pyro.fire_burner` in a config file.

| Burner | Native behavior |
| --- | --- |
| `rolling` (default) | Two overlapping feeds trade height. Fuel-rich pockets survive longer and the amber raymarch brings out their edges. |
| `compact-draft` | A tighter burner feeds a passing draft above the flame from 4.0 to 6.1 seconds of simulation time. Pausing holds the draft. |
| `classic` | The original single feed, emission curve and ball obstacle. |

The burner menu keeps the current grid and camera. At startup, Rolling and
Compact use an open-top 128 × 256 × 128 domain. Classic starts in the original
128³ cube. Individual startup options override the preset. For example,
`--set pyro.tall_domain=false` runs Rolling in the cube.

Rolling and Compact follow solver time. A headless video takes one simulation
step per output frame, capped at 1/60 second. At 30 fps, four seconds of
simulation therefore span eight seconds of video. The showcase records at
60 fps and keeps alternate frames to deliver the motion at its original speed.
The solar clock follows video time independently.

For Rolling and Compact, enable **Rendering → Flame lights smoke** or launch
with `--pyro-firelight` to see the underside of the plume catch light from the
flame. The renderer samples nearby emissive cells below each smoke sample and
attenuates their light through intervening soot. The checkbox also works while
paused. Firelight starts off by default.

The Simulation panel offers two independent options. **Half-resolution flow**
restarts the plume with velocity, divergence and pressure at half the material
resolution along each axis. Material dimensions must be even. **Skip empty ray
segments** updates an 8³-voxel brick mask and skips empty space along camera
and shadow rays. It can be toggled while the plume is paused or moving.
The interactive defaults use full-resolution flow and dense rays.

Material stores soot, heat, flame and fuel in RGBA16F. Velocity uses XYZ in
another RGBA16F texture. Divergence, pressure, shadow transmittance and the brick
mask use R32F. Shader arithmetic uses 32-bit floats.

For the tall, open-top torch with 256 × 512 × 256 material, 128 × 256 × 128
flow and 64 × 128 × 64 shadows, run:

```bash
./build/dev/projects/fluid/fire_3d/fire_3d --pyro-tall-domain --pyro-coarse-flow --pyro-ray-skipping \
    --grid-width 256 --grid-height 512 --grid-depth 256 --pyro-obstacle-radius 0
```

The site capture uses the Rolling burner at 256 × 512 × 256 with half-resolution
flow, empty-space skipping and local firelight. The sun holds at 18:00 while
the camera moves closer to the flame. The [capture record](docs/showcase-history.md)
includes the native command, camera edit and poster selection.

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

The [Rolling capture receipt](../../../site/media/receipts/fire-3d-rolling.json) records the master, poster and web film. The [showcase archive](../../../docs/media/showcase/README.md) retains the earlier Balanced clip.

For headless video, `--capture-video-orbit-degrees N` authors an eased bounded
move over the complete frame range; use `0` for a fixed camera or omit the
option to preserve the historical automatic orbit. `--capture-camera-distance`
sets the headless camera distance. `--capture-target-height` sets its orbit
target in world units above the volume floor. Both controls apply to stills and
films. For the tighter Rolling shot, use distance `2.04` and target height
`1.03`. The same controls are shared with `explosion_3d`.

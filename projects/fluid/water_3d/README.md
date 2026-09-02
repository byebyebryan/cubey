# Water 3D

`water_3d` is the first 3D liquid sandbox for Cubey. It uses the same particle-grid
direction as `water_2d`: particles carry water volume, a staggered MAC grid solves
velocity and pressure, and APIC is the default particle transfer mode.

The default scene is a long showcase tank. A left-biased dam break seeds the
initial volume, and a horizontal wave driver adds low-frequency motion. Hose,
suction drain, and rain emission are available from the UI but start disabled.
The solver still runs in normalized `[0,1]^3` coordinates; the long tank is a
render-domain scale rather than a full anisotropic-cell rewrite.

Rain particles use the same particle-grid solver path as water, but they carry a
separate render state so falling droplets draw smaller than bulk liquid particles.

The default renderer is a screen-space surface path: the selected environment and
simple ground scene are rendered into offscreen color/depth targets, particles
write front depth and thickness into render-graph transients, a separable
bilateral pass repairs small holes and runs configurable depth-aware smoothing,
and a composite pass shades the water with Fresnel, scene-color refraction,
environment reflection, Beer-Lambert absorption, a heuristic screen-space foam
mask, and visual-only secondary whitewater spray/foam particles. The default
environment source is the shared procedural atmosphere, including dynamic
reflection-probe updates, environment lighting, exposure, and nested Environment
controls. Its atmosphere-owned surface clouds are composited into the HDR scene
before water refraction, and the filtered cloud environment feeds broad water
reflection through the existing PBR bindings. The shared Cloud Environment
panel and `clouds.*` config/CLI options drive both products. Pass `--no-clouds`
for a clear procedural A/B, or `--pbr-environment-source static` to use the
generated/HDR IBL fallback without procedural clouds. The old particle splats
remain as an opaque debug view. SSR,
anisotropic particle kernels, marching cubes, mesh generation, and richer
collision are still deferred until the 3D solver and renderer contract are
stable.

Pass `--terrain-heightfield <field-or-directory>` to add the shared terrain
backdrop behind the simulation. Terrain writes the existing scene color and
depth before cloud composition, so the water surface refracts it and remains
correctly occluded. No project-owned floor is rendered; terrain and atmosphere
provide the complete surrounding scene. Terrain requires the procedural
atmosphere environment.

Useful render views:

- `surface`: default screen-space water surface.
- `particles`: camera-facing particle splats.
- `cells`: center slice occupancy.
- `velocity`: center slice velocity magnitude.
- `pressure`: center slice pressure.
- `solid`: tank boundary mask.
- `overpack`: particle bin pressure/overfill diagnostic.
- `surface-depth`: repaired and smoothed surface depth.
- `surface-thickness`: accumulated surface thickness.
- `surface-normals`: reconstructed screen-space normals.
- `surface-foam`: screen-space foam mask.
- `whitewater`: secondary whitewater particles over a dimmed scene.

Common runs:

```sh
./build/dev/projects/fluid/water_3d/water_3d
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 24 --output outputs/water-3d.png
./build/dev/projects/fluid/water_3d/water_3d --headless --capture video --frames 480 --fps 60 --capture-camera-distance 2.5 --capture-video-orbit-degrees 30 --output outputs/water-3d.mp4
./build/dev/projects/fluid/water_3d/water_3d --grid-width 48 --grid-height 48 --grid-depth 48
./build/dev/projects/fluid/water_3d/water_3d --grid-width 128 --grid-height 64 --grid-depth 48
./build/dev/projects/fluid/water_3d/water_3d --environment build/dev/_deps/cubey_hdr_sample_assets-src/venetian_crossroads_2k.hdr
./build/dev/projects/fluid/water_3d/water_3d --grid-width 64 --grid-height 64 --grid-depth 64 --frames 240 --profile-output water3d-64 --profile-warmup-frames 60
./build/dev/projects/fluid/water_3d/water_3d --headless --grid-width 64 --grid-height 64 --grid-depth 64 --frames 120 --profile-output water3d-64-diagnostics --profile-diagnostics --profile-diagnostic-interval 4 --no-validation
./build/dev/projects/fluid/water_3d/water_3d --water3d-transfer pic-flip --water3d-transfer-limit 96 --no-water3d-wave
./build/dev/projects/fluid/water_3d/water_3d --water3d-hose --water3d-drain --water3d-rain --no-water3d-whitewater
./build/dev/projects/fluid/water_3d/water_3d --no-clouds
./build/dev/projects/fluid/water_3d/water_3d --pbr-environment-source static
./build/dev/projects/fluid/water_3d/water_3d --terrain-heightfield cache/terrain/sources/v1/default --terrain-foreground-height 5
```

## Showcase highlight

[![Water 3D showcase poster](../../../docs/media/showcase/water-3d.png)](../../../docs/media/showcase/water-3d.mp4)

The highlight follows one substantial dam break from collapse through impact
and rebound. Wave forcing is disabled so the motion comes entirely from the
initial volume; the clear sky and close three-quarter view keep the reconstructed
surface, foam, and whitewater readable.

The exact capture uses initial fill `0.60/0.75/0.75`, whitewater intensity
`1.35`, speed threshold `0.85`, a 0.5-second pre-roll, camera distance `2.2`,
and a 30-degree eased arc. It retains frames 30:510 from this source:

```sh
./build/dev/projects/fluid/water_3d/water_3d --headless --capture video --frames 510 --fps 60 --width 1280 --height 720 --capture-camera-distance 2.2 --capture-video-orbit-degrees 30 --time-of-day-mode solar --time-hours 13.5625 --time-speed-hours-per-second 0.875 --no-clouds --cloud-weather-preset broken-cumulus --cloud-quality full --no-water3d-wave --water3d-whitewater --water3d-whitewater-intensity 1.35 --water3d-whitewater-speed-threshold 0.85 --water3d-initial-fill-width 0.60 --water3d-initial-fill-height 0.75 --water3d-initial-fill-depth 0.75 --output outputs/showcase/audition-2/water/water-3d-refine-dam-only-fill-60-75-75-no-clouds-preroll-source.mp4
```

The retained and publication trim commands, exact hash, and poster timestamp
are recorded in the [showcase media manifest](../../../docs/media/showcase/manifest.json).

Interactive controls include fill size/placement, render-domain scale, hose,
drain, wave, rain, solver settings, surface reconstruction, whitewater, and
frame/GPU-memory diagnostics. Procedural mode also exposes the shared atmosphere
and Cloud V1 controls; static environment mode hides those controls.

Water-specific CLI controls:

- `--capture-video-orbit-degrees N`: author an eased, bounded headless-video
  orbit over the complete frame range. Use `0` for a fixed camera; omit the
  option to preserve the historical automatic video orbit.
- `--capture-camera-distance N`: override the absolute headless capture-camera
  distance without changing the normal windowed camera.
- `--water3d-transfer apic|pic-flip`: select APIC or PIC/FLIP grid-to-particle
  transfer.
- `--water3d-transfer-limit N`: cap sorted particle samples consumed per cell by
  P2G; raw cell occupancy is still tracked for overpack diagnostics and volume
  correction.
- `--water3d-p2g-mode active|tiled`: choose the default active-face P2G path or
  the opt-in tiled profiling path.
- `--water3d-hose`, `--no-water3d-hose`, `--water3d-drain`,
  `--no-water3d-drain`, `--water3d-rain`, `--no-water3d-rain`,
  `--water3d-wave`, `--no-water3d-wave`, `--water3d-whitewater`, and
  `--no-water3d-whitewater`: override the runtime defaults for optional flow,
  forcing, and visual secondary-particle systems.

Profiling writes frame, pass, metric CSVs, Chrome trace JSON, and a text summary
under `outputs/profiles/` when the prefix has no directory component.
`--profile-diagnostics` is headless-only and opt-in because it adds
readback-oriented diagnostic passes for workload, P2G scan/histograms, solver
residual, transfer truncation, liquid/rain split, and whitewater counters. Use
`--profile-diagnostic-interval N` to sample the diagnostic passes and readbacks
less frequently during longer captures.

The particle-grid transfer path keeps canonical particle positions, velocities,
and APIC affine rows stable. Each bin refresh writes a compact
`sorted_particle_indices` range per occupied cell, and P2G/diagnostics
dereference those IDs instead of shuffling particle payload buffers.

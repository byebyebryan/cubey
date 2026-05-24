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

The default renderer is a screen-space surface path: an HDR/generated environment
and simple ground scene are rendered into offscreen color/depth targets, particles
write front depth and thickness into render-graph transients, a separable bilateral
pass repairs small holes and runs configurable depth-aware smoothing, and a composite
pass shades the water with Fresnel, scene-color refraction, environment reflection,
Beer-Lambert absorption, a heuristic screen-space foam mask, and visual-only
secondary whitewater spray/foam particles. The old particle splats remain as an
opaque debug view. SSR, anisotropic particle kernels, marching cubes, mesh
generation, and richer collision are still deferred until the 3D solver and
renderer contract are stable.

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
./build/dev/projects/fluid/water_3d/water_3d --grid-width 48 --grid-height 48 --grid-depth 48
./build/dev/projects/fluid/water_3d/water_3d --grid-width 128 --grid-height 64 --grid-depth 48
./build/dev/projects/fluid/water_3d/water_3d --environment build/dev/_deps/cubey_hdr_sample_assets-src/venetian_crossroads_2k.hdr
./build/dev/projects/fluid/water_3d/water_3d --grid-width 64 --grid-height 64 --grid-depth 64 --frames 240 --profile-output water3d-64 --profile-warmup-frames 60
./build/dev/projects/fluid/water_3d/water_3d --headless --grid-width 64 --grid-height 64 --grid-depth 64 --frames 120 --profile-output water3d-64-diagnostics --profile-diagnostics --profile-diagnostic-interval 4 --no-validation
./build/dev/projects/fluid/water_3d/water_3d --water3d-transfer pic-flip --water3d-transfer-limit 96 --no-water3d-wave
./build/dev/projects/fluid/water_3d/water_3d --water3d-hose --water3d-drain --water3d-rain --no-water3d-whitewater
```

Interactive controls include fill size/placement, render-domain scale, hose,
drain, wave, rain, solver settings, surface reconstruction, whitewater, and
frame/GPU-memory diagnostics.

Water-specific CLI controls:

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

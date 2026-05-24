# Water 3D

`water_3d` is the first 3D liquid sandbox for Cubey. It uses the same particle-grid
direction as `water_2d`: particles carry water volume, a staggered MAC grid solves
velocity and pressure, and APIC is the default particle transfer mode.

The default scene is a long showcase tank. A left-biased dam break seeds the
initial volume, a hose and drain keep the simulation alive after the first
splash, and a mild wave driver adds low-frequency motion. Rain emission is
available from the UI but starts disabled. The solver still runs in normalized
`[0,1]^3` coordinates; the long tank is a render-domain scale rather than a
full anisotropic-cell rewrite.

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
./build/dev/projects/fluid/water_3d/water_3d --frames 240 --profile-output water3d-64 --profile-warmup-frames 60
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 120 --profile-output water3d-64-diagnostics --profile-diagnostics --profile-diagnostic-interval 4 --no-validation
```

Interactive controls include fill size/placement, render-domain scale, hose,
drain, wave, rain, solver settings, surface reconstruction, whitewater, and
frame/GPU-memory diagnostics.

Profiling writes frame, pass, metric CSVs, Chrome trace JSON, and a text summary
under `outputs/profiles/` when the prefix has no directory component.
`--profile-diagnostics` is opt-in because it adds readback-oriented diagnostic
passes for workload, P2G scan/histograms, solver residual, and whitewater
counters. Use `--profile-diagnostic-interval N` to sample the diagnostic passes
and readbacks less frequently during longer captures.

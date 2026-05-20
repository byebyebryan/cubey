# Fluid 3D

`fluid_3d` is Cubey's first dense 3D smoke project. It keeps the initial scope
deliberately small: 3D storage textures, MacCormack advection/injection, Jacobi
pressure projection, vorticity confinement, and a lit fullscreen raymarcher with
precomputed volume self-shadowing.

The project is intentionally dense-grid first. Sparse bricks, tiled allocation,
scene lighting integration, and proper volume light lists are later steps; this
slice is meant to prove the volume texture, command recording, headless capture,
and UI/control shape before the solver becomes more ambitious.

## Controls

- Left-drag orbits the camera.
- Space pauses/resumes simulation.
- `R` resets the volume and active sources.
- `D` cycles smoke, density-slice, and velocity debug views.

The UI exposes the source scenario, source count, pressure iterations, raymarch
steps, decay, source radius, smoke/heat amounts, velocity force, buoyancy,
vorticity, absorption, light, shadow strength, shadow ray steps/update interval,
ambient terms, and recent GPU pass timings.

## CLI

```bash
./build/dev/projects/fluid_3d/fluid_3d --width 1280 --height 720
./build/dev/projects/fluid_3d/fluid_3d --fluid-scenario smoke-plume --fluid-sources 6
./build/dev/projects/fluid_3d/fluid_3d --fluid-scenario explosion --fluid-explosion-interval 3.0 --fluid-explosion-duration 0.12 --fluid-explosion-boost 18
./build/dev/projects/fluid_3d/fluid_3d --fluid-smoke 7.0 --fluid-heat 1.8 --fluid-source-force 8.0 --fluid-buoyancy 1.5
./build/dev/projects/fluid_3d/fluid_3d --shadow-grid-width 64 --shadow-grid-height 64 --shadow-grid-depth 64 --shadow-steps 64 --shadow-update-interval 1
./build/dev/projects/fluid_3d/fluid_3d --frames 300 --print-frame-stats --width 1280 --height 720
./build/dev/projects/fluid_3d/fluid_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-3d.png
```

The default solver grid is `128x128x128`. The default shadow grid is decoupled
at `64x64x64`, with 64 shadow ray steps and per-frame shadow updates. The CLI
grid flags are useful for quick smoke tests (`32x32x32`) and higher-quality
local runs once performance allows. Shadow grid changes are startup-time
resource choices; shadow steps and update interval are also exposed live in UI.
The default `smoke-plume` scenario uses fixed low sources that inject smoke,
heat, and upward velocity. `explosion` emits a short boosted smoke/heat/flame
and radial velocity impulse, then pauses until the next interval. The density
volume channels are semantic material channels rather than display color:
`r = smoke/soot`, `g = heat`, `b = flame`, and `a = reserved`. Smoke/heat/flame
injection and velocity force are controlled independently, and the solver
applies material-weighted upward buoyancy during the correction/injection pass.

## Current Pipeline

The simulation uses `RGBA16F` 3D textures for material/velocity fields and prediction
scratch, plus `R32F` scalar volumes for divergence, pressure, and shadow
transmittance:

- Density A/B.
- Velocity A/B.
- Density/velocity prediction scratch.
- Divergence.
- Pressure A/B.
- Shadow transmittance on the decoupled shadow grid.

The command path records:

1. One-time image layout transition to `VK_IMAGE_LAYOUT_GENERAL`.
2. Reset pass when requested.
3. Semi-Lagrangian advection prediction pass.
4. MacCormack/BFECC correction, limiter, cleanup, procedural injection, and
   buoyancy.
5. Divergence pass.
6. Jacobi pressure ping-pong.
7. Projection plus vorticity confinement.
8. Shadow-volume compute pass from the current density volume.
9. Raymarched fullscreen draw sampling the precomputed shadow volume.

Windowed runs collect GPU timestamps for reset, advection prediction/correction,
divergence, pressure, projection, shadow, and raymarch passes. The timings are
shown in the UI and emitted once per second with `--print-frame-stats`.

The project currently uses direct command recording rather than the render
graph. That keeps the first 3D volume path easy to inspect; the render graph can
absorb it later once volume resources and async compute policy have more pressure.

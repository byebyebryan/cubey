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
- `R` resets the volume and procedural injectors.
- `D` cycles smoke, density-slice, and velocity debug views.

The UI exposes injector count, pressure iterations, raymarch steps, decay,
injection radius, density injection, velocity force, propulsion, buoyancy,
movement type, orbit/circle controls, vorticity, absorption, light, shadow
strength, shadow ray steps/update interval, ambient terms, and recent GPU pass
timings.

## CLI

```bash
./build/dev/projects/fluid_3d/fluid_3d --width 1280 --height 720
./build/dev/projects/fluid_3d/fluid_3d --grid-width 64 --grid-height 64 --grid-depth 64 --injectors 8
./build/dev/projects/fluid_3d/fluid_3d --injectors 8 --injector-orbit-radius 0.24 --injector-orbit-angular-speed 0.0 --injector-orbit-angular-speed-spread 1.2 --injector-orbit-inclination-degrees 0 --injector-orbit-inclination-spread-degrees 70
./build/dev/projects/fluid_3d/fluid_3d --injector-movement circle --injector-circle-height 0.62 --injectors 8
./build/dev/projects/fluid_3d/fluid_3d --fluid-density-injection 7.0 --fluid-buoyancy 1.5
./build/dev/projects/fluid_3d/fluid_3d --shadow-grid-width 64 --shadow-grid-height 64 --shadow-grid-depth 64 --shadow-steps 64 --shadow-update-interval 1
./build/dev/projects/fluid_3d/fluid_3d --frames 300 --print-frame-stats --width 1280 --height 720
./build/dev/projects/fluid_3d/fluid_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-3d.png
```

The default solver grid is `128x128x128`. The default shadow grid is decoupled
at `64x64x64`, with 64 shadow ray steps and per-frame shadow updates. The CLI
grid flags are useful for quick smoke tests (`32x32x32`) and higher-quality
local runs once performance allows. Shadow grid changes are startup-time
resource choices; shadow steps and update interval are also exposed live in UI.
Procedural injectors follow moving targets with spring/damping physics rather
than teleporting directly onto parametric paths. `orbit` uses tilted 3D paths;
`circle` uses a horizontal X/Z path at configurable normalized Y height. The
injected velocity combines source carry velocity with an opposite-direction
propulsion term.
Density injection and velocity force are controlled independently, and the
solver applies density-weighted upward buoyancy during the correction/injection
pass.

## Current Pipeline

The simulation uses `RGBA16F` 3D textures for dye/velocity fields and prediction
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

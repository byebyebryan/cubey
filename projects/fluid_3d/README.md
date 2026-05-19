# Fluid 3D

`fluid_3d` is Cubey's first dense 3D smoke project. It keeps the initial scope
deliberately small: 3D storage textures, compute advection/injection, Jacobi
pressure projection, vorticity confinement, and a fullscreen raymarcher.

The project is intentionally dense-grid first. Sparse bricks, tiled allocation,
lighting, shadowing, and scene integration are later steps; this slice is meant
to prove the volume texture, command recording, headless capture, and UI/control
shape before the solver becomes more ambitious.

## Controls

- Left-drag orbits the camera.
- Space pauses/resumes simulation.
- `R` resets the volume and procedural injectors.
- `D` cycles smoke, density-slice, and velocity debug views.

The UI exposes injector count, pressure iterations, raymarch steps, decay,
injection radius/force, vorticity, absorption, and emission.

## CLI

```bash
./build/dev/projects/fluid_3d/fluid_3d --width 1280 --height 720
./build/dev/projects/fluid_3d/fluid_3d --grid-width 64 --grid-height 64 --grid-depth 64 --injectors 8
./build/dev/projects/fluid_3d/fluid_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-fluid-3d.png
```

The default solver grid is `96x96x96`. The CLI grid flags are useful for quick
smoke tests (`32x32x32`) and higher-quality local runs once performance allows.

## Current Pipeline

The simulation uses seven `RGBA32F` 3D textures:

- Density A/B.
- Velocity A/B.
- Divergence.
- Pressure A/B.

The command path records:

1. One-time image layout transition to `VK_IMAGE_LAYOUT_GENERAL`.
2. Reset pass when requested.
3. Advection and procedural injector pass.
4. Divergence pass.
5. Jacobi pressure ping-pong.
6. Projection plus vorticity confinement.
7. Raymarched fullscreen draw.

The project currently uses direct command recording rather than the render
graph. That keeps the first 3D volume path easy to inspect; the render graph can
absorb it later once volume resources and async compute policy have more pressure.

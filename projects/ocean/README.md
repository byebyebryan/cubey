# Ocean

`ocean` is a practical water-rendering project. It is deliberately separate from
the particle-grid `water_3d` tank: this demo starts from a camera-relative
surface renderer, not from a CFD solver.

The v1 target is scale. The surface follows the camera, evaluates a
multi-cascade spectral wave field in world space, fades short wavelengths with
distance, and blends the far mesh into the sky so the ocean can read all the way
to the horizon without obvious texture repetition.

Implemented:

- camera-relative projected grid generated in the vertex shader;
- in-repo Stockham FFT path for three spectral ocean cascades;
- texture-sampled displacement, normals, and generated crest foam;
- distance-filtered wave detail to reduce horizon aliasing;
- Fresnel sky reflection, fake scene refraction, Beer-style absorption, crest
  and shallow-water foam, exposure/tonemap handling, and debug views;
- first-class future hooks for local disturbances and shoreline/bathymetry
  masks, disabled by default so the baseline reads as wind-driven ocean.

Deferred:

- real scene color/depth refraction against arbitrary geometry;
- boats, buoyancy, wake databases, shoreline authoring, caustics, and underwater
  rendering;
- physically meaningful breaking waves.

Breaking waves are intentionally not part of v1. Deep-water whitecaps can be
approximated from crest steepness and foam, which is what this project does now.
Plunging shore breakers are a different class of problem: they need terrain or
bathymetry, surf-zone wave transformation, local nonlinear simulation or
particles, and strong scene interaction. The renderer keeps the hooks, but the
first milestone keeps the water surface scalable and readable.

Useful runs:

```sh
./build/dev/projects/ocean/ocean --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --debug-view foam --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-ocean.png
./build/dev/projects/ocean/ocean --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-ocean.mp4
```

Controls:

- Left-drag: orbit camera.
- Mouse wheel: zoom.
- Space: pause or resume wave time.
- `R`: reset camera and wave time.
- `D`: cycle final, height, displacement, normal, foam, reflection,
  refraction, and spectrum debug views.
- Escape: close.

Primary references:

- Jerry Tessendorf, "Simulating Ocean Water".
- GPU Gems Chapter 1, "Effective Water Simulation from Physical Models".
- GPU Gems 2 Chapter 19, "Generic Refraction Simulation".
- Unreal Water System and Unity HDRP Water System public docs for water-system
  scope and production tradeoffs.

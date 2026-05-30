# Atmosphere

`atmosphere` is a standalone clear-sky scattering demo. It proves the sky model
before the ocean renderer consumes it for sky, reflection, sun lighting, and
horizon aerial perspective.

The first implementation is direct ray-marched single scattering with Rayleigh
air scattering, Mie aerosol scattering, ozone absorption, transmittance, a sun
disk, and debug views. LUT-backed production rendering is intentionally deferred.

Low-sun planet shadowing uses a softened solar-disk visibility term. The
transition is deliberately wider than the physical sun radius so sunrise and
sunset remain inspectable before multiple-scattering LUTs exist.

Useful runs:

```sh
./build/dev/projects/atmosphere/atmosphere --headless --output /tmp/cubey-atmosphere.png
./build/dev/projects/atmosphere/atmosphere --headless --debug-view transmittance --output /tmp/cubey-atmosphere-transmittance.png
./build/dev/projects/atmosphere/atmosphere --headless --atmosphere-preset sunset --output /tmp/cubey-atmosphere-sunset.png
./build/dev/projects/atmosphere/atmosphere --headless --capture video --frames 120 --output /tmp/cubey-atmosphere.mp4
```

Controls:

- Left-drag: rotate view direction.
- `R`: reset to the active preset.
- `D`: cycle final, rayleigh, mie, transmittance, optical-depth, sun-disk, and
  aerial-perspective debug views.
- The Reference panel controls the ground grid, local red/cyan axes, and origin
  marker used for orientation.
- Escape: close.

# Atmosphere

`atmosphere` is a standalone clear-sky scattering demo. It proves the sky model
before the ocean renderer consumes it for sky, reflection, sun lighting, and
horizon aerial perspective.

The first implementation is direct ray-marched single scattering with Rayleigh
air scattering, Mie aerosol scattering, ozone absorption, transmittance, a sun
disk, and debug views. LUT-backed production rendering is intentionally deferred.

Low-sun planet shadowing uses a softened solar-disk visibility term. The
transition is deliberately wider than the physical sun radius so sunrise and
sunset remain inspectable before multiple-scattering LUTs exist. Time of day is
resolved on the CPU from local solar time, day of year, and latitude, with manual
sun direction still available for art/debug work.

Night rendering includes procedural foreground stars, a moon disk with a
generated lunar atlas, and a procedural Milky Way atlas by default. Explicit
`data` or `auto` source selection can use NASA SVS Deep Star Maps
`starmap_8k.jpg` when `CUBEY_FETCH_MILKY_WAY_ASSETS=ON` or
`CUBEY_MILKY_WAY_ASSETS_DIR` points at a local copy. The optional fetch also
keeps the earlier NOAA/Mellinger `2048.jpg` panorama as a fallback/reference.
Sources:
https://svs.gsfc.nasa.gov/3895, https://svs.gsfc.nasa.gov/4851,
https://sos.noaa.gov/catalog/datasets/milky-way-panorama/, and
https://arxiv.org/abs/0908.4360.

Useful runs:

```sh
./build/dev/projects/atmosphere/atmosphere --headless --output /tmp/cubey-atmosphere.png
./build/dev/projects/atmosphere/atmosphere --headless --debug-view transmittance --output /tmp/cubey-atmosphere-transmittance.png
./build/dev/projects/atmosphere/atmosphere --headless --debug-view moon-surface --output /tmp/cubey-atmosphere-moon-surface.png
./build/dev/projects/atmosphere/atmosphere --headless --debug-view milky-way --milky-way-source procedural --output /tmp/cubey-atmosphere-milky-way.png
./build/dev/projects/atmosphere/atmosphere --headless --atmosphere-preset sunset --output /tmp/cubey-atmosphere-sunset.png
./build/dev/projects/atmosphere/atmosphere --headless --time-of-day-mode solar --time-hours 17.8 --output /tmp/cubey-atmosphere-twilight.png
./build/dev/projects/atmosphere/atmosphere --headless --capture video --frames 120 --output /tmp/cubey-atmosphere.mp4
```

Controls:

- Left-drag: rotate view direction.
- Space: play/pause solar time.
- `R`: reset to the active preset.
- `D`: cycle final, rayleigh, mie, transmittance, optical-depth, sun-disk,
  aerial-perspective, night-sky, milky-way, moon, and moon-surface debug views.
- The Time panel switches between manual sun direction and local solar time.
- The Reference panel controls the ground grid, local red/cyan axes, and origin
  marker used for orientation.
- The Night sky panel switches Milky Way source, human/camera response, Milky Way
  intensity/contrast, light pollution, and procedural variation.
- Escape: close.

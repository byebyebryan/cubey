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
generated lunar atlas, and a procedural Milky Way atlas. The Milky Way generator
is tuned in local layers for stellar emission, dust lanes, star clouds, H II
regions, and speckles instead of consuming a source panorama.

Windowed runs create the lunar and night-sky atlases in background jobs and show
placeholder textures until uploads complete. Headless runs generate the same
atlases synchronously for deterministic capture output. Presets resolve through
the same solar-clock path used at runtime so their reported sun and exposure
values match what is rendered.

Useful runs:

```sh
./build/dev/projects/atmosphere/atmosphere --headless --output /tmp/cubey-atmosphere.png
./build/dev/projects/atmosphere/atmosphere --headless --debug-view transmittance --output /tmp/cubey-atmosphere-transmittance.png
./build/dev/projects/atmosphere/atmosphere --headless --debug-view moon-surface --output /tmp/cubey-atmosphere-moon-surface.png
./build/dev/projects/atmosphere/atmosphere --headless --debug-view milky-way --output /tmp/cubey-atmosphere-milky-way.png
./build/dev/projects/atmosphere/atmosphere --headless --debug-view milky-way --milky-way-layer dust-tau --output /tmp/cubey-atmosphere-milky-way-dust.png
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
- The Night sky panel switches diagnostic layer, human/camera response, Milky
  Way intensity/contrast, light pollution, and procedural variation.
- Escape: close.

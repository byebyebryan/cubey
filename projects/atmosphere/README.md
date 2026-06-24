# Atmosphere

`atmosphere` is a standalone clear-sky scattering demo. It proves the sky model
before the ocean renderer consumes it for sky, reflection, sun lighting, and
horizon aerial perspective.

The first implementation is direct ray-marched single scattering with Rayleigh
air scattering, Mie aerosol scattering, ozone absorption, transmittance, a sun
disk, and debug views. LUT-backed production rendering is intentionally deferred.
The reusable pieces now live under `cubey::render`: shared environment math and
frame uniform packing are in `atmosphere_environment`, and fullscreen sky
descriptor/pipeline/draw ownership is in `AtmosphereBackgroundFrame`.

Low-sun planet shadowing uses a softened solar-disk visibility term. The
transition is deliberately wider than the physical sun radius so sunrise and
sunset remain inspectable before multiple-scattering LUTs exist. Time of day is
resolved on the CPU from local solar time, day of year, and latitude, with manual
sun direction still available for art/debug work.

Night rendering includes procedural foreground stars, visible moon geometry that
uses the generated spherical lunar surface map, and a procedural Milky Way
atlas. Final, `moon`, and `moon-surface` views now use the shared celestial body
geometry path for moon drawing; the atmosphere shader keeps moon data only for
moonlight, star masking, and sky washout. The Milky Way generator is tuned in
local layers for stellar emission, dust lanes, star clouds, H II regions, and
speckles instead of consuming a source panorama.

Windowed runs create the lunar surface map and night-sky atlas in background
jobs and show placeholder textures until uploads complete. Headless runs
generate the same assets synchronously for deterministic capture output. Presets
resolve through the same solar-clock path used at runtime so
their reported sun and exposure values match what is rendered. This project
still owns presets, UI, debug view selection, generated sky assets, and
render-graph wiring; the render helpers are intended to be reusable by ocean and
later terrain/environment work.

Final view also has an early shared cloud-layer smoke path. It uses the
promoted `cubey::render::CloudLayer*` contracts and `shaders/cubey/cloud/`
assets to march a conservative half-resolution cloud product, then composites it
over the clear-sky atmosphere background. Cloud controls, temporal cloud
history, cloud debug views, cloud shadows, and cloud-driven reflection or
environment-lighting outputs are still deferred to the cloud foundation work.

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

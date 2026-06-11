# Clouds

`clouds` is the standalone pressure project for planet-aware cloud and weather
rendering. It keeps cloud policy separate from the clear-sky atmosphere project
while consuming the same solar-clock and atmosphere scattering foundation.

The current renderer is a v1 fullscreen spherical cloud-shell raymarch. It has
surface, high-altitude, and orbit camera modes, low-frequency weather coverage,
detail erosion, single-scattering lighting, cheap self-shadowing, and debug
views for the major fields. It deliberately does not integrate into ocean or
planet yet; those projects should later consume cloud sky/reflection and shadow
outputs after this standalone path is stable.

Useful runs:

```sh
./build/dev/projects/clouds/clouds
./build/dev/projects/clouds/clouds --cloud-camera-mode surface
./build/dev/projects/clouds/clouds --cloud-camera-mode high
./build/dev/projects/clouds/clouds --cloud-camera-mode orbit
./build/dev/projects/clouds/clouds --debug-view weather
./build/dev/projects/clouds/clouds --debug-view density
./build/dev/projects/clouds/clouds --debug-view shadow
./build/dev/projects/clouds/clouds --headless --frames 2 --cloud-camera-mode surface --output outputs/clouds-surface.png
./build/dev/projects/clouds/clouds --headless --frames 2 --cloud-camera-mode high --output outputs/clouds-high.png
./build/dev/projects/clouds/clouds --headless --frames 2 --cloud-camera-mode orbit --output outputs/clouds-orbit.png
./build/dev/projects/clouds/clouds --headless --capture video --frames 300 --fps 30 --cloud-camera-mode high --time-of-day-mode solar --time-speed-hours-per-second 0.05 --output outputs/clouds-high.mp4
```

Controls:

- Left-drag: rotate the camera.
- `D`: cycle final, weather, density, transmittance, lighting, shadow, and step
  debug views.
- Space: play/pause solar time.
- `R`: reset camera, time, and cloud settings.

## Current Scope

- The cloud layer is a spherical shell around the configured planet radius.
- `--cloud-camera-mode surface|high|orbit` changes the default camera altitude
  and view framing.
- `--cloud-quality quarter|half|full` controls raymarch sample budgets. The
  quality value is also carried as a resolution-scale contract for the later
  low-resolution composite path.
- Weather fields are procedural and deterministic. There is not yet an uploaded
  weather texture, authoring UI, temporal accumulation buffer, ocean reflection
  output, or cloud shadow texture.

See
[`docs/notes/cloud-weather-rendering-research.md`](../../docs/notes/cloud-weather-rendering-research.md)
for the research context and promotion criteria.

## Known V1 Issues

These are observed blockers before the clouds project should feed ocean or
planet rendering:

- Broad cloud-map seams have a first fix through a seam-safe spherical weather
  domain. Orbit-edge composition artifacts and high-view tuning are still rough.
- Interactive control is rough. The project has quick camera mode buttons and
  basic sliders, but it has not been moved onto the shared hierarchical control
  model used by the more mature projects.
- Runtime feedback is incomplete. The window does not show FPS/frame-time or
  sample-budget diagnostics yet, so perceived slowness is hard to separate from
  actual raymarch cost.
- The atmosphere horizon band can appear again. The clouds shader currently
  owns its own sky/ground composition path for the prototype, so it can diverge
  from the fixed shared atmosphere behavior used by established projects.

The next clouds pass should keep tightening these before adding ocean
reflection, planet integration, or richer cloud types.

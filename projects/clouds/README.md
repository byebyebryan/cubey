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
./build/dev/projects/clouds/clouds --cloud-camera-mode surface-up
./build/dev/projects/clouds/clouds --cloud-camera-mode high
./build/dev/projects/clouds/clouds --cloud-camera-mode high-oblique
./build/dev/projects/clouds/clouds --cloud-camera-mode orbit
./build/dev/projects/clouds/clouds --cloud-camera-mode orbit-terminator
./build/dev/projects/clouds/clouds --debug-view weather
./build/dev/projects/clouds/clouds --debug-view density
./build/dev/projects/clouds/clouds --debug-view shadow
./build/dev/projects/clouds/clouds --debug-view ground-hit
./build/dev/projects/clouds/clouds --debug-view cloud-alpha
./build/dev/projects/clouds/clouds --debug-view shell
./build/dev/projects/clouds/clouds --headless --frames 2 --cloud-camera-mode surface --output outputs/clouds-surface.png
./build/dev/projects/clouds/clouds --headless --frames 2 --cloud-camera-mode high --output outputs/clouds-high.png
./build/dev/projects/clouds/clouds --headless --frames 2 --cloud-camera-mode orbit --output outputs/clouds-orbit.png
./build/dev/projects/clouds/clouds --headless --capture video --frames 300 --fps 30 --cloud-camera-mode high --time-of-day-mode solar --time-speed-hours-per-second 0.05 --output outputs/clouds-high.mp4
```

Controls:

- Left-drag: rotate the camera.
- `D`: cycle final, weather, density, transmittance, lighting, shadow, step,
  background, atmosphere, ground, ground-hit, cloud-alpha, and shell debug
  views.
- Space: play/pause solar time.
- `R`: reset camera, time, and cloud settings.

## Current Scope

- The cloud layer is a spherical shell around the configured planet radius.
- `--cloud-camera-mode surface|surface-up|high|high-oblique|orbit|orbit-terminator`
  changes the default camera altitude and view framing. The shorter legacy
  names `surface`, `high`, and `orbit` remain the standard aliases.
- `--cloud-quality quarter|half|full` controls raymarch sample budgets. The UI
  shows the intended resolution-scale contract, but clouds still render directly
  to the final target until the separate low-resolution composite path lands.
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
- Runtime feedback now shows FPS/frame-time and sample-budget diagnostics, but
  the quality presets still do not reduce the actual render target resolution.
- Sky, ground, and cloud composition is now explicit and has debug views, but it
  remains project-local prototype code rather than a reusable cloud scene pass.

The next clouds pass should keep tightening these before adding ocean
reflection, planet integration, or richer cloud types.

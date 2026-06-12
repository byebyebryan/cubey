# Clouds

`clouds` is the standalone pressure project for planet-aware cloud and weather
rendering. It keeps cloud policy separate from the clear-sky atmosphere project
while consuming the same solar-clock and atmosphere scattering foundation.

The current renderer is a v1 spherical cloud-shell raymarch. It renders a
quality-scaled cloud product target with linear cloud radiance plus view
transmittance, composites that product over the standalone atmosphere/ground
proxy, and supports surface, high-altitude, and orbit camera modes,
typed procedural weather presets, front/cell/streak weather structure,
type-specific density profiles, single-scattering lighting, cheap
self-shadowing, prototype surface cloud shadows, and debug views for the major
fields. It deliberately does not integrate into ocean or planet yet; those
projects should later consume cloud sky/reflection and shadow outputs after this
standalone path is stable.

Useful runs:

```sh
./build/dev/projects/clouds/clouds
./build/dev/projects/clouds/clouds --cloud-camera-mode surface
./build/dev/projects/clouds/clouds --cloud-camera-mode surface-up
./build/dev/projects/clouds/clouds --cloud-camera-mode high
./build/dev/projects/clouds/clouds --cloud-camera-mode high-oblique
./build/dev/projects/clouds/clouds --cloud-camera-mode orbit
./build/dev/projects/clouds/clouds --cloud-camera-mode orbit-terminator
./build/dev/projects/clouds/clouds --cloud-weather-preset fair-weather
./build/dev/projects/clouds/clouds --cloud-weather-preset broken-cumulus
./build/dev/projects/clouds/clouds --cloud-weather-preset overcast-stratus
./build/dev/projects/clouds/clouds --cloud-weather-preset storm-cells
./build/dev/projects/clouds/clouds --cloud-weather-preset high-cirrus
./build/dev/projects/clouds/clouds --debug-view weather
./build/dev/projects/clouds/clouds --debug-view density
./build/dev/projects/clouds/clouds --debug-view shadow
./build/dev/projects/clouds/clouds --debug-view ground-hit
./build/dev/projects/clouds/clouds --debug-view cloud-alpha
./build/dev/projects/clouds/clouds --debug-view shell
./build/dev/projects/clouds/clouds --debug-view surface-shadow
./build/dev/projects/clouds/clouds --cloud-shadow-strength 1.0
./build/dev/projects/clouds/clouds --headless --frames 2 --cloud-camera-mode surface --output outputs/clouds-surface.png
./build/dev/projects/clouds/clouds --headless --frames 2 --cloud-camera-mode high --output outputs/clouds-high.png
./build/dev/projects/clouds/clouds --headless --frames 2 --cloud-camera-mode orbit --output outputs/clouds-orbit.png
projects/clouds/capture_review.sh outputs/clouds-review
./build/dev/projects/clouds/clouds --headless --capture video --frames 300 --fps 30 --cloud-camera-mode high --time-of-day-mode solar --time-speed-hours-per-second 0.05 --output outputs/clouds-high.mp4
```

Controls:

- Left-drag: rotate the camera.
- `D`: cycle final, weather, density, transmittance, lighting, shadow, step,
  background, atmosphere, ground, ground-hit, cloud-alpha, shell, and
  surface-shadow debug views.
- Space: play/pause solar time.
- `R`: reset camera, time, and cloud settings.

## Current Scope

- The cloud layer is a spherical shell around the configured planet radius.
- `--cloud-camera-mode surface|surface-up|high|high-oblique|orbit|orbit-terminator`
  changes the default camera altitude and view framing. The shorter legacy
  names `surface`, `high`, and `orbit` remain the standard aliases.
- `--cloud-weather-preset fair-weather|broken-cumulus|overcast-stratus|storm-cells|high-cirrus`
  selects baseline coverage, density, weather scale, wind, layer altitude, and
  internal cloud style. Legacy aliases remain accepted:
  `clear`, `scattered`, `inspection`, `overcast`, and `storm`.
  The default `broken-cumulus` preset is intentionally a review-friendly broken
  cloud view so surface, high-altitude, and orbit captures all show meaningful
  structure.
- `--cloud-quality quarter|half|full` controls both raymarch sample budgets and
  the offscreen cloud product render scale before final compositing. `quarter`
  is the default interactive mode; higher modes are still useful for diagnosis
  but are not yet real-time enough to be the default.
- `--cloud-shadow-strength` controls the prototype analytic cloud shadow factor
  applied only to the standalone procedural ground/ocean proxy.
- Weather fields are procedural and deterministic. There is not yet an uploaded
  weather texture, authoring UI, temporal accumulation buffer, ocean reflection
  output, or promoted cloud shadow texture.

See
[`docs/notes/cloud-weather-rendering-research.md`](../../docs/notes/cloud-weather-rendering-research.md)
for the research context and promotion criteria.

Use `projects/clouds/capture_review.sh outputs/clouds-review` to write the
standard review bundle: surface, surface-up, high, high-oblique, orbit,
orbit-terminator, weather, density, cloud-alpha, and surface-shadow. If
ImageMagick is available the helper also writes `contact-sheet.png`.

## Known V1 Issues

These are observed blockers before the clouds project should feed ocean or
planet rendering:

- Broad cloud-map seams have a first fix through a seam-safe spherical weather
  domain and typed procedural fronts/cells/streaks. Orbit-edge and high-view
  composition are still rough, but high-altitude cloud contribution now tapers
  near the horizon and orbit detail suppresses local high-frequency erosion so
  the planet reads as broad weather masses rather than speckle.
- Surface camera views still show horizon streaking/banding from the v1
  raymarched cloud shell and vertically extruded density model. Full quality
  reduces noise but does not remove the pattern, so this needs a deeper
  density/reconstruction pass rather than more sample-budget tuning.
- High-oblique views can expose a dark upper-atmosphere/space band because the
  standalone clouds composite uses a local prototype atmosphere/background
  pass. This should be resolved before cloud outputs are promoted into the
  shared sky/environment path.
- Interactive control is rough. The project has quick camera mode buttons and
  basic sliders, but it has not been moved onto the shared hierarchical control
  model used by the more mature projects.
- Runtime feedback now shows FPS/frame-time, sample-budget diagnostics, cloud
  render pixels, output pixels, and the active cloud render scale.
- Sky, ground, and cloud composition is explicit and uses a cloud product target
  plus the shared atmosphere sky-background ray classifier for non-ground rays,
  but it remains project-local prototype code rather than a reusable cloud scene
  pass.
- Night-side cloud lighting now gates direct/twilight contribution more
  strictly, but proper moonlight/starlight and exposure-aware night cloud
  silhouettes are still missing.

## Integration Contract

Ocean and planet should not raymarch volumetric clouds inside their material
shaders. The intended promotion path is a cloud scene/composite producer that
emits:

- linear cloud radiance plus transmittance for background composition;
- a low-frequency cloud shadow factor keyed by sun direction;
- optional reflection/environment inputs for water and PBR consumers;
- debug views for weather, density, lighting, shadow, cloud alpha, and shell
  coverage.

The next clouds pass should keep tightening these before adding ocean
reflection, planet integration, or richer cloud types.

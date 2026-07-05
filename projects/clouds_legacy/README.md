# Clouds Legacy

`clouds_legacy` is the frozen first-pass planet-aware cloud and weather
prototype. It is kept as a known comparison target while active cloud work uses
the shared `cubey::render::CloudLayerRuntime` tuned through
`projects/atmosphere`. Do not add new production cloud work here unless the
goal is explicitly to preserve or inspect legacy behavior.

The legacy renderer is a v1 planet-aware cloud raymarch. Surface and high
cameras use a capped spherical-shell local cloud segment with a distant horizon
layer, while orbit cameras keep the full spherical planet-scale shell. The
renderer writes a quality-scaled cloud product target with linear cloud
radiance plus view transmittance, emits reconstruction metadata
(mean cloud distance, alpha, horizon factor, confidence), resolves color and
metadata through per-frame-slot temporal reconstruction, and composites that
product over the standalone atmosphere/ground proxy. It supports typed
procedural weather presets, front/cell/streak weather structure, type-specific
density profiles, shared-atmosphere sun/moon lighting, cheap base/detail density
separation, distance/grazing/footprint detail LOD, cheap self-shadowing,
prototype surface cloud shadows, and raw product debug views for the major
fields. It deliberately does not integrate into ocean or planet; current
consumers should use the shared runtime and receive cloud sky/reflection or
shadow outputs there when those products become stable.

Useful runs:

```sh
./build/dev/projects/clouds_legacy/clouds_legacy
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-camera-mode surface
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-camera-mode surface-up
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-camera-mode high
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-camera-mode high-oblique
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-camera-mode orbit
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-camera-mode orbit-terminator
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-weather-preset fair-weather
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-weather-preset broken-cumulus
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-weather-preset overcast-stratus
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-weather-preset storm-cells
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-weather-preset high-cirrus
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view weather
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view density
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view shadow
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view ground-hit
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view cloud-alpha
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view shell
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view surface-shadow
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view domain
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view distance
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view base-density
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view detail-density
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view density-lod
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view step-length
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view local-march
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view far-horizon
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view cloud-depth
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view cloud-confidence
./build/dev/projects/clouds_legacy/clouds_legacy --debug-view horizon-blend
./build/dev/projects/clouds_legacy/clouds_legacy --cloud-shadow-strength 1.0
./build/dev/projects/clouds_legacy/clouds_legacy --no-cloud-temporal
./build/dev/projects/clouds_legacy/clouds_legacy --headless --frames 2 --cloud-camera-mode surface --output outputs/clouds-surface.png
./build/dev/projects/clouds_legacy/clouds_legacy --headless --frames 2 --cloud-camera-mode high --output outputs/clouds-high.png
./build/dev/projects/clouds_legacy/clouds_legacy --headless --frames 2 --cloud-camera-mode orbit --output outputs/clouds-orbit.png
projects/clouds_legacy/capture_review.sh outputs/clouds-review
./build/dev/projects/clouds_legacy/clouds_legacy --headless --capture video --frames 300 --fps 30 --cloud-camera-mode high --time-of-day-mode solar --time-speed-hours-per-second 0.05 --output outputs/clouds-high.mp4
```

Controls:

- Left-drag: rotate the camera.
- `D`: cycle final, weather, density, transmittance, lighting, shadow, step,
  background, atmosphere, ground, ground-hit, cloud-alpha, shell,
  surface-shadow, domain, distance, base-density, detail-density, density-lod,
  step-length, local-march, far-horizon, cloud-depth, cloud-confidence, and
  horizon-blend debug views.
- Space: play/pause solar time.
- `R`: reset camera, time, and cloud settings.

## Current Scope

- Surface and high cameras render a finite local cloud volume to avoid
  planet-scale tangent marches near the horizon. Orbit cameras render the
  spherical shell around the configured planet radius.
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
  the offscreen cloud product render scale before final compositing. Local
  surface/high views keep at least half vertical cloud resolution even in
  `quarter` mode because near-horizon cloud rows are sensitive to vertical
  undersampling. The cloud raymarch still uses the final view aspect ratio when
  the offscreen target is scaled non-uniformly. `quarter` is still the default
  interactive mode; higher modes remain useful for diagnosis but are not yet
  real-time enough to be the default.
- `--cloud-temporal` / `--no-cloud-temporal` toggles per-frame-slot temporal
  reconstruction of the cloud product. The temporal pass reprojects history
  from cloud mean-distance metadata, rejects disagreeing depth/alpha/confidence
  samples, and clamps history against the current neighborhood. Disable it when
  isolating raw raymarch noise or history artifacts.
- `--cloud-shadow-strength` controls the prototype analytic cloud shadow factor
  applied only to the standalone procedural ground/ocean proxy.
- Weather fields are procedural and deterministic. There is not yet an uploaded
  weather texture, authoring UI, ocean reflection output, or promoted cloud
  shadow texture.

See
[`docs/notes/cloud-weather-rendering-research.md`](../../docs/notes/cloud-weather-rendering-research.md)
for the research context and promotion criteria.

Use `projects/clouds_legacy/capture_review.sh outputs/clouds-review` to write the
standard review bundle: surface, surface-up, high, high-oblique, orbit,
orbit-terminator, temporal-off surface, weather, density, base-density,
detail-density, density-lod, step-length, local-march, far-horizon,
cloud-alpha, domain, distance, surface-shadow, and orbit-night. If ImageMagick
is available the helper also writes `contact-sheet.png`.

## Known V1 Issues

These are observed blockers before any cloud project should feed ocean or
planet rendering:

- Broad cloud-map seams have a first fix through a seam-safe spherical weather
  domain and typed procedural fronts/cells/streaks. Orbit-edge and high-view
  composition still need more art direction, but orbit detail suppresses local
  high-frequency erosion so the planet reads as broad weather masses rather
  than speckle.
- Surface and high views now use a capped spherical-shell local segment plus a
  distant horizon layer instead of marching the full spherical shell tangent to
  the planet. The density model separates broad base density from high-frequency
  erosion, suppresses detail for distant/grazing local rays, adds targeted
  adaptive horizon samples, replaces the old single-sample horizon fallback,
  keeps a higher vertical cloud target in local quarter-quality views, preserves
  the final view aspect for anisotropic cloud targets, and applies a final-only
  lower-sky filter for local horizon composition. The latest reconstruction
  pass adds metadata-driven temporal reprojection, footprint-aware density LOD,
  per-step horizon dither, and a stronger low-frequency local horizon layer.
  This gives the project the right hooks for the fix, but does not fully solve
  the raw surface/high horizontal streak source; remaining artifacts are
  visible in raw `density`, `detail-density`, `density-lod`, and `local-march`
  diagnostics.
- High-oblique background composition now separates sky/space from the
  diagnostic ground proxy and uses sky-only atmosphere classification for the
  background. Any remaining horizon band should be treated as a visual tuning
  bug, not as the old ground-occluded atmosphere path.
- Interactive control is rough. The project has quick camera mode buttons and
  basic sliders, plus temporal and debug controls, but it has not been moved
  onto the shared hierarchical control model used by the more mature projects.
- Runtime feedback now shows FPS/frame-time, sample-budget diagnostics, cloud
  render pixels, output pixels, and the active cloud render scale split by X/Y
  axis.
- Sky, ground, and cloud composition is explicit and uses a cloud product target
  plus the shared atmosphere sky-background ray classifier for non-ground rays,
  but it remains project-local prototype code rather than a reusable cloud scene
  pass.
- Night-side cloud lighting now gets direct sun and moon ambient intensity from
  the shared atmosphere lighting model. Moonlight is still scalar ambient rather
  than a full directional/moon-shadow model.

## Integration Contract

Ocean and planet should not raymarch volumetric clouds inside their material
shaders. The intended promotion path is a cloud scene/composite producer that
emits:

- linear cloud radiance plus transmittance for background composition;
- a low-frequency cloud shadow factor keyed by sun direction;
- optional reflection/environment inputs for water and PBR consumers;
- debug views for weather, density, base/detail density, lighting, shadow,
  cloud alpha, shell coverage, horizon fallback, and local march diagnostics.

The next clouds pass should keep tightening these before adding ocean
reflection, planet integration, or richer cloud types.

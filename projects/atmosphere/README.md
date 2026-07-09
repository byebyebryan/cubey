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

Final view also uses the shared cloud-layer runtime. `CloudLayerRuntime` owns
generated cloud resources, march/temporal/composite pipelines, render-graph
products, descriptors, and temporal history; atmosphere consumes it in
external-background mode so clouds are composited over the clear-sky background.
The Clouds panel exposes the shared quality, sampling, layer, weather, shape,
lighting, and debug controls, and existing `clouds.*` config/CLI overrides
apply here too. Cloud V1 is intentionally surface-only: `surface-volume`,
`auto`, the lower-sky horizon handoff, Bayer sampling, single-frame sampling,
and the terrain-post resolve are the production baseline. Use
`--cloud-distance-mode local --no-cloud-horizon-layer` when a strict local-only
reference fallback is needed. Aerial/high-altitude/orbit controls remain exposed
only as deferred experiments. `--cloud-view-samples 2 --cloud-view-sample-mode
single-frame` is the direct local-volume sampling reference; `--cloud-temporal
--cloud-view-samples 2 --cloud-view-sample-mode temporal-phased` is the cheaper
temporal reconstruction candidate. Cloud shadows and cloud-driven reflection or
environment-lighting outputs remain deferred.

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
projects/atmosphere/capture_milky_way_layers.sh outputs/atmosphere-milky-way-layers
projects/atmosphere/capture_atmosphere_background_horizon.sh outputs/atmosphere-background-horizon
projects/atmosphere/capture_cloud_review.sh outputs/atmosphere-cloud-review
projects/atmosphere/capture_cloud_edge_resolve.sh outputs/atmosphere-cloud-edge-resolve
projects/atmosphere/capture_cloud_farfield_handoff.sh outputs/atmosphere-cloud-farfield-handoff
projects/atmosphere/capture_cloud_lighting_regimes.sh outputs/atmosphere-cloud-lighting-regimes
projects/atmosphere/capture_cloud_surface_horizon_regimes.sh outputs/atmosphere-cloud-surface-horizon
```

The cloud review script writes a stable set of labeled captures plus
`manifest.tsv`, `index.md`, and `contact-sheet.png` for quick visual comparison.
It defaults to 1920x1080 half-quality captures, because half resolution is the
current practical target; set `WIDTH`, `HEIGHT`, `QUALITY`, or `DEEP=1` for
faster smoke runs or full-quality reference comparisons. Surface rows define
Cloud V1 acceptance. High-oblique/orbit rows, when present, are deferred
diagnostics and should not drive V1 tuning.

The Milky Way layer script captures the `milky-way` debug view for every
generated atlas layer: final, stellar emission, dust optical depth, star clouds,
H II emission, and speckles. Use it before changing the procedural night-sky
atlas recipe; the v2 design direction is tracked in
`docs/notes/milky-way-v2-research.md`.

The background horizon script disables clouds and reference geometry, then
captures `ground`, `sky-only`, and `sky-only-no-ground-occlusion` at noon,
twilight, and night. Use it before cloud tuning when a hard horizon band is
visible in final color.

The edge resolve script is the focused A/B for cloud-edge artifacts. It captures
`terrain-post` and `metadata-bilateral` resolve modes for `surface-up` and
`high-oblique` views, with `final`, `raw-final`, `cloud-alpha`, and `edge-mask`
diagnostics plus a secondary half-quality final check.

The far-field handoff script focuses on horizon and grazing-ray artifacts. It
captures full-resolution `surface-horizon`, `surface-up`, and `high-oblique`
views with handoff, local-truncation, integrated-horizon, edge, and distance
regime diagnostics. For Cloud V1, use the surface captures as the acceptance
surface; high-oblique output is carried only as later-version evidence.

The lighting-regime script focuses on the `surface-volume` surface cloud path.
It captures noon, twilight, afterglow, and night in surface-up and horizon views
with lighting, alpha, background, edge, and distance diagnostics, and disables
the standalone reference grid so cloud/sky transitions are easier to inspect.
Use it when changing `clouds.twilight_color_strength`,
`clouds.twilight_edge_strength`, `clouds.twilight_saturation_strength`, or
`clouds.afterglow_strength`; twilight should pick up low-sun warmth and
optional afterglow color without making night clouds look sunlit.

The surface-horizon regime script is the focused A/B for the lower-sky handoff.
For each noon, twilight, afterglow, and night case it captures local-only
`surface-volume`, the default handoff path with `--cloud-distance-mode auto
--cloud-horizon-layer`, and a no-cloud background comparison. Use the handoff,
local-truncation, and integrated-horizon diagnostics there before judging
surface horizon fixes from final color alone.

Controls:

- Left-drag: rotate view direction.
- Space: play/pause solar time.
- `R`: reset to the active preset.
- `D`: cycle final, rayleigh, mie, transmittance, optical-depth, sun-disk,
  aerial-perspective, night-sky, milky-way, moon, and moon-surface debug views.
- The Time panel switches between manual sun direction and local solar time.
- The Reference panel controls the ground grid, local red/cyan axes, and origin
  marker used for orientation.
- The Clouds panel controls the shared cloud layer used in final view, including
  cloud diagnostic views through `--cloud-debug-view` / Debug view.
- The Night sky panel switches diagnostic layer, human/camera response, Milky
  Way intensity/contrast, light pollution, and procedural variation.
- Escape: close.

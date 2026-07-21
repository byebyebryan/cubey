# Terrain Surface Semantics Study

Date: 2026-07-20

Status: active reference study; no product-default change.

## Context

Terrain V1 intentionally promoted an elevation-only asset. That narrow contract
made source shape, placement, cached topology, material frequency, lighting, and
steady-state cost independently reviewable. Terrain Diffusion can also emit
continuous climate fields, but the runtime asset disabled them and the renderer
currently derives only ground, rock, snow, and ambient visibility from height
and slope.

The resulting landform is best described as fluvially dissected mountainous
upland. The current mineral palette makes it read as uniformly barren or
semi-arid, but that ecological reading is a Cubey presentation choice rather
than source data. This study tests whether a broader valley-to-mountain surface
transition improves the accepted far-backdrop without changing its geometry.

## Frozen Comparison

All lanes retain the canonical seed-0 elevation hash, selected placement,
vertical transform, cached mesh density, render stride, camera envelope,
atmosphere, clouds, shadows, and Material V2 detail texture. The three surface
models are:

- `mineral-control`: the accepted Material V2 ground, rock, and sparse-snow
  response, with no vegetation implication;
- `landform-transition`: muted temperate valley and plain cover that gives way
  continuously to foothill soil, exposed mountain rock, and sparse snow;
- `climate-transition`: the same landform capacity modulated by Terrain
  Diffusion temperature, precipitation, and seasonality.

The study does not classify categorical biomes. Its outputs are continuous
material controls, and it does not claim grass blades, trees, scree geometry,
or close-surface fidelity.

## Climate Companion

The study asset is separate from `cubey.terrain.heightfield.v1` and is bound to
one elevation SHA-256. It stores a deterministic 256 x 256, 240 m grid produced
by area-averaging each 8 x 8 block of the native 30 m output. Channels are
normalized at bake time to:

1. mean temperature in degrees Celsius;
2. temperature standard deviation in degrees Celsius;
3. annual precipitation in millimeters;
4. precipitation coefficient of variation in `[0, 1]`.

The upstream Terrain Diffusion checkout is MIT licensed. Cubey keeps the raw
continuous climate semantics and does not import its Minecraft-specific biome
IDs or categorical classifier.

## Continuous Surface Model

Landform capacity uses broad normalized height, geometric slope, and local
concavity. Low, flat, sheltered terrain is most capable of supporting the
temperate cover treatment; steep and high terrain transitions to the existing
rock and snow response. Every curve is smooth and the final material weights
remain normalized and bounded.

The climate lane estimates potential evapotranspiration from temperature,
computes an effective moisture ratio from precipitation and precipitation
seasonality, and estimates growing-season length from mean temperature and
temperature variability. Moisture and growing-season suitability modulate the
landform capacity. Cold, sufficiently wet, non-steep terrain receives the
climate snow response. These are rendering controls, not an ecological survey.

The material palette remains restrained: dry olive through cool muted green for
cover, desaturated soil, the accepted cool/warm mineral rock, and sparse cool
snow. Vegetated ground receives less normal perturbation than exposed rock; no
high-frequency color or normal field is used to imply unrendered flora.

## Runtime Boundary

The companion source, surface model enum, diagnostics, and controls remain
project-local reference facilities. `mineral-control` stays the default.
Climate mode requires a matching companion source; mineral and landform modes
remain usable with elevation alone. Surface model or placement changes stage a
complete cached-product rebuild and swap only a successful replacement.

The ordinary build and `cubey_terrain_generate_default_asset` remain
elevation-only and have no new network or generation side effects. The explicit
`cubey_terrain_generate_surface_study_asset` target owns climate generation.

## Review Gate

The primary review compares all three lanes at the qualified 500 m view and
four headings. Raking light, 100 m stress, and selected/raw placement controls
are supporting evidence. Vegetation, moisture, material-weight, and albedo
diagnostics must explain the final image.

The study succeeds when:

- the valley/plain-to-mountain transition is clearer in at least three of four
  qualified headings;
- no hard elevation band, saturated green blanket, periodic field, or false
  grass detail appears;
- climate variation is spatially coherent and agrees with its diagnostics
  across raw placements;
- elevation hash, geometry hash, placement, topology, and source sampling stay
  invariant across lanes;
- clear mean and p50 remain at or below 1.10 ms and within 0.10 ms of the
  mineral control at 1600 x 900.

One bounded calibration pass may adjust continuous thresholds, strengths, and
palette balance. It may not change source selection, height, placement,
topology, or camera support. The final result is a documented recommendation,
not a production promotion.

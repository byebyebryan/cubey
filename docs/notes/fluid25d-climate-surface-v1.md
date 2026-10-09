# Fluid 2.5D climate-informed procedural surface V1

## Verdict and scope

Terrain Diffusion's climate companion is useful for **macro material placement**,
not a substitute for surface detail. This study reuses the Terrain project's
existing landform/climate classifier instead of introducing another biome model.
The climate-aware candidate removes most of the height-only snow-like cap on
the retained temperate mountain and gives sheltered ground a more coherent
substrate treatment. It is worth retaining as an opt-in foundation; closer
runoff views still read smooth/clay-like. This is not a claim of realistic rock,
known geology, actual vegetation or current snow cover.

The current lane is deliberately **completed native recordings only**. It needs
the original heightfield and its adjacent `surface-fields.json`. Streamed
recordings, external live sessions and unbound source identities are rejected,
not silently assigned a guessed crop. The next integration step, if this
appearance is accepted, is to give the live scene contract equally explicit
source-coordinate provenance. Neither application nor launcher defaults change.

The gallery, matched captures, statistics, timings and regression receipts are
under `outputs/fluid/climate-surface-v1-20261008/`. Owner visual acceptance is
deferred. No desktop window or hydraulic producer was needed for the comparison.

## Reused inputs and derived masks

`fluid_25d_terrain_surface.cpp` links the unchanged
`terrain_surface_model.cpp` (`terrain-surface-model-v2`) and
`terrain_raster_climate_source.cpp`. Source elevation and climate are checked
for matching seed, elevation hash and bounds. The replay binding also checks
the recorded manifest/payload identity, crop, spacing and transformed source
bed, including every original source-coordinate elevation sample. A flipped or
transposed crop cannot pass merely by supplying its new byte hash.

Three separable, edge-renormalized height averages use 90m, 360m and 1440m
physical radii. Fine-scale gradients provide slope; local/broad departures
provide a concavity proxy. These feed the existing classifier alongside
crop-relative height and relief. Climate is bilinearly sampled at the original
source coordinates, not normalized to the viewing camera or invented from noise.

The two RGBA masks contain rock, cover potential, moisture potential and snow
potential. Here cover potential changes **bare substrate color/roughness**; no
grass geometry or leafy green carpet is added. The procedural material reuses
the existing detail field, varies stone/soil response and quiets soil normals.
All four values are artistic priors, not observed land-cover probabilities.

Annual mean temperature, temperature seasonality, annual precipitation and
precipitation variability are long-term climate. They do not change GUI rain
intensity, hydraulic forcing or current wetness. Actual wet-ground response
still comes from the retained water depth. The climate companion's 240m raster
does not imply 240m independently generated climate: this model's learned
climate is much coarser (about 7.68km). Moisture potential nearly saturates on
this crop, an inherited classifier limitation; do not read it as standing water.
Snow potential is also only a presentation proxy, not a weather/snow simulation.

## Runtime and controls

For a built dev application and retained local inputs, from the repository root:

```sh
rtk proxy build/dev/projects/fluid/fluid_25d/fluid_25d \
  --frames 0 --width 1280 --height 720 \
  --fluid25d-recording outputs/fluid/native-rain-recession-v1-20261003-1ZMqTJ/recordings/rain-on/recording.json \
  --fluid25d-recording-time-seconds 6000 --fluid25d-recording-camera runoff \
  --fluid25d-native-presentation scenic --fluid25d-scenic-material macro \
  --fluid25d-scenic-surface-source cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json \
  --fluid25d-scenic-surface-mode climate
```

This starts paused at a saved state. In **Scenic materials**, **Terrain
organization** switches Legacy / Landform masks / Climate + landform without
resetting playback or uploading different hydraulic fields. The control is
disabled without a bound source. `V` still compares Readable; raw maps remain
the numerical reference. The new CLI options are not yet forwarded by the
Mountain Rain launcher; use the explicit recording command for this study.

Masks are built once on load, not per frame. Two 512-square RGBA8 textures with
full area-averaged mip chains use 2,796,200 bytes (about 2.67MiB); both reside to
permit instant mode switching. Float base masks retain 8MiB CPU storage on this
case; startup allocations and latency were not profiled. The terrain shader
samples one selected mask. No imported material assets are added.

The matched nine-run 720p headless replay profile measured climate median
0.663ms and median-of-batch p95 2.643ms, versus legacy 0.648/2.577ms. The p95
difference is 0.067ms, within the predeclared 4ms absolute / 0.15ms incremental
gates. These are presentation GPU spans, not total GUI frame times, and exclude
first-use setup and simultaneous CUDA. Do not generalize that small noisy delta
to a guaranteed live overhead.

## Review and invariants

The three comparison columns are existing macro / landform-only / climate plus
landform. Start with the dry overview and reverse heading, then the matched dry
and wet runoff views. In component views, red=rock, green=cover potential and
blue=snow potential; they deliberately show classification, not realism.
Reported channel means are mean weights, **not area percentages**.

The review runner is `projects/fluid/fluid_25d/review_climate_surface_v1.py`.
Its baseline phase must run against the pre-change executable before rebuilding;
candidate, review, profile and climate-statistics phases use the new executable.
Use a fresh `climate-surface-*` leaf under `outputs/fluid`; phases refuse to
overwrite evidence. Frozen recording and source identities are checked before
and after each capture/profile batch.

The final `seal` phase verifies the completed full-test/private-GUI receipts,
source/input/runtime hashes, retained media and scene-pixel changes outside the
GUI panel. It writes `acceptance.json` once, without promoting human acceptance.

All eight disabled-mode images match the pre-change pixels exactly. Only the
Scenic terrain fragment SPIR-V changes; water, geometry and numerical shaders
remain byte-identical. Recorded terrain/depth/velocity uploads pass bit-exact
validation with zero hydraulic dispatches. Tests cover rectangular/oriented
source binding, deterministic masks, climate isolation, invalid inputs and
unsupported-mode rejection. This study does not modify elevation, solver
inputs, film/reconstruction defaults, dots/trails or live rain controls.

All 173 dev tests pass without skips. The actual private GUI switches all three
surface modes and round-trips Readable and resizing; scene pixels change, not
just labels. The existing water-component GUI probe also passes after its fixed
click coordinates are adjusted for the additional row. A first private-probe
setup attempt used a headless-only upload-validation flag and was correctly
rejected; its log is retained, not counted as a product failure or passing GUI
evidence. No user desktop or hydraulic worker was launched.

## What remains

One mountain crop is not general terrain calibration. The modest visual gain is
better material organization, not added geological structure. Connected rock
exposure/weathering structure at intermediate scales is a better next rendering
question than more unrelated texture noise. Review this foundation before
expanding the live contract or promoting a preset. Vegetation, clutter,
near-ground texture assets, erosion, new terrain generation and numerical solver
research remain outside this direction.

Related work: [Terrain semantics](terrain-surface-semantics-study.md),
[climate calibration](terrain-climate-calibration-v1.md),
[climate model research](terrain-climate-surface-model-research.md), and
[procedural visual strategy](terrain-visual-fidelity-strategy.md).

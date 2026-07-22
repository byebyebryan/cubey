# Terrain Climate Calibration V1

Date: 2026-07-21

Status: evidence complete; existing formulas and production default unchanged.

## Purpose

The first climate surface study used one warm, dry seed-0 patch. That was not
enough evidence for a shared surface model. This calibration freezes five
Terrain Diffusion elevation/climate pairs and runs the same three surface
models across all of them:

- `mineral-control` isolates the existing height/slope material allocation;
- `landform-transition` adds art-directed cover from local terrain only;
- `climate-transition` combines the same landform capacity with imported
  temperature and precipitation proxies.

This remains a far-field rendering study. The regime names are calibration
labels, not categorical biomes or ecological claims.

## Generated Asset Contract

The explicit target is:

```sh
cmake --build --preset dev \
  --target cubey_terrain_generate_climate_calibration_assets
```

It writes untracked assets under
`build/dev/assets/terrain/climate-calibration/`. Ordinary configure, build, and
test do not invoke Terrain Diffusion.

The pinned inputs remain:

- Terrain Diffusion code `82a0431281f21a6ec3d691a12ee61525de5b0790`;
- model `xandergos/terrain-diffusion-30m` at revision
  `9ef8030cb805b433b98ec25c5dddefbac07a9e26`;
- seed 0 at 30 m native spacing;
- 2048 x 2048 elevation and 256 x 256 area-averaged climate per region;
- the canonical height transform used by the terrain product.

Selection scans non-overlapping 61.44 km windows from one coarse seed-0 field.
Candidates require at least 80 percent land and 1000 m of p90-p25 relief. The
hot/dry control remains fixed; the other regimes minimize normalized distance
to a temperature/precipitation target with deterministic relief, land, origin,
and coordinate tie-breaks.

| Regime | Coarse origin | Land | Relief | Temperature | Precipitation |
| --- | ---: | ---: | ---: | ---: | ---: |
| hot/dry | -8, -24 | 0.859 | 1389 m | 20.41 C | 179 mm/year |
| hot/wet | -56, 24 | 1.000 | 1294 m | 23.17 C | 2201 mm/year |
| cool/wet | 120, -112 | 1.000 | 1497 m | 10.24 C | 710 mm/year |
| cold/dry | -16, -32 | 1.000 | 1320 m | -12.45 C | 47 mm/year |
| cold/wet | -8, -88 | 0.844 | 2464 m | -4.20 C | 281 mm/year |

The real bake completed in 111.75 seconds and produced 94,349,735 bytes,
passing the explicit 300-second and 128 MiB gates. The hot/dry control retained
the canonical hashes:

- elevation:
  `27b49f12f29ae24629a8ec03d12b53c6986404c0354069529be75a5ea02c45df`;
- climate:
  `cf56ae54e93ab45a10d0e93c2c39ab2a95b1593bf89639eeda3e3b7080497fea`.

## Diagnostic Contract

The implementation now exposes the existing empirical calculations as named
`TerrainClimatePotential` fields. No constants, thresholds, or visual formulas
changed. In particular, the previous PET and aridity variable names are now
reported as a thermal water-demand proxy and climate moisture ratio; they are
not presented as physical evapotranspiration or soil moisture.

Profiles record:

- raw temperature mean and seasonality;
- raw annual precipitation and precipitation CV;
- growing-season days and thermal growth;
- thermal water-demand proxy and climate moisture ratio;
- seasonality factor and effective moisture;
- moisture and cover weights;
- annual-cold and wet-snow potentials;
- final rock, snow, vegetation, and moisture means.

When a climate companion is explicitly bound, all three surface models sample
it for diagnostics. Mineral and landform models still ignore climate for their
material output.

## Capture Contract

Run:

```sh
projects/terrain/capture_climate_calibration_study.sh
```

The pack is written to `outputs/terrain/climate-calibration-v1/`. It contains
matched 500 m views at headings 90 and 270 degrees, one raking-light climate
view per regime, four surface diagnostics, fixed-scale source previews, profile
metrics, a JSON report, and five contact sheets.

The main comparison uses `raw-center` placement so source selection remains
independent from the climate/material study. Under the original 1 km local gate,
the cool/wet field's best selected candidate had 129.589 m of relief against the
120 m limit and 0.337648 P95 slope against the 0.25 limit. Its directional and
camera-clearance checks passed.

The follow-up selected-stage contract evaluates the actual supported product
envelope instead: a 500 m radius, 120 m relief limit, and 0.275 P95 slope limit.
Cool/wet now passes at source focus `(-11.5, -11.5) km`, with 91.433 m local
relief, 0.260455 P95 slope, and 484.231 m baked clearance. This admits the useful
alpine/snowline composition without changing the source. The selected hot/dry
control retains its source and placement while the rendering-acceptance
center-normal correction updates its product hashes to:

- geometry `0x0e3762ad8af185aa`;
- climate content `0x84ba91da263d7164`.

Within each regime, geometry hash, raw-center focus, and raw/derived climate
metrics are identical across all three surface models.

## Results

The table below reports product-sampled climate means and final
`climate-transition` surface weights. These means differ from coarse selection
medians because the product uses a polar 16.384 km sampling footprint and the
mean-temperature channel includes local elevation response.

| Regime | T | P | Effective moisture | Growth | Cold | Wet snow | Rock | Snow | Vegetation | Moisture |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| hot/dry | 21.35 C | 203 mm | 0.140 | 1.000 | 0.000 | 0.210 | 0.012 | 0.000 | 0.237 | 0.164 |
| hot/wet | 22.49 C | 2481 mm | 1.762 | 1.000 | 0.000 | 1.000 | 0.000 | 0.000 | 0.603 | 1.000 |
| cool/wet | 3.25 C | 665 mm | 1.097 | 0.924 | 0.341 | 0.928 | 0.026 | 0.271 | 0.010 | 0.982 |
| cold/dry | -14.00 C | 23 mm | 0.059 | 0.356 | 1.000 | 0.000 | 0.023 | 0.000 | 0.000 | 0.036 |
| cold/wet | 1.01 C | 238 mm | 0.445 | 0.958 | 0.524 | 0.312 | 0.014 | 0.159 | 0.191 | 0.785 |

The broad ordering is useful:

- hot/wet is wetter and supports more cover than hot/dry;
- cold/dry suppresses cover and does not manufacture snow without the wet-snow
  signal;
- cold/wet retains substantial moisture and some cover while allocating snow;
- climate input corrects the mineral model's near-total height-driven snow in
  the cool/wet and cold/dry controls.

The evidence also identifies limits:

- hot/wet moisture saturates at 1.0, leaving little visual range;
- cool/wet vegetation is nearly eliminated by the combination of snow and
  landform capacity despite a high thermal-growth mean;
- cold potential still uses annual mean temperature rather than the estimated
  cold-season minimum proposed by the research pass;
- precipitation and precipitation-CV previews visibly retain their 7.68 km
  macro structure and cannot support local ecological detail;
- diagnostic material weights expose polar-sector or topology repetition in
  some near-field views, especially cold/dry; that is a rendering diagnostic,
  not a climate-field defect.

## Decision

The calibration supports using Terrain Diffusion climate as a coherent macro
material control. It does not support promoting categorical biomes or treating
the four fields as complete terrain semantics.

Keep the production default unchanged. The next climate-material batch should
use this frozen pack to evaluate, independently:

1. cold-season potential derived from the same sinusoidal temperature model;
2. range-stable moisture mapping that avoids immediate wet-climate saturation;
3. palette and detail responses that improve visual separation without adding
   fake high-frequency climate;
4. the separate topology repetition visible in diagnostic views.

Hydrology, foliage placement, soil, weather, and elevation modification remain
outside this contract.

## Related Notes

- [Terrain Climate Surface Model Research](terrain-climate-surface-model-research.md)
- [Terrain V1 Runtime](../architecture/terrain-v1.md)
- [Terrain Product Promotion](terrain-product-promotion.md)

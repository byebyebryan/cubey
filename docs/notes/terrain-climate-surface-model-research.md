# Terrain Climate Surface Model Research

Date: 2026-07-21

Status: research complete; implementation direction proposed; production default
unchanged.

## Question

Terrain Diffusion supplies mean temperature, temperature seasonality, annual
precipitation, and precipitation seasonality beside elevation. This pass asks
what those fields can defensibly control in Cubey, whether the current
`climate-transition` model has the right structure, and what evidence is needed
before richer surface semantics are promoted.

The target remains a convincing far-field backdrop. This is not a biome
simulation, a hydrology model, or a foliage system.

## Source Semantics

The public Terrain Diffusion API identifies its four climate outputs with the
WorldClim bioclimatic variables BIO1, BIO4, BIO12, and BIO15:

1. annual mean temperature in degrees Celsius;
2. temperature seasonality as the standard deviation of monthly temperature;
3. annual precipitation in millimeters;
4. precipitation seasonality as the coefficient of variation of monthly
   precipitation.

Cubey's bake converts BIO4 from `standard deviation x 100` to degrees Celsius
and BIO15 from percent to a fraction. The companion field therefore preserves
the documented units.

These are long-term annual climate summaries. They are not current weather,
monthly climate, soil moisture, runoff, snow depth, vegetation, or categorical
biomes. In particular, BIO15 describes how uneven precipitation is through the
year but does not say which months are wet. It cannot tell whether the wet
season overlaps the growing season or the freezing season.

## Generated Field Provenance

Terrain Diffusion does not run a climate simulation for its default synthetic
world. Its synthetic conditioning starts with procedural fields whose marginal
distributions are quantile-matched to 10 arc-minute WorldClim 2.1 data. A
learned coarse diffusion stage then jointly produces elevation and climate.

The returned climate is spatially coarse:

- mean temperature begins with a coarse sea-level baseline and lapse-rate
  field, then applies the generated high-resolution elevation;
- temperature seasonality, annual precipitation, and precipitation
  seasonality are bilinearly sampled from the learned coarse field;
- one coarse cell spans `32 * 8` native samples, or 7.68 km at the pinned 30 m
  model resolution.

Cubey's 240 m climate companion is therefore a convenient sampling raster, not
a claim of 240 m climate information. Mean temperature can contain meaningful
terrain-scale variation because of the elevation adjustment. The other three
channels should remain macro controls. Adding local noise to them would create
detail, but not additional climate information.

## Baked Patch Audit

The current companion is the SHA-bound 256 x 256 field in
`build/dev/assets/terrain/surface-study/`. It covers approximately 61.4 km per
side. Statistics below are from the baked seed-0 field after Cubey's unit
normalization.

| Field | Whole-field mean | Whole-field p02-p98 | Correlation with elevation |
| --- | ---: | ---: | ---: |
| mean temperature | 19.58 C | 2.55 to 28.15 C | -0.828 |
| temperature stddev | 6.30 C | 3.15 to 12.40 C | 0.757 |
| annual precipitation | 211.17 mm | 13.72 to 615.33 mm | -0.046 |
| precipitation CV | 0.578 | 0.279 to 0.895 | -0.386 |

At the selected product location and 16.384 km product radius:

| Field | Mean | p02-p98 |
| --- | ---: | ---: |
| mean temperature | 23.93 C | 18.94 to 26.31 C |
| temperature stddev | 4.21 C | 3.16 to 6.48 C |
| annual precipitation | 121.50 mm | 16.95 to 297.64 mm |
| precipitation CV | 0.627 | 0.334 to 0.855 |

The selected terrain is consistently warm and dry. A sparse arid or semi-arid
cover response is the expected climate-driven result. Tuning this patch into a
temperate green valley would invalidate the imported data rather than improve
the model.

All four fields are highly autocorrelated across the backdrop. Horizontal lag
correlations remain between 0.92 and 0.98 at 7.68 km. This is appropriate for
regional surface transitions and inappropriate as the source of local material
detail.

## Reference Model Review

### WorldClim and categorical climate classes

WorldClim describes these bioclimatic variables as annual trends and
seasonality summaries intended for ecological and species-distribution models.
They are useful predictors, not biome labels.

Koppen-Geiger classes are a poor implementation target for the current
contract. Their boundaries depend on monthly wet/dry timing and warmest or
coldest month values that BIO1, BIO4, BIO12, and BIO15 do not preserve. A hard
Koppen lookup would manufacture information the source does not contain.

### Evapotranspiration and aridity

Long-term precipitation divided by potential evapotranspiration is a useful
continuous moisture axis in Budyko and Holdridge-style models. However, the
standard FAO Penman-Monteith estimate requires radiation, humidity, wind, and
temperature data. Even reduced-data methods require location or radiation and
minimum/maximum temperature. Cubey cannot claim a physical PET estimate from
the four imported fields.

The current quadratic `potential_evapotranspiration` expression was adapted
from Terrain Diffusion's Minecraft classifier. It is a useful visual demand
proxy, but it is not a validated Thornthwaite or Penman-Monteith calculation.

A Holdridge-inspired comparison is possible with the available data: estimate
an annual sinusoidal temperature cycle from BIO1 and BIO4, derive mean
biotemperature, and use precipitation relative to the resulting thermal demand.
On the current baked field this comparison correlates 0.994 with the existing
quadratic ratio. The current model is directionally stable for this sample;
changing formulas alone is unlikely to create a meaningful visual improvement.

### Terrain Diffusion's biome path

Terrain Diffusion's optional Minecraft API derives aridity, growing-season
length, tree-density classes, snow classes, and Minecraft biome IDs. It then
adds independent temperature, precipitation, and snow noise and applies many
hard thresholds.

That code is useful as evidence that the four channels can drive broad surface
semantics. It is not a product contract to port. The added noise is game-scale
variation rather than climate evidence, and the categorical thresholds are
specific to Minecraft presentation.

## Recommended Continuous Contract

Keep climate and landform as separate layers with explicit meanings.

### Climate potential

Derive broad, continuous fields from the companion:

- `thermal_growth`: estimated fraction of the year above a plant-growth
  threshold, using annual mean temperature and temperature seasonality;
- `thermal_extreme`: an approximate cold-season limit from the same sinusoidal
  model, used only as a cold-capable signal;
- `water_supply`: annual precipitation on a logarithmic or otherwise
  range-stable scale;
- `climate_moisture`: precipitation relative to a clearly named thermal-demand
  proxy;
- `seasonality`: precipitation CV retained as a separate uncertainty or stress
  axis, with bounded influence because wet-season phase is unknown.

These names avoid claiming physical soil moisture, actual evapotranspiration,
or observed vegetation.

### Landform capacity

Derive local establishment and exposure from terrain:

- slope limits stable soil and cover;
- broad concavity or valley position increases shelter and retained-material
  potential;
- exposed convex or steep terrain favors rock;
- terrain-frequency material detail remains independent of climate resolution.

Aspect should not enter the first model. The generated world has no reliable
latitude, hemisphere, or seasonal sun relationship, so an aspect response would
be arbitrary.

### Render controls

Combine climate potential and landform capacity into continuous rendering
controls:

- ground-cover potential;
- dry-to-damp substrate;
- exposed rock;
- snow-capable accumulation;
- optional future foliage density and type envelopes.

The outputs should modulate material palettes and detail strength. They should
not modify elevation: Terrain Diffusion already generated terrain conditioned
on the coarse climate, and a second geometry response would double-count that
relationship.

The material layer may add independent fine procedural variation inside a
climate mask. That detail must remain zero-mean and bounded so it does not turn
the macro climate map into noisy pseudo-ecology.

## Explicit Non-Claims

The V1 climate surface model cannot infer:

- rivers, drainage, lakes, or local runoff;
- soil moisture, groundwater, soil depth, or geology;
- current rain, fog, humidity, or cloud state;
- actual snow cover or snow depth;
- species, tree placement, fire regime, land use, or ecological succession;
- a defensible categorical biome at each point.

Those require additional source fields or dedicated systems. Annual
precipitation should not be reused as a river mask or live weather control.

## Current Implementation Audit

Keep:

- the separate SHA-bound climate companion;
- continuous rather than categorical output;
- growing-season estimation from temperature mean and seasonality;
- precipitation-to-thermal-demand as a broad moisture axis;
- landform gating before vegetation or snow material weights;
- mineral control and art-directed landform control as independent comparisons.

Revise before promotion:

- rename the PET/aridity internals so they describe a visual proxy rather than
  a physical estimate;
- derive cold potential from the estimated seasonal minimum rather than annual
  mean temperature alone;
- keep precipitation CV visible as its own diagnostic and reduce assumptions
  about when the dry season occurs;
- expose raw temperature, precipitation, and seasonality diagnostics beside
  the derived controls;
- record the 7.68 km effective macro spacing in companion metadata or design
  documentation;
- validate multiple climate regimes instead of calibrating only the hot, dry
  selected patch.

## Next Evidence Batch

Before changing the production default, build a climate calibration study with
at least five Terrain Diffusion regions:

1. hot and dry;
2. hot and wet;
3. cool and wet;
4. cold and dry;
5. cold and wet.

Select candidates from the coarse climate field before paying for full terrain
generation. Freeze each elevation/climate pair, then compare:

- raw climate channels;
- derived thermal, moisture, seasonality, and cold-potential fields;
- landform capacity;
- final material weights and rendered views.

Acceptance is cross-regime ordering, not ecological precision: wet regions
must not be drier than matched dry regions, cold regions must suppress growing
capacity, steep rock must remain exposed in every climate, and transitions must
remain broad and continuous. The existing seed-0 patch remains the hot/dry
control rather than the sole tuning target.

## Verdict

The imported temperature and precipitation fields are useful, but narrowly.
They can provide coherent macro thermal and moisture potential for materials
and future foliage envelopes. They do not provide local ecology or hydrology.

The present `climate-transition` has the right layer ordering and produces a
reasonable hot/dry response. Its formulas should be treated as rendering
proxies and validated across multiple climates before promotion. The next
highest-value work is a cross-climate calibration pack, not another tuning pass
on the current seed.

## Sources

- Terrain Diffusion checkout at pinned commit
  `82a0431281f21a6ec3d691a12ee61525de5b0790`: `API_README.md`,
  `terrain_diffusion/inference/world_pipeline.py`,
  `terrain_diffusion/inference/synthetic_map.py`, and
  `terrain_diffusion/inference/minecraft_api.py`.
- [WorldClim bioclimatic variable definitions](https://www.worldclim.org/data/bioclim.html).
- [WorldClim 2.1 historical climate data](https://www.worldclim.org/data/worldclim21.html).
- [FAO-56 reference evapotranspiration overview](https://www.fao.org/4/X0490E/x0490e05.htm).
- [FAO-56 meteorological data and reduced-data limits](https://www.fao.org/4/X0490E/x0490e07.htm).
- [FAO ecological zoning summary of Holdridge life zones](https://www.fao.org/4/ac632e/AC632E07.htm).

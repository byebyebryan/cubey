# Terrain Landscape Variations V1

Date: 2026-07-24

Status: accepted as a project-local source catalog; no foundation presets promoted.

## Purpose

The accepted terrain product uses one strong external heightfield as a coherent
far-backdrop source. This study asks whether the pinned Terrain Diffusion model
can also supply several meaningfully different landscapes without custom
conditioning maps.

This is a source-capability study hosted by `projects/terrain`. It does not add
named terrain presets to shared consumers and does not define categorical
biomes. A landscape result combines:

1. generated elevation morphology;
2. generated macro climate fields;
3. Cubey's existing continuous climate surface model.

The source must remain recognizable in fixed-scale height, slope, and clay
views. Color or snow alone cannot establish a distinct terrain type.

## Frozen Producer

The producer contract remains:

- Terrain Diffusion code `82a0431281f21a6ec3d691a12ee61525de5b0790`;
- model `xandergos/terrain-diffusion-30m` at revision
  `9ef8030cb805b433b98ec25c5dddefbac07a9e26`;
- deterministic seeds `0`, `9012`, and `12345`;
- 2048 x 2048 elevation at 30 m spacing;
- 256 x 256 climate fields at 240 m spacing;
- the existing global height offset and scale.

The batch scans each synthetic world at coarse resolution. It does not import
or draw elevation, temperature, or precipitation maps. Each selected region is
generated at full resolution only after coarse qualification.

## Landscape Recipes

All relief values are coarse-window `p90 - p25` land elevation. Temperature and
precipitation are coarse-window land medians.

| ID | Intended reading | Relief | Temperature | Precipitation | Minimum land |
| --- | --- | ---: | ---: | ---: | ---: |
| `alpine-range` | high cold mountain range | 2000-4500 m | at most 2 C | at least 200 mm/year | 90% |
| `temperate-mountain-valley` | temperate mountains around a usable open stage | 1200-2200 m | 5-15 C | 500-1600 mm/year | 95% |
| `dry-upland` | warm arid highland | 900-1800 m | at least 15 C | at most 300 mm/year | 95% |
| `rolling-wet-lowland` | warm or mild wet rolling terrain | 300-900 m | 8-24 C | at least 600 mm/year | 98% |

Eligible candidates are ranked by normalized distance to the center or target
of each recipe. Precipitation uses logarithmic distance. Ties use seed order,
distance from the world origin, and coarse coordinates. A seed/origin pair may
serve only one recipe.

There is no fallback to a nearest unqualified candidate. If the three pinned
worlds cannot satisfy a recipe, the generator fails with the ranked candidate
summary and the study records that limitation. Custom conditioning is a
separate follow-up decision.

## Generated Contract

The explicit target will be:

```sh
cmake --build --preset dev \
  --target cubey_terrain_generate_landscape_variation_assets
```

It writes ignored assets under
`cache/terrain/sources/v1/landscape-variations/`. Each variant contains the
existing heightfield and climate companion manifests and payloads. A
`variation-index.json` binds stable IDs and display labels to relative
manifest paths, source provenance, selection metrics, and content hashes.

The index is a project-local review catalog. It is not a foundation terrain
preset API. The terrain app retains the startup source and frozen climate
calibration controls when the optional catalog is absent.

The accepted deterministic selections are:

| ID | Seed | Coarse origin | Elevation SHA-256 | Climate SHA-256 |
| --- | ---: | ---: | --- | --- |
| `alpine-range` | 12345 | 0, -32 | `5fbf519f9af5...` | `2eb169eefd3f...` |
| `temperate-mountain-valley` | 0 | 120, -112 | `a978ecd435d2...` | `7e96235ec4d6...` |
| `dry-upland` | 9012 | 64, 0 | `7e0d5cfc47ab...` | `36fd8ad536e7...` |
| `rolling-wet-lowland` | 12345 | -96, -56 | `fd52d7c3b25f...` | `9012e73dd078...` |

Generation scanned 2,642 coarse candidates across the three pinned worlds.
The first complete bake took 258.52 seconds, including 49.03 seconds for the
coarse scan, and produced a 76,879,888-byte ignored package. A valid existing
package is reused without loading the model.

## Review Contract

The headless pack is written to
`outputs/terrain/landscape-variations-v1/` and uses matched atmosphere, light,
camera, placement, and surface settings. It includes:

- fixed-scale height and slope previews;
- 500 m clay and climate-surface captures from two headings;
- a 200 m surface stress capture;
- material-weight, vegetation, moisture, and snow diagnostics;
- selection, placement, material, provenance, and timing metrics.

Acceptance requires:

- four unique geometry hashes and four passing selected placements;
- no holes, discontinuities, or source/material registration failures;
- visibly different morphology in height and clay views before material;
- alpine relief and snow above the other variants;
- rolling lowland relief and slope below the other variants;
- wet-lowland moisture and cover above dry upland;
- effectively no dry-upland snow;
- combined terrain atmosphere, shadow, surface, stage, and post mean and p50
  at or below the accepted 1.10 ms budget at 1600 x 900.

P95 remains reported rather than gated.

## Result

The four-source pack is available at
`outputs/terrain/landscape-variations-v1/`. All four sources:

- produced unique source and rendered geometry identities;
- passed selected backdrop placement without discontinuities or registration
  failures;
- stayed below the 1.10 ms combined terrain mean and p50 gate at 1600 x 900;
- remained distinguishable in fixed-scale source height and slope views before
  material.

| ID | Selected local relief | P95 slope | Snow | Vegetation | Moisture | Mean / p50 / p95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `alpine-range` | 28.84 m | 0.131 | 0.244 | 0.000 | 0.880 | 0.932 / 0.923 / 0.981 ms |
| `temperate-mountain-valley` | 91.43 m | 0.260 | 0.020 | 0.140 | 0.978 | 0.944 / 0.939 / 1.026 ms |
| `dry-upland` | 37.03 m | 0.183 | 0.000 | 0.102 | 0.050 | 0.914 / 0.921 / 0.947 ms |
| `rolling-wet-lowland` | 16.29 m | 0.058 | 0.002 | 0.608 | 1.000 | 0.965 / 0.965 / 0.973 ms |

The source study succeeds. Terrain Diffusion can provide useful deterministic
landscape variation without custom conditioning, and the existing continuous
surface model responds to its climate fields in the intended direction. This
earns the four variants a project-local catalog for review and future scene
work, not named biome semantics or a shared consumer API.

The comparison also exposes current rendering limits. Alpine snow and
atmospheric haze flatten some mid-distance detail. Rolling wet lowland is a
credible low-relief backdrop but visually sparse without foliage geometry.
Clay mode mainly validates the silhouette because it is unlit; fixed-scale
height and slope remain the stronger morphology evidence. These are rendering
and content follow-ups rather than reasons to reject the source set.

## Explicit Deferrals

- custom Terrain Diffusion conditioning maps or SNR studies;
- hydrology, rivers, lakes, wetlands, and coastlines;
- erosion or geometry post-processing;
- foliage geometry and ecological biome classification;
- per-variant material formulas or palette tuning;
- public foundation presets and wider consumer integration.

## Commits

1. `docs(terrain): define landscape variation study`
2. `feat(terrain): bake deterministic landscape variants`
3. `feat(terrain): expose landscape variation catalog`
4. `test(terrain): add landscape variation review pack`

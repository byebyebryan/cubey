# Terrain Landscape Variations V1

Date: 2026-07-24

Status: implementation planned; evidence pending.

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

## Explicit Deferrals

- custom Terrain Diffusion conditioning maps or SNR studies;
- hydrology, rivers, lakes, wetlands, and coastlines;
- erosion or geometry post-processing;
- foliage geometry and ecological biome classification;
- per-variant material formulas or palette tuning;
- public foundation presets and wider consumer integration.

## Planned Commits

1. `docs(terrain): define landscape variation study`
2. `feat(terrain): bake deterministic landscape variants`
3. `feat(terrain): expose landscape variation catalog`
4. `test(terrain): add landscape variation review pack`

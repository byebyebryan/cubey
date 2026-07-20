# Terrain Source Bakeoff V1

Date: 2026-07-10

## Decision

Keep `upland-catchment-v1` revision 2 as the default historical baseline and
keep `upland-broad-noise-control-v1` as a low-complexity comparison source. Do
not promote either source as the intended mountain model.

The contour baseline proves that a generated field can still encode an
artificial construction. Its narrow crests are the `0.5` iso-contours of local
value noise, so they form cell-like loops, thin fins, and stepped shoulders
without any authored line geometry. The broad-noise control removes that
construction and demonstrates cleaner mass continuity, but its fBm composition
remains rolling and under-structured. It does not produce a convincing
uplift-to-range-to-ridge-to-valley hierarchy.

The next source experiment should use broad initial elevation and explicit
uplift as inputs to a stream-power or analytical erosion process. It should not
add another ridge-profile transform to either current source.

## Comparison Contract

The bakeoff holds seed, world extent, grid spacing, hydrology, renderer, and
fixed diagnostic ranges constant. It compares:

- `upland-catchment-v1`, generator revision 2: corrected 64-bit seed handling
  over the preserved contour-ridge source;
- `upland-broad-noise-control-v1`, generator revision 1: OpenSimplex2S uplift,
  macro-mass, and relief fields with no ridge, contour, terrace, or local mask;
- seeds `0`, `9012`, and `12345` at `257x257`, `32 m` spacing;
- seed `9012` over a `769x769` regional frame, approximately `24.6 km` square.

The artifact manifest is `cubey.terrain.patch.v2`. Known fields use fixed
physical display ranges, and every field records distribution percentiles.
Review metrics include fill coverage and volume plus a slope-weighted,
16-direction source-gradient histogram. These are diagnostics, not standalone
realism scores.

## Patch Measurements

| Recipe | Seed | Height span m | Support mean | Fill mean m | Fill p95 m | Fill >10 m | Fill >50 m | Orientation ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Contour baseline | 0 | 1153 | 0.560 | 8.52 | 62.41 | 16.3% | 6.84% | 1.35 |
| Contour baseline | 9012 | 1967 | 0.828 | 6.64 | 49.17 | 12.8% | 4.91% | 1.29 |
| Contour baseline | 12345 | 1921 | 0.750 | 17.55 | 122.25 | 18.3% | 12.50% | 1.68 |
| Broad-noise control | 0 | 1087 | 0.257 | 4.27 | 38.36 | 10.3% | 3.34% | 1.41 |
| Broad-noise control | 9012 | 1813 | 0.296 | 0.87 | 3.24 | 3.15% | 0.12% | 1.61 |
| Broad-noise control | 12345 | 1660 | 0.279 | 1.07 | 5.94 | 4.07% | 0.00% | 1.52 |

Over the regional frame, the broad-noise control reduces mean routing fill
from `35.61 m` to `10.11 m`, p95 fill from `232.95 m` to `81.80 m`, and area
filled deeper than `50 m` from `19.13%` to `7.68%`. Regional orientation ratio
changes from `1.29` to `1.22`. The source change therefore reduces enclosed
basin pressure and grid bias slightly, but it does not solve routing.

## Visual Findings

- The contour source still exposes narrow loops, rectangular shoulders, and
  isolated sharp crests in source and slope views.
- The broad-noise source replaces those fins with continuous rolling masses,
  but the surface lacks strong peaks, broad ridges, incised valleys, and clear
  mountain-range hierarchy.
- Fine fBm octaves appear as filament-like slope texture without being coupled
  to macro drainage or uplift structure.
- Both contributing-area views contain planar routing regions, straight
  boundaries, and striped flats. The broad source cannot correct the current
  priority-fill and gradient-sector routing model by itself.
- Fixed display ranges make seed elevation and fill severity directly
  comparable; the previous patch-relative views understated these differences.

## Outputs

Run:

```sh
studies/terrain/hydrology/capture_review.sh
```

Current ignored outputs are under `outputs/terrain_hydrology_lab/source-bakeoff-v1/`:

- `terrain-source-bakeoff-contact-sheet.png`;
- `terrain-source-bakeoff-surface-sheet.png`;
- `terrain-source-bakeoff-regional-sheet.png`;
- `comparison-summary.json` and per-patch v2 manifests.

Use `studies/terrain/hydrology/capture_v1_baseline.sh` only to reproduce the earlier
single-recipe review layout.

## Next Boundary

The next batch should add one regional uplift-plus-erosion candidate while
keeping these two sources as controls. Generate process truth at one canonical
regional resolution and publish `uplift_potential`, `initial_height_m`,
`process_delta_m`, and `height_m` separately. True D-infinity facets, explicit
flat resolution, and basin classification remain the following hydrology
boundary; do not paint rivers over the current routing output.

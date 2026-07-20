# Terrain Landscape Evolution V1 Review

Date: 2026-07-10

## Result

`upland-landscape-evolution-v1` is now a working finite-region analytical
terrain candidate. It is a useful known midpoint and process-field producer,
not production terrain. The three-seed review passes sink, basin-cliff,
orientation, and deep-fill gates. One of 18 automated checks remains outside
the declared oracle tolerance: slope `p95` differs by `27.5%` against a `25%`
limit.

The decisive implementation correction was the full lower-neighbor gradient
factor. A centered finite difference erased ridge-top gradient and over-eroded
the high elevation tail. Computing the gradient from all lower direct
neighbors now reproduces the oracle's elevation distribution without changing
the fixed physical parameters.

## Evidence

The canonical region is `513x513` at `100 m` with a hidden 64-sample guard.
The review command is:

```sh
studies/terrain/hydrology/capture_landscape_evolution_v1.sh
```

Ignored evidence is written to
`outputs/terrain_hydrology_lab/landscape-evolution-v1/review/`. The main artifacts are:

- `landscape-evolution-macro-sheet.png`: source, evolved height, slope, and
  process drainage for seeds `0`, `9012`, and `12345`;
- `landscape-evolution-process-sheet.png`: physical inputs, advection terms,
  thermal activation, analytical height, correction, and deltas;
- `landscape-evolution-comparison-sheet.png`: broad control versus evolved
  terrain;
- `landscape-evolution-render-sheet.png`: oblique, surface-low, and profile
  renderer checks;
- `review-summary.json`: machine-readable gates and oracle comparison.

| Seed | Height p05 / p50 / p95 (m) | Slope p95 | Anisotropy | Severe basin cliffs | Thermal active |
| ---: | ---: | ---: | ---: | ---: | ---: |
| `0` | `440 / 782 / 1197` | `0.589` | `1.078` | `0.026%` | `7.28%` |
| `9012` | `409 / 707 / 1213` | `0.621` | `1.086` | `0.049%` | `9.06%` |
| `12345` | `413 / 715 / 1151` | `0.600` | `1.104` | `0.029%` | `7.97%` |

Every seed has zero unresolved process sinks. Common-hydrology fill coverage
above `50 m` is zero for every evolved patch, compared with `15.95%` to
`20.13%` for the broad controls.

For seed `9012`, height `p05/p50/p95` differs from the guarded external oracle
by `0.63% / 0.55% / 0.08%`. Slope differs by
`3.87% / 19.25% / 27.50%`. The final slope tail therefore remains a measured
open issue rather than being hidden by loosening the acceptance threshold.

## Visual Review

What works:

- broad source masses become connected ridge and valley systems rather than
  isolated authored features;
- each seed preserves a distinct macro layout while producing comparable
  distributions;
- the 64-sample guard keeps the visible region from reading as a square
  watershed boundary;
- process drainage is connected and altitude correction avoids broad
  cross-basin cliffs;
- surface-low and profile captures stay above the mesh and expose actual
  relief.

What remains weak:

- four-neighbor river trees leave cardinal runs, right-angle tributaries, and
  terraced comb texture in top and slope views;
- the steepest uplift mass has a heavier slope tail than the oracle;
- the simple surface material compresses relief and makes the 3D view read
  softer than the scalar fields;
- this stage forms macro terrain only; it has no residual detail, LOD, water,
  climate, material, or vegetation contract.

## Decision

Keep the recipe and process fields as the analytical baseline. Do not tune away
the routing texture with smoothing or authored masks. The next terrain design
pass should decide whether the production model keeps a raster four-neighbor
tree, adopts a less axis-bound routing representation, or uses this solver only
as a macro field followed by a separate multi-scale process. That decision
should precede fine-detail amplification.

# Fluid 2.5D rain-pool terrain screen V1 (2026-09-24)

Status: bounded diagnostic study. No terrain fixture, rain demo, solver behavior,
rendering default, or source elevation was promoted or changed. This pass
followed the [rain catchment study](fluid-25d-rain-catchment-study-v1.md): a
hydrologically plausible outlet was not visually legible as a one-outlet event,
so the next question was whether an immutable terrain crop might instead show
rain collecting into a visible interior pool.

## Fixed question and evidence layers

The unchanged Fluid pilot starts dry, rains uniformly at 60 mm/h for 600 s
(10 mm rain excess per cell), then runs 600 s dry. It uses the finite-volume
solver, 2 s outer steps, eight substeps, native 30 m spacing, a 256x128 crop,
and open outflow on every crop edge. The 60 mm/h forcing is an existing
visibility stress test, not a calibrated storm. A pool is not established by
terrain analysis, a positive global maximum depth, or a wet-cell count:
location, connected footprint, time evolution, and presentation must agree.

The reusable
[`terrain_rain_pool_screen_v1.py`](../../projects/fluid/fluid_25d/terrain_rain_pool_screen_v1.py)
screen reads all 12 pinned 2048x2048 Terrain Diffusion elevation payloads
directly from their immutable cache manifests and checks the elevation hashes.
It priority-fills a *derived copy* with map-edge outlets, finds 8-connected
areas where fill exceeds 1 cm, and seeds the global raw-elevation minimum of
each area. It measures connected footprints at 5 and 10 cm above that floor,
while requiring the stage to remain below the derived fill/spill proxy. Stage
storage is the sum of stage-minus-raw elevation over those cells. A deliberately
optimistic rain budget assumes all 10 mm falling on the footprint **and** a
three-cell (90 m) apron reaches the pool, without losses. This is not a routed
contributing area or a water simulation. The output is
[`screen.json`](../../outputs/fluid/rain-pool-screen-v1-20260924/screen.json).

Across the 12 maps, 20 global-floor stage footprints reach at least 25 cells;
13 also pass the optimistic rain-budget, non-edge, and crop-margin gates. Only
two of those reach 100 cells. Both are on the pinned rolling-wet-lowland map
(elevation SHA-256
`fd52d7c3b25f139ac709b8ced673ccc77d749cc33e4c52e66745494a86dcf7a2`):

| Site | Crop origin | Screened stage / cells | Storage proxy | Rain on footprint | Optimistic apron rain | Raw crop p95-p05 relief |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| A | `(548,190)` | 5 cm / 107 | 2,036 m3 | 963 m3 | 3,168 m3 | 36.6 m |
| B | `(8,1788)` | 10 cm / 102 | 2,586 m3 | 918 m3 | 2,736 m3 | 30.0 m |

The direct rain is only 0.47x and 0.35x the respective storage proxy. Their
budget pass therefore depends on near-perfect capture of nearby runoff. Site
A's 5 cm stage is only 0.91 cm below its derived spill proxy. Each candidate
footprint occupies roughly 0.3% of a 32,768-cell crop, so even a real local
pool may remain small in a full-scene view. The most relief-rich crops in this
bounded screen offered smaller or budget-deficient footprints.

This screen does **not** exhaust possible basins: it seeds only the global
raw-minimum plateau of each >1 cm derived-fill component. Secondary local
minima and nested subbasins can be missed. Nor does the priority-filled copy
modify the simulator terrain. The optional prior hydro-read report is a
cross-check, not a prerequisite for reproducing the screen.

## Two unchanged Fluid pilots

The two shortlisted sites were run on the same built app (SHA-256
`3ab868e0708673e180cd4d5c3ad7e110ab639f2749784800dd27e9c09566fc53`).
The crop identity hashes, full command shape, exact profile files, and image
provenance are in the ignored
[`pilot summary`](../../outputs/fluid/rain-pool-top2-pilot-v1-20260924/summary.txt).
Composite used only the existing render-only thin-water option, height scale
0.6, and home camera distance 5500 m; the paired WaterDepth view shared the
same simulation settings. Same-frame Composite and WaterDepth metrics files
are byte-identical at both times for both crops.

| Site / elapsed | Water stored | Crop-wide max depth | Active-flow cells | Boundary outflow | Conservation residual |
| --- | ---: | ---: | ---: | ---: | ---: |
| A / 600 s | 289,388 m3 | 0.166 m | 403 | 5,526 m3 | 7.203 m3 |
| A / 1,200 s | 282,718 m3 | 0.503 m | 146 | 12,196 m3 | 7.209 m3 |
| B / 600 s | 288,234 m3 | 0.128 m | 395 | 6,681 m3 | 8.655 m3 |
| B / 1,200 s | 274,489 m3 | 0.361 m | 819 | 20,426 m3 | 8.284 m3 |

All four snapshots have zero solver-status flags, zero sinks, the same
294,907 m3 cumulative rain source, and residual/source below 0.003%. The
crop-wide maximum rises during the dry period, but its location is not
reported; it cannot be assigned to the screened floor. Every cell is
numerically wet because the whole crop received direct rain, so wet-cell
count is not an accumulation metric. Site B also sits only eight cells from
the source map's west edge, an additional context limit.

The [A depth view at 600 s](../../outputs/fluid/rain-pool-top2-pilot-v1-20260924/A-depth-f0300.png)
and [1,200 s](../../outputs/fluid/rain-pool-top2-pilot-v1-20260924/A-depth-f0600.png)
show branching traces and some brightening, but the
[A Composite](../../outputs/fluid/rain-pool-top2-pilot-v1-20260924/A-composite-f0600.png)
does not read as a growing pond. The corresponding
[B depth](../../outputs/fluid/rain-pool-top2-pilot-v1-20260924/B-depth-f0600.png)
and [Composite](../../outputs/fluid/rain-pool-top2-pilot-v1-20260924/B-composite-f0600.png)
are still more mottled and subtle. Floor-marked visual aids are available for
[A](../../outputs/fluid/rain-pool-top2-pilot-v1-20260924/A-target-window-f0600.png)
and [B](../../outputs/fluid/rain-pool-top2-pilot-v1-20260924/B-target-window-f0600.png).
These are qualitative screenshots, not local water-depth readbacks. Neither
site establishes a coherent, growing pool at its screened floor.

## Verdict and next gate

Searching the existing 12 pinned 30 m maps did find small terrain pockets;
the two best size/budget candidates nevertheless fail the intended *visible
pool* story under the unchanged 20-minute rain-and-drain protocol. This is a
negative result for these candidates and this bounded screen, **not** a claim
that Terrain Diffusion lacks all usable basins. Keep the current rain result
labelled as distributed runoff/channel convergence, not rain filling a lake.

For a new pond/lake attempt, predeclare a site and story gate before changing
forcing: select a depression whose connected stage covers a meaningful share
of the intended view, whose measured nearby contributing area and event time
can plausibly supply its storage, and whose local water depth/connected area
can be read back over time. Extend the terrain screen to secondary minima or
try a different, finer/local terrain source as a separate site-search protocol.
Only then run another fixed Fluid pilot and review top-down plus oblique views.
Do not tune rainfall or water color until the physical accumulation is
measured at the proposed pool.

## Reproduction

From the repository root, use a fresh output directory (the scanner refuses
to overwrite existing evidence):

```sh
rtk proxy env PYTHONDONTWRITEBYTECODE=1 uv run --python 3.12 \
  projects/fluid/fluid_25d/terrain_rain_pool_screen_v1.py \
  --output-dir outputs/fluid/rain-pool-screen-v1-<new-run>
rtk proxy env PYTHONDONTWRITEBYTECODE=1 uv run --python 3.12 \
  projects/fluid/fluid_25d/test_terrain_rain_pool_screen_v1.py
```

The five focused synthetic tests cover stage volume, clipped donor area,
crop/source-edge handling, source inventory digest, malformed optional
cross-checks, and deterministic repeatability. They passed on this pass.

# Fluid 2.5D Terrain Diffusion hydrology read V1 — 2026-09-24

## Decision

The pinned Terrain Diffusion corpus **does contain topographically convergent,
connected drainage networks**. The earlier scarcity of usable *gentle,
bank-contained river reaches* is a stricter site-selection problem, not evidence
that the generator has no river-like landforms. Keep the existing lowland reaches
as conditional discovery candidates; do not promote a natural river fixture,
paint the drainage lines as water, or change the terrain or Fluid defaults.

This is an offline reading of immutable elevation. It answers *where unit runoff
would tend to collect if every cell contributed equally and depressions could
spill to the map edge*. It does not answer whether water is present, what its
depth or speed would be, whether a patch is a permanent lake, or whether a
particular reach will hold a visibly full river under the Fluid solver.

## Review images and how to read them

- [Twelve-map overview](../../outputs/fluid/terrain-hydro-read-v1-corpus-visible-20260924/corpus-review-overview.png): each pair uses the same display scale. Blue on the left is *upstream contributing area*, not a water mask; the darkest major trunks are widened **for display only**. Yellow/orange/red on the right is the depth of a *derived fill-to-spill copy*, not observed lake water.
- [Lowland three-panel close view](../../outputs/fluid/terrain-hydro-read-v1-corpus-visible-20260924/rolling-wet-lowland-seed12345-fd52d7c3/contact-sheet.png): left is the connected D8 route; middle is an **invalid-for-ranking** MFD sensitivity view with unresolved interior sinks; right is derived fill depth. Pink rectangles are the two prior morphology-only reach crops.
- [Real-DEM positive control](../../outputs/fluid/terrain-hydro-read-real-control-20260924/review/real-control-overlay.png): red is a published mapped stream/river line, cyan is the reader's >=1 km2 route, yellow is a derived route within two cells of a mapped line. This is a route-alignment sanity check, not field-flow validation.

Map edges are computational outlets, not actual rivers or sea level. No source,
drain, rainfall history, infiltration, discharge, stage, or water surface is
specified by these maps.

## Method and gates

The [reader](../../projects/fluid/fluid_25d/terrain_hydro_read_v1.py) loads the
exact pinned cache inventory through the previous reach scanner: 16 manifests,
4 aliases and 12 unique 2048x2048, 30 m elevation payloads. It checks the
source hashes, applies each manifest's height transform and never writes back
to the cache. On a derived copy, PyFlwDir's priority flood and through-flat D8
route make a connected unit-runoff contributing-area map. Review cutoffs are
fixed at 0.25, 1, 4 and 16 km2; none is calibrated to perennial streamflow.
Positive fill patches are reported as basin/storage *proxies* with area, fill
volume, maximum fill depth, derived spill elevation and bounding box. Their
8-connected grouping can merge nested basins.

The [four synthetic controls](../../projects/fluid/fluid_25d/test_terrain_hydro_read_v1.py)
check input rejection, open-slope drainage and unit-area conservation, a closed
bowl's exact fill storage, and edge-terminal detection. The first pinned
lowland map then passed the gate: zero interior D8 terminals and exact terminal
area balance. This establishes internal routing consistency, **not physical
correctness**. A preliminary PySheds flat-resolution D8 route stranded 64.47%
of unit runoff in interior terminals on this map. Its MFD panel is therefore
retained only as a visibly labeled sensitivity image, never for ranking.

The independent [real-data control](../../projects/fluid/fluid_25d/terrain_hydro_real_control_v1.py)
uses the 450x500, 30 m NED GeoTIFF and `FTYPE=STREAM/RIVER` lines from the
[published North Carolina sample data](https://grassbook.org/datasets/datasets-3rd-edition/).
The input archives, extracted selected files and hashes are retained under the
ignored `outputs/fluid/terrain-hydro-read-real-control-20260924/` tree. At the
1 km2 threshold, 3,898 derived route cells were found; 83.20% fall within two
cells of a mapped stream. Conversely, only 23.30% of all mapped stream cells
are near that >=1 km2 route, consistent with the threshold omitting headwaters.
At 0.25 km2 those fractions are 82.90% and 41.66%. The control also has zero
interior terminals and exact terminal area balance. A second run reproduced
the metrics and overlay hash. The published lines are an independent *positive
control*, but are not exact cell-scale flow truth. [PyFlwDir documents the
fill/edge-outlet operation](https://deltares.github.io/pyflwdir/latest/_generated/pyflwdir.dem.fill_depressions.html);
[USGS guidance emphasizes preserving true sinks](https://www.usgs.gov/ngp-standards-and-specifications/elevation-derived-hydrography-data-acquisition-specifications-15),
which this exploratory edge-spill copy does not classify.

## Corpus read and candidates

All 12 unique payloads passed the same routing checks: zero interior D8
terminals and exact unit-area balance. Maximum map-level contributing area
ranges from 658 to 2,379 km2. Derived positive fill occupies 3.43–14.79% of
cells across maps. These are discovery metrics; forced depression spill can
manufacture a through-route or a large fill patch, especially near an edge.
Several high-relief fill patches require tens to hundreds of metres of derived
fill, so coloring them as lakes would be particularly misleading. All per-map
numbers, image hashes, source paths/hashes, tool versions and reader hash are in
the [corpus summary](../../outputs/fluid/terrain-hydro-read-v1-corpus-visible-20260924/summary.json)
and each case's `result.json`.

The lowland map has the strongest maximum route (2,379 km2). The broad prior
reach at `(x384,z837,256x128)` contains a route up to 112 km2; the narrow reach
at `(x843,z809,256x128)` contains one up to 784 km2. This strengthens their
*catchment context* but does not repair the [prior finding](fluid-25d-natural-reach-study-v1.md)
that the narrow reach is about three cells wide and has a locally weak
downstream bank, or that neutral 12 mm/hour rain produced only thin water.
Neither is yet a validated bank-contained, end-to-end river.

For a separate possible lake study, the lowland map contains two among its 12
largest-by-volume analytical fill patches that are relatively shallow:
`bbox_xz=[844,297,1284,580]`, 34.39 km2, 4.70 m maximum fill; and
`[608,140,1020,354]`, 9.95 km2, 4.08 m maximum fill. These are **not lake
sites** yet: the grouping, spill saddle, source water budget, persistence and
true-sink status remain unverified. The top-12-by-volume report is not an
exhaustive shallow-basin inventory.

## Next bounded gate

1. Trace a *shorter* segment on the lowland connected route and compare its
   raw (unfilled) bed, cross-sections, downstream saddle and both bank heights
   at one proposed target stage. Reject it if the desired water level spills
   out before reaching the intended outlet. Keep the broad and narrow crops
   as reference controls, not automatic winners.
2. If a contained segment passes, run a **separate opt-in** source/outlet Fluid
   audition with visibly marked endpoints and report steady inflow/outflow,
   bank containment and dye/floatable transit. Do not infer these from the
   static hydrology map or from the previous neutral rain run.
3. For a rain/collection demo, define rainfall, loss and outlet assumptions;
   then compare time-dependent runoff and ponding against the static drainage
   hypotheses. Classify candidate true sinks before rendering lake water.

This closes the offline read gate, not fixture selection. No terrain was
modified, no water was simulated in this pass, and no site, inlet/outlet,
shader or solver default was promoted. Validation here is focused Python,
synthetic and independent real-data control; it is not a Fluid GPU or field
hydrology validation. No commit or push was requested.

## Replay

```sh
rtk proxy uv run --python 3.12 projects/fluid/fluid_25d/test_terrain_hydro_read_v1.py
rtk proxy uv run --python 3.12 projects/fluid/fluid_25d/terrain_hydro_read_v1.py --output-dir outputs/fluid/hydro-first-replay
rtk proxy uv run --python 3.12 projects/fluid/fluid_25d/terrain_hydro_read_v1.py --all-pinned --output-dir outputs/fluid/hydro-corpus-replay
rtk proxy uv run --python 3.12 projects/fluid/fluid_25d/terrain_hydro_overview_v1.py --corpus-dir outputs/fluid/hydro-corpus-replay
rtk proxy uv run --python 3.12 projects/fluid/fluid_25d/terrain_hydro_real_control_v1.py --output-dir outputs/fluid/hydro-real-control-replay
```

The real-control command requires the published GeoTIFF and SHAPE inputs in
`outputs/fluid/terrain-hydro-read-real-control-20260924/inputs/`. Each runner
refuses an existing output directory or overview file. Use a fresh replay
path; do not overwrite the recorded evidence.

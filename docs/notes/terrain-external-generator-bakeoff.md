# Terrain External Generator Bakeoff

Date: 2026-07-18

Status: planned comparison. No external source is a terrain v1 runtime
dependency or production default.

## Decision

Evaluate Terrain Diffusion as an offline heightfield producer rather than
continuing to tune another clean-room mountain noise composition. The study
will generate fixed fields outside Cubey, load elevation through the existing
`TerrainHeightSource` study boundary, and compare it with the frozen v2.1 and
current graduated-mountain controls through one renderer.

The batch stops at a `promote`, `reference`, or `reject` verdict. A passing
result does not authorize model inference in the render loop, replace
`mountains-hierarchy-v2`, or select an ONNX/C++ deployment path.

## Pinned Reference

The comparison uses:

- code checkout `~/code/ref/terrain-diffusion` at
  `82a0431281f21a6ec3d691a12ee61525de5b0790`;
- model `xandergos/terrain-diffusion-30m` at
  `9ef8030cb805b433b98ec25c5dddefbac07a9e26`;
- the model's native `30 m` sample spacing and pretrained synthetic
  conditioning defaults;
- CUDA inference in Python 3.12, fp32, direct caching, latent batch size one,
  and no `torch.compile` for the first correctness-oriented bakeoff. Batch size
  one is intentional: the upstream batch-of-four path changed reverse-order
  results by up to `0.0198 m`, while this study requires order-stable fields.
  The exporter also resets Python, NumPy, and Torch RNGs from the world seed
  before each build. This works around the pinned reference's `seed or
  random.randint(...)` handling, which otherwise makes seed zero vary between
  process launches.

The upstream code and model identify MIT licensing. The model card identifies
Copernicus GLO-30 as training data. Cubey records the model card, revisions,
resolved Python packages, generated-content hashes, and machine details, but
does not commit or redistribute model weights or generated fields.

## Field Selection

Terrain Diffusion produces an unbounded world rather than a mountain-only
preset. A fixed origin can therefore land in ocean or low relief and answer the
wrong question. The study may locate a useful mountain region, but it may not
pick one by eye or supply an authored mask.

For each seed, the exporter evaluates a fixed `7 x 7` grid of non-overlapping
`8 x 8` coarse-cell windows. One coarse cell covers `256` native samples, so a
candidate covers `2048 x 2048` samples or `61.44 km` per side. Candidates with
at least `80%` positive-elevation samples are ranked lexicographically by:

1. positive-elevation `p90 - p25` relief;
2. positive-elevation `p90`;
3. distance to the world origin;
4. integer world coordinates.

If no candidate reaches the land threshold, land coverage becomes the first
ranking key. Every candidate score and the selected coordinates remain in the
manifest. This measures whether a deterministic world source can supply a
mountain backdrop without visual cherry-picking.

## Artifact Contract

Seeds are `0`, `9012`, and `12345`. Each selected field is generated as four
`1024 x 1024` quadrants with a 64-sample context halo and is stored as
little-endian float32 data. The context guard accounts for the upstream
shape-local Laplacian reconstruction; consistency is measured in the central
eight samples on each side of the actual quadrant join rather than at the
outer query boundary:

- elevation in row-major `[z][x]` order;
- temperature, temperature variability, precipitation, and precipitation
  variability in channel-major order;
- a JSON manifest containing grid placement, units, revisions, generation
  settings, hashes, candidate selection, timings, and memory telemetry.

The raw values are source truth. Rendering uses one affine calibration shared
by all three diffusion seeds: aggregate raw `p05` maps to `0 m` and raw `p95`
maps to `3500 m`, with no clipping and no per-seed normalization. Climate is
preserved for later biome/material work but is not consumed by this renderer.

The ignored pack belongs under
`outputs/terrain/terrain-diffusion-bakeoff-v1/`. Existing source-model packs
remain frozen.

## Comparison Contract

The comparison lanes are:

- `control-v2-1`, the frozen dramatic silhouette control;
- `mountains-hierarchy-v2`, the current graduated mountain source;
- `terrain-diffusion-30m`, loaded from the baked raster.

All lanes use seeds `0`, `9012`, and `12345`, the `hard-cut-v1` profile,
`3.2-16.384 km` visible extent, high cached topology, render stride one,
weathering off, identical cameras, identical lighting, and identical
materials. Height and slope sheets are reviewed before clay silhouettes;
surface presentation is reviewed last.

Generation must be random-access consistent. Seed `0` is regenerated in
reverse quadrant order after clearing the cache. Core and overlap values must
match within `1e-4 m`; any tile seam or order-dependent field fails the study.

## Acceptance

The source is eligible for a later productization batch only when:

- at least two seeds improve broad mass, terrain-scale ridge body, summit
  hierarchy, and valley coherence over both controls;
- no seed exposes a tile seam, dominant grid/diagonal direction, thin fins,
  cone clusters, repeated templates, or flat elevation shelves;
- one post-load seed, including region selection, four detailed tiles, and
  climate export, completes within five minutes without exhausting the local
  16 GB GPU;
- the raw diagnostics and common renderer tell the same morphology story.

Latency and memory are evidence, not render-loop budgets. The baked source is
sampled only while constructing the existing cached terrain product, so the
runtime renderer must remain source-independent.

## Deferred

- ONNX export or native inference;
- an engine-level terrain asset/cache format;
- runtime generation or streaming;
- custom/Azgaar conditioning;
- climate-driven material or biome integration;
- hydrology, lakes, water rendering, vegetation, and near-field detail;
- changing the terrain product default.

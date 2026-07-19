# Terrain External Generator Bakeoff Review

Date: 2026-07-19

Status: complete. Verdict: **reference**. Terrain Diffusion is not promoted to
the terrain v1 source, cached product, or runtime dependency.

## Result

The pinned Terrain Diffusion fields are a substantially stronger morphology
reference than either internal control. Their height and slope sheets show
connected mountain mass, broad valleys, terrain-scale ridge bodies, and
dendritic detail instead of v2.1's dense cone-like roughness or
`mountains-hierarchy-v2`'s smooth blobs. This validates the external-generator
direction and argues against another clean-room mountain-noise tuning loop.

They are not yet a usable backdrop product. All three diffusion seeds become
low rolling horizons in the common clay and surface lanes even though their
top fields contain strong mountain structure. The deterministic crop ranks a
whole `61.44 km` field by land coverage and relief; it does not select a clear
foreground with useful relief in the supported `3.2-16.384 km` viewing band.
The bounded common stage planner keeps every query valid, but placement and
camera clearance do not recover the source morphology in presentation.

The raw diagnostics and common renderer therefore do not tell the same story.
That fails an explicit promotion gate. Treating the field as production-ready
would move the unresolved problem from source generation into opaque crop and
camera tuning.

## Evidence

The maintained pack is
`outputs/terrain/terrain-diffusion-bakeoff-v1/`. Review it in the order stated
by its `REVIEW.md`:

1. `height-contact-sheet.png` and `slope-contact-sheet.png` establish source
   morphology before rendering.
2. `seam-contact-sheet.png`, `seam-validation.json`, and
   `repeat-validation.json` establish field integrity.
3. `clay-seed-*.png` expose silhouette and placement without material cover.
4. `presentation-seed-9012.png` checks common-renderer compatibility last.

The final run used the pinned code/model revisions, Python 3.12, fp32, direct
caching, latent batch size one, and no Torch compilation. Results were:

- seed generation: `45.33 s` for seed 0 including its reverse-order rebuild,
  `22.56 s` for seed 9012, and `21.50 s` for seed 12345;
- peak CUDA allocation/reservation: `2.31/3.21 GB`;
- peak host RSS: about `3.29 GiB`;
- ignored generated field pack: about `294 MiB` including elevation, four
  climate channels, manifests, and previews;
- all central quadrant joins: `0 m` maximum difference at a `1e-4 m` gate;
- a fresh-process repeat: byte-identical elevation and climate payloads for
  all three seeds.

The exporter had to make two correctness accommodations explicit:

- batch-of-four inference changed reverse-order output by up to `0.0198 m`, so
  the correctness run uses latent batch size one;
- the pinned synthetic-map factory treats seed zero as false and substitutes
  `random.randint(...)`, so the exporter resets Python, NumPy, and Torch RNGs
  from the world seed before each build.

These are captured behavior, not upstream edits. Model weights, generated
fields, climate data, and Python environments remain ignored artifacts.

## Acceptance

| Gate | Result |
| --- | --- |
| Better broad mass, ridges, valleys, and summit hierarchy in at least two seeds | Pass |
| No Cubey quadrant seam or tile-order dependence | Pass |
| Fresh-process repeatability | Pass after explicit zero-seed RNG handling |
| One seed completes within five minutes on the local 16 GB GPU | Pass |
| Raw diagnostics and common renderer show the same morphology | Fail |
| Eligible for terrain v1 productization | No |

## Decision Boundary

Keep the following as study infrastructure:

- the pinned offline exporter and provenance manifest;
- the immutable raster `TerrainHeightSource` adapter;
- bounded stage-search requests for finite sources;
- common height, slope, seam, climate, clay, and presentation captures.

Do not:

- add Python, Torch, model weights, or diffusion inference to Cubey runtime;
- replace the frozen terrain v1 source or radial backdrop product;
- hide the presentation failure with an authored valley mask;
- resume tuning internal mountain operators as though the external result had
  not demonstrated a stronger source family.

## Follow-Up

The next generator comparison should separate two contracts that this bakeoff
combined:

1. **Terrain asset quality:** can a maintained CLI/library/data source produce
   coherent, deterministic heightfields with useful scale hierarchy?
2. **Backdrop composition:** can deterministic, stage-aware crop selection find
   a low foreground and useful directional or radial relief without modifying
   the source heightfield?

Terrain Diffusion remains a valid candidate for the first contract and a
useful visual oracle. Revisit it only through an offline asset pipeline with a
stage-aware selection score, then rerun the unchanged common renderer. In
parallel, evaluate a simpler maintained non-ML generator or real DEM pipeline;
that comparison can determine whether diffusion's 294 MiB field workflow and
Python/model stack buy enough morphology quality to justify their tooling
cost.

# Terrain Placement Controls

Date: 2026-07-19

Status: planned product-control and comparison batch.

## Context

The active terrain product renders one continuous, unchanged raster heightfield.
Its quiet foreground is not a circular clear, flattening filter, or transition
mask. The runtime searches the source for a coordinate with low local relief, a
coherent mountain arc, and an open arc, then bakes the same source around that
coordinate into the cached backdrop mesh.

That selected coordinate is useful for product composition, but it also makes
it difficult to tell how much of the result comes from the source and how much
comes from placement. The next checkpoint therefore adds unfiltered placement
controls before considering any synthetic source shaping.

## Placement Modes

The runtime will expose three startup-only modes:

- `selected`: retain the current bounded directional search and require its
  composition contract;
- `raw-center`: use the geometric center of the raster without scoring or
  refinement;
- `raw-sample`: use a deterministic indexed pseudo-random coordinate without
  scoring, refinement, rejection, or retry.

Every mode evaluates the same relief, slope, prominence, and arc metrics for
reporting. Only `selected` uses those metrics as acceptance criteria. Raw modes
must satisfy only finite source coverage and the baked camera-clearance
contract. A cluttered foreground, weak mountain composition, or failed
directional score is valid negative-control evidence.

The raw sample index is separate from the heightfield generation seed. It maps
to a stable normalized location over the valid source domain, so the same index
can compare compatible source assets without changing their generated content.
The valid domain is inset by the full render support radius and gradient sample
margin; the runtime must never hide a poor sample by selecting a replacement.

## Runtime Boundary

Placement remains immutable after startup. Loading the raster, building its mip
chain, choosing a focus, sampling the cached mesh, and uploading geometry are a
single setup operation. The first implementation therefore exposes command-line
and config selection plus read-only UI evidence. It does not present a live
selector that appears cheap while synchronously rebuilding millions of samples.

Raw modes default to source-space heading zero. The selected mode retains its
current showcase heading, while an explicit backdrop azimuth overrides either
behavior. Controlled captures always pass explicit matched headings.

Foreground height becomes a reproducible startup option matching the existing
2-1000 m review slider. The 100 m default remains a deliberate stress view; the
500 m baked reference remains the clearance-qualified comparison.

## Deferred Prepared Stage

This checkpoint adds no terrain clearing, fitted plane, radial valley, source
mask, transition radius, transition width, or shaping strength. A later
`prepared-stage` lane is justified only if the selected and raw controls show a
repeatable composition problem that coordinate selection cannot solve.

If that lane is needed, it must remain explicitly synthetic and must blend
toward a low-pass or fitted local terrain target over a broad band. It must not
replace the source with a constant-height disk or make an artificial circular
boundary part of the product contract.

## Acceptance

The default selected placement must retain its current source focus, metrics,
cached product, and capture. Raw center and representative raw indexes must be
deterministic, cover the complete render disk, report metrics without rejecting
failed composition, and preserve the same geometry topology, material,
atmosphere, camera, and per-frame cost as the selected product.

A focused review pack compares selected, raw center, and three raw samples at
four matched headings, followed by 100 m and 500 m foreground checks. The
result decides whether natural selection is sufficient or whether a separate
prepared-stage study should be planned.

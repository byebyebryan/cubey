# Terrain Placement Controls

Date: 2026-07-19

Status: implemented and accepted product-control checkpoint.

## Context

The active terrain product renders one continuous, unchanged raster heightfield.
Its quiet foreground is not a circular clear, flattening filter, or transition
mask. The runtime searches the source for a coordinate with low local relief, a
coherent mountain arc, and an open arc, then bakes the same source around that
coordinate into the cached backdrop mesh.

That selected coordinate is useful for product composition, but it previously
made it difficult to tell how much of the result came from the source and how
much came from placement. This checkpoint adds unfiltered placement controls
before considering any synthetic source shaping.

## Placement Modes

The runtime exposes three placement modes at startup and through the review UI:

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

The control checkpoint initially kept placement immutable after startup because
changing focus requires resampling and uploading the complete cached product.
The follow-up runtime selector preserves that cost boundary: mode and raw index
are staged explicitly, CPU resampling runs asynchronously, and only a completed
replacement is uploaded and swapped into the renderer. The current product
remains visible while the replacement builds and remains active after a failure.

Raw modes default to source-space heading zero. The selected mode retains its
current showcase heading, while an explicit backdrop azimuth overrides either
behavior. Controlled captures always pass explicit matched headings.

Foreground height is a reproducible startup option matching the existing
2-1000 m review slider. The later rendering-acceptance pass uses 200 m as the
product default; 100 m remains the deliberate close stress view and 500 m the
clearance-qualified comparison.

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
four matched headings, followed by 100 m and 500 m foreground checks.

## Implementation

The public configuration controls are:

- `terrain.placement` / `--terrain-placement`;
- `terrain.placement_index` / `--terrain-placement-index`;
- `terrain.foreground_height_m` / `--terrain-foreground-height`.

The raster source publishes its exact world-space bounds. Raw samples use a
fixed indexed pseudo-random sequence over source sample coordinates inset by
the complete `16.384 km` render radius and one gradient step. The sequence is
independent of the heightfield seed. Raw locations are evaluated once and are
never retried when relief, slope, or directional composition fails.

The focused-stage camera contract is separate from the directional placement
contract. Selected mode requires both; raw modes require source coverage and
baked camera clearance while retaining a failed directional result for UI and
profile reporting. The GUI edits mode and raw index, reports rebuild status or
failure, and shows the active coordinate, score, relief, slope, arcs, and
clearance. A successful swap resets the orbit for the new stage while preserving
the current foreground height.

The renderer keeps the current cached CPU/GPU product active while a replacement
is built. Only the latest requested generation may install, and a failed or
superseded build leaves the active terrain untouched. This is intentionally a
coarse review operation rather than incremental streaming.

## Review Evidence

The canonical review used runtime revision `cb882355`, the seed-0 2048 x 2048
field with elevation SHA-256
`27b49f12f29ae24629a8ec03d12b53c6986404c0354069529be75a5ea02c45df`, and
the 30 captures under `outputs/terrain/placement-control-v1`.

| Placement | Focus x/z (m) | Directional | Score | Local relief (m) | P95 slope | Baked clearance (m) |
|---|---:|---:|---:|---:|---:|---:|
| selected | 8500 / -2500 | pass | 5.571 | 29.75 | 0.081 | 490.76 |
| raw center | 0 / 0 | fail | -1.622 | 298.05 | 0.910 | 385.20 |
| raw sample 0 | 3105 / -13725 | fail | -0.997 | 189.85 | 0.555 | 473.40 |
| raw sample 1 | 135 / -3585 | fail | -3.083 | 285.72 | 0.864 | 421.63 |
| raw sample 2 | 9075 / 8025 | fail | -0.333 | 145.75 | 0.527 | 416.96 |

The four-heading sheet makes the placement benefit unambiguous. Selected mode
retains a quiet continuous foreground at every heading while preserving a
useful mountain arc in some directions and open terrain in others. Raw center
and raw sample 1 place large slopes directly into the foreground; samples 0 and
2 occasionally compose well but are not stable across yaw. The 100 m versus
500 m sheet shows that raising the focus improves clearance but does not turn a
poor raw location into a consistently useful stage.

The quiet selected foreground has no circular boundary in either sheet. Every
lane samples the unchanged source, so the comparison directly rejects the idea
that the accepted composition depends on hidden flattening or a radial clear.

Two 120-frame, 1600 x 900 video profiles retained 90 post-warmup frames:

| Placement | Terrain surface p50 | Atmosphere + terrain + post p50 | Product triangles | Source samples |
|---|---:|---:|---:|---:|
| selected | 0.432 ms | 0.855 ms | 607200 | 2657280 |
| raw sample 0 | 0.425 ms | 0.846 ms | 607200 | 2657280 |

The small timing difference is noise-level variation from the same renderer.
Placement changes startup source coordinates, not steady-state topology or
material work.

## Verdict

Keep `selected` as the V1 product default and retain raw center/sample as
explicit diagnostics. Natural coordinate selection is sufficient for the
current fixed-focus far-backdrop contract. Do not add a prepared stage,
clearing radius, transition band, or shaping strength now.

Synthetic preparation should be reconsidered only if a future consumer needs a
larger owned foreground footprint, multiple source assets repeatedly fail the
selection contract, or scene composition requires a guaranteed floor that
cannot be supplied by placement. Material frequency and close-view source
fidelity remain separate terrain-quality work rather than placement failures.

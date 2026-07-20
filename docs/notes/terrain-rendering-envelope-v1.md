# Terrain Rendering Envelope V1

Date: 2026-07-20

Status: decision gate in progress.

## Goal

Determine whether the remaining low-poly mountain read is caused by the fixed
render stride, by the external source and its filtering, or only by views
outside the accepted far-backdrop envelope. This is an evidence batch, not an
LOD, source-model, or material redesign.

The canonical control remains the selected seed-0 Terrain Diffusion field:

- elevation SHA-256:
  `27b49f12f29ae24629a8ec03d12b53c6986404c0354069529be75a5ea02c45df`;
- selected source focus: `8500 / -2500 m`;
- continuous seam-matched center and `16.384 km` outer radius;
- high-density cached source grid with render stride 3 by default;
- filtered-detail material, directional terrain shadows, and 40 degree field
  of view.

No capture in this batch may change the source field, placement, vertical
scale, material inputs, or shadow implementation.

## Camera Contract

The product-qualified reference is a foreground focus 500 m above the placed
terrain with a 50-250 m orbit and 0-30 degree elevation. The 100 m foreground
focus and 500-1000 m orbit are inspection stress cases. They expose limits but
do not silently expand the V1 support claim.

Startup camera overrides configure only the review camera. They must not alter
the baked placement stage, its 500 m reference, or its clearance result. Live
GUI pitch remains unrestricted for exploratory inspection.

## Review Matrix

All retained captures use `1600 x 900`, selected placement, a paused solar
clock at 09:00 on day 172 at 35 degrees latitude, terrain shadows, and the
filtered-detail presentation.

- macro control: headings 0, 90, 180, and 270 degrees at the qualified 500 m
  focus, with matched clear and fair-cloud frames;
- low-view control: the same headings at the 100 m stress focus;
- camera envelope: mountain-heavy and sparse headings at the 50/250 m
  qualified orbit endpoints and 500/1000 m stress endpoints;
- topology A/B: stride 1 and stride 3 at three matched views, each rendered as
  surface, clay, and projected-edge diagnostics;
- profiles: clear stride 3, clear stride 1, and cloud stride 3 after 30 warmup
  and 120 measured frames.

Profiles report mean, p50, and p95. The clear stride-3 composition keeps a
`1.0 ms` mean and p50 target for atmosphere, cached terrain, stage proxy, and
post combined. Cloud and stride-1 lanes are comparisons, not new product
budgets.

## Decision Rule

Use visible silhouette faceting in normal surface and clay views as the gate;
the projected-edge view explains the result but cannot fail the product by
itself.

- If stride 1 removes the defect at two qualified headings, the next batch is
  cached screen-space sector LOD or index variants.
- If stride 1 retains the same shape defect in qualified views, the next batch
  addresses source sampling or filtering rather than LOD.
- If stride 3 is acceptable throughout the qualified envelope and only stress
  views fail, keep the V1 boundary and proceed to Material V2.

The final section of this note will record the measured costs, visual result,
and one selected next direction. Until then, no geometry or material response
is promoted from this diagnostic.

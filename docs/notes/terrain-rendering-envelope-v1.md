# Terrain Rendering Envelope V1

Date: 2026-07-20

Status: closed; retain stride 3 and proceed to Material V2.

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

## Result

The retained pack under `outputs/terrain/rendering-envelope-v1` was captured
from revision `05199bfa` and contains 46 matched frames. It preserves the
canonical elevation hash, selected focus, stride-3 product hash
`0xcf2100b0763a8211`, and `2,657,280` cached source samples.

Stride 1 raises the complete render product from `607,200` to `5,305,344`
triangles, or `8.737x`. It does not materially change the visible silhouette in
either qualified heading or in the 1 km stress comparison. The normalized
surface-frame pixel RMSE is only `0.0022-0.0029` across the three pairs, and
the surface and clay contact sheets retain the same broad outline and source
character. The projected-edge diagnostic shows the denser topology, but that
difference does not translate into a meaningful product-view improvement.

The qualified stride-3 views do not show a topology failure severe enough to
justify LOD. The remaining weaknesses are the pale and nearly uniform material
response, atmospheric contrast loss, heading-dependent sparse mountain
coverage, and limited close-view character. The fair-cloud lane composes
correctly and improves scene context, but does not hide those terrain limits.

| Lane | Triangles | Terrain mean / p50 | Clear mean / p50 | Full mean / p50 |
|---|---:|---:|---:|---:|
| clear stride 3 | 607,200 | 0.543 / 0.486 ms | 0.996 / 0.921 ms | 0.996 / 0.921 ms |
| clear stride 1 | 5,305,344 | 1.296 / 0.720 ms | 1.847 / 1.147 ms | 1.847 / 1.147 ms |
| cloud stride 3 | 607,200 | 0.727 / 0.705 ms | 1.222 / 1.180 ms | 3.218 / 2.991 ms |

The stride-3 clear lane meets the `1.0 ms` mean and p50 gate, narrowly on mean.
P95 remains diagnostic: `1.481 ms` for clear stride 3, `4.845 ms` for clear
stride 1, and `4.249 ms` for the full cloud composition.

## Decision

Retain fixed stride 3 and the existing V1 support boundary. Do not implement
adaptive LOD or full-resolution cached meshes for the current backdrop. The
100 m foreground and 500-1000 m orbit remain stress views, not a close-terrain
claim.

The next terrain rendering batch is Material V2: improve broad and mesoscopic
surface separation, terrain-light contrast, and geological scale cues without
changing silhouette or adding high-frequency noise. Source sampling/filtering
returns only if that material pass leaves a shape defect visible in the
qualified views. LOD returns only when a real consumer requires a closer or
wider camera envelope.

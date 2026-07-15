# Terrain Cached Backdrop Pivot

Date: 2026-07-15

Status: implemented and accepted. This supersedes the quality-tile renderer as
the terrain v1 product target while preserving it as geometry-continuity
evidence. Measured evidence is recorded in
[`terrain-cached-backdrop-v1-review.md`](terrain-cached-backdrop-v1-review.md).

## Why The Direction Changed

The detached stage made the current renderer easy to inspect from a real scene
camera. That inspection exposed three separate problems:

- mountain silhouettes and peaks remain visibly faceted at the supported orbit;
- the source is being judged around 1.5 km, where its coherent-noise structure
  reads as mid-field terrain rather than the far backdrop it previously passed;
- procedural source, weathering, normal recovery, terrain-shadow marching, and
  layered triplanar material work are repeated during every draw.

The last accepted `9.3638 ms` quality-tile number and the later roughly `27 ms`
1920 x 1080 comparisons are whole headless frame intervals. They include shared
sky, post, submission, readback, and capture work. They are not a terrain GPU
pass measurement and cannot prove either success or failure against a terrain
budget.

The tile field did solve its intended crack and ownership problem. It should be
kept as an experiment, not extended into the backdrop product.

## V1 Product Decision

Terrain v1 is a fixed-focus, three-dimensional backdrop:

- yaw remains unrestricted around a fixed local scene focus;
- orbit radius and elevation stay bounded by the detached stage contract;
- the supported lower-frame terrain distance returns to 3.2 km;
- translation, traversal, streaming, and planet-scale LOD are deferred;
- source v2.1 is frozen for this pivot so renderer changes remain attributable;
- height, normals, material masks, and ambient visibility are generated once;
- the runtime draws static, cullable geometry and does not evaluate terrain
  noise or local weathering per frame.

The fixed focus permits a topology that a general terrain renderer could not
use: a polar field with logarithmic radial spacing and fixed azimuth sectors.
This keeps geometric density approximately screen-relative for every yaw,
allows conservative sector culling, and removes runtime LOD transitions.
Topology is a renderer contract only; it does not author or mask source shape.

## Performance Contract

The hard budget is the `terrain surface` GPU pass at 2560 x 1440 on the review
machine's RTX 5070 Ti. After 30 warmup frames, its 95th percentile must remain
below `1.0 ms` over at least 120 measured frames.

Atmosphere, stage proxy, post, wall-frame time, readback, and encoding are
reported separately. Setup-time generation and resident memory are also
reported so the runtime gain is not purchased with unbounded artifacts, but
they are not folded into the per-frame surface budget.

## Implementation Result

The accepted high product samples 2,558,976 source points into 48 exact-seam
polar sectors. It retains the high field for cached normals and diagnostics but
uses a 540,672-triangle far-field index capacity before conservative azimuth
and frustum culling. The runtime terrain shader reads cached geometry and
classification only.

The maintained 2560 x 1440 RTX 5070 Ti orbit recorded 146 post-warmup samples
at `0.876288 ms` terrain-surface p95. Setup plus the first 640 x 360 frame took
`18,396 ms` and reached `342,728 KiB` process RSS; that includes stage search,
product generation, Vulkan startup, and upload. Runtime cost is accepted.
Persistent caching and setup-time reduction remain deferred.

## Preserved And Deferred Work

The `control` clipmap and `quality` tessellation paths remain explicit review
controls. Their source parity, diagnostics, and historical captures are still
useful. They are no longer terrain v1 acceptance paths.

Hydrology, source redesign, close ground detail, foliage, water bodies, dynamic
terrain cast shadows, camera translation, and general clipmap/streaming design
remain separate work. The next source pass should begin only after the cached
renderer establishes an honest distance and cost envelope.

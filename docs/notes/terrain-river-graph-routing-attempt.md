# Terrain River Graph Routing Attempt

Date: 2026-06-23

This note captures the reverted revision 4 river-routing attempt so the lesson
is kept without leaving the worse visual product in place.

## What Was Tried

The reverted implementation tried to move the active river product from the
revision 3 hybrid trunk/tributary extractor to a more explicit hydrology graph:

- run a Priority-Flood-style epsilon fill over the padded hidden routing
  surface;
- emit `filled_drainage_potential` and `depression_depth` diagnostics;
- route D-Infinity-style fractional accumulation over the filled surface;
- threshold high-accumulation cells into a channel mask;
- collapse source/confluence/outlet cells into graph nodes and edges;
- select outlet components and prune short or parallel graph edges;
- render selected graph edges directly into `channel_graph`, `river_trunk`,
  `tributaries`, and `river_mask`.

The implementation landed as `197dc5bc` and was documented in `e54049d9`, then
reverted by `8afb5d4`.

## Why It Was Rejected

The generated pictures were noticeably worse than revision 3:

- active river masks broke into disconnected snippets;
- many segments exposed hard horizontal, vertical, or 45-degree receiver runs;
- stress captures produced schematic clusters and parallel edge fans;
- graph-edge selection optimized local edge scores instead of preserving a
  coherent visible river system;
- downstream-chain selection helped continuity in one place but made the
  selected product sparser and less useful elsewhere;
- smoothing, lateral offset, and relaxation could dress a path but could not fix
  bad topology after selection.

The main failure was treating graph edges as renderable product centerlines too
early. A raster receiver graph is useful analysis data, but selected graph edges
are not automatically organic river paths.

## What To Keep

The attempt still points at useful future work:

- depression fill or breach routing is worth revisiting, but only with visual
  gates and diagnostic captures;
- fill diagnostics can be useful, but should be introduced separately from a
  product-rendering change;
- a channel graph may be useful as a debug or planning structure, not as the
  direct river mask source;
- any future network extractor must operate at network level: connected
  downstream chains, branch hierarchy/order, basin-aware pruning, and explicit
  trunk continuity before rasterization;
- rendered river paths need sub-cell curves, variable width/taper by discharge
  or stream order, and path smoothing before they become product masks.

## Current Decision

Revision 5 kept the revision 3 connected padded-domain,
fractional-accumulation, candidate-scored trunk/tributary implementation as the
baseline, then added only active-network-connected stream-order-seeded paths.
Revision 6 supersedes that hybrid by selecting connected stream-order support
corridors before tracing active product paths.

Do not reintroduce direct graph-edge rendering into `river_mask` or
`river_trunk` without first showing that it improves the default and stress
captures against the connected corridor baseline.

# Terrain Graph-First River Plan

Date: 2026-06-28

Status: implemented for `temperate-mountain-river-stress` in generator revision
17.

Revision 16 made the stress river cleaner, but it still exposes the same
source-model problem: river coverage and organic shape are being solved after
the topology has already been constrained by raster routing. Relaxing the
filters brings back long D8-like straight or diagonal paths. Tightening the
filters starves basin reach.

## Decision

Move the stress recipe to a graph-first river driver. Keep priority-flood
routing repair, D-Infinity flow, raster flow accumulation, and stream order as
diagnostics and support fields, but do not use D8 paths as the visible stress
network source.

The first implementation should build a deterministic drainage plan before
rasterization:

- place non-grid river nodes over the padded hidden routing domain;
- build local adjacency between nearby nodes without adding a triangulation
  dependency yet;
- choose one downstream edge per active node so rivers merge downstream but do
  not split;
- score candidate edges with routing potential, continuous flow alignment,
  curvature inertia, confluence spacing, and parallel-crowding penalties;
- compute graph discharge and stream order on the accepted tree;
- rasterize graph edges into the existing trunk, tributary, channel-width,
  valley-width, wetness, and deposition fields.

## Reference Lessons

- `terrain-erosion-3-ways` avoids one-node-per-grid-coordinate river graphs
  because they create horizontal and vertical artifacts. It uses Poisson-disc
  points plus Delaunay topology, then grows rivers upstream from outlets.
- Proland keeps rivers as graph and curve data first: axis curves, banks,
  potentials, widths, and derived flow fields.
- Cordonnier et al. build a stream graph over the terrain domain and convert
  that graph into terrain features.
- Priority-Flood remains useful for depression repair and drainage validation,
  but it should not be the visible river designer.
- Hydraulic erosion and SimpleHydrology-style particle refinement are deferred
  until the network topology is credible.

## Implementation Boundary

- Apply the graph-first driver only to `temperate-mountain-river-stress` in this
  batch. The default recipe keeps the current path until the stress driver is
  visually credible.
- Do not add a Delaunay, erosion, lake, breach-routing, or sediment dependency.
- Do not remove existing routing diagnostics. The new graph fields should make
  it easy to compare planned river topology against raster routing output.
- Do not render graph edges directly without the existing channel rasterization
  and smoothing pipeline.

## Acceptance

- Stress captures should read as one connected drainage network with an obvious
  mainstem, attached tributaries, broad reach, and discharge-driven width
  variation.
- `river-graph-plan.png` should expose the source topology separately from
  `river-mask.png`.
- `river-graph-discharge.png` should show larger values toward the downstream
  trunk and confluences.
- Existing `flow-accumulation.png` and `stream-order.png` remain diagnostic
  products, not the visible stress source of truth.

## Outcome

- Added stress-only graph planning over the padded hidden routing domain using
  deterministic jittered nodes and local k-nearest adjacency.
- Added graph discharge/order metrics and exported them as
  `river-graph-plan.png` and `river-graph-discharge.png`.
- Routed accepted graph paths through the existing channel rasterization path
  instead of painting graph edges directly.
- Kept the default recipe on the prior repaired-routing path so the graph-first
  work can mature in the stress recipe first.
- Fixed the first graph painter issue where shared downstream tributary
  segments were painted once per upstream leaf, creating artificial parallel
  offset copies.

Remaining caveats: this is still a jittered-grid/k-nearest graph, not a
Poisson-disc plus Delaunay graph, hydraulic erosion model, lake/breach router,
or evolved river system. The generated network is materially less D8-shaped than
the raster-routing stress source, but branch joins can still be angular and the
topology builder needs another pass before it becomes the default river driver.

## Next Hierarchy And Scale Direction

The next revision should treat `river_trunk` as the major-channel product field,
not only the single downstream mainstem. Keep the public field contract stable
for now:

- `river_trunk`: mainstem plus major/high-discharge tributaries.
- `tributaries`: minor attached branches.
- `river_graph_plan` and `river_graph_discharge`: source diagnostics, not
  separate product hierarchy fields.

This keeps the product simple while matching how river networks read visually:
large tributaries should affect channel width, valley width, material response,
and final composition similarly to the mainstem.

The revision 17 graph pass is still patch-local. Nodes are generated inside the
padded hidden domain for the requested product, and topology only knows that
finite local graph. That is acceptable for current stress testing but not a
world-scale terrain model.

The scalable direction is to separate river topology from local rasterization:
build deterministic basin/segment graph data in stable world coordinates, query
only the tile plus halo at product time, and rasterize the relevant graph
segments into local fields. Coarse basin graphs should feed sub-basin tributary
graphs, then local channel detail. Segment IDs, chunk caches, and tile-border
continuity are future work; this batch should avoid making the patch-local
implementation look like a permanent global model.

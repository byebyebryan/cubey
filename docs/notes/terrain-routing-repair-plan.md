# Terrain Routing Repair Plan

Date: 2026-06-27

Revision 9 made the stress river network connected, but the source still exposes
straight reaches, parallel branches, and local-sink artifacts. The next batch
should repair the routing surface before adding more biome or river-rendering
features.

## Decision

Use a small deterministic priority-flood epsilon fill over the hidden routing
domain. Route D8, D-Infinity, accumulation, stream order, sink masks, and active
river extraction over the repaired routing surface. Keep the raw-to-repaired
delta as a visible debug field so the fill can be reviewed instead of hidden.

This is a routing repair pass, not erosion. SimpleHydrology is useful as a
reminder that real hydrology is particle/process driven, but it is broader than
this batch. TerraForge3D is useful as a heightmap workflow reference, but does
not provide a ready-made fill/breach model to port here.

## Implementation Boundary

- Keep the terrain recipe IDs and public config stable.
- Bump the generator revision when the repaired routing product lands.
- Add `routing_fill_delta` as a product/debug field.
- Do not add breach routing, hydraulic erosion, lakes, wetlands, or sediment
  transport in this batch.
- Do not reintroduce direct graph-edge rendering, near-active snap joins, raw
  support painting, or multiple unrelated stress corridors.

## Acceptance

- `routing-fill-delta.png` is inspectable and non-negative.
- Fill remains bounded; it should repair local pits, not flatten the terrain.
- `sink-mask.png` stays limited to visible crop outlets and true remaining
  terminals.
- `flow-accumulation.png` and `stream-order.png` remain organized and do not
  collapse to a flooded patch.
- Default and stress river masks remain connected enough for the existing
  review tests, with no regression to scattered clusters.

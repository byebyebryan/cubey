# Terrain Routing Repair Plan

Date: 2026-06-27

Revision 9 made the stress river network connected, but the source still exposes
straight reaches, parallel branches, and local-sink artifacts. The next batch
should repair the routing surface before adding more biome or river-rendering
features.

Status: implemented in generator revision 10.

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

## Outcome

Revision 10 added the priority-flood epsilon fill over the padded hidden routing
domain. `drainage_potential` now reflects the repaired routing surface, and
`routing_fill_delta` is emitted as a product/debug field. The default recipe no
longer paints raw stream-order support cells; traced paths carry procedural
offset, join discs, width scale, and strength variation so the visible product
stays connected without long uniform high-strength cores.

This did not solve full river-network quality. The stress recipe still shows
parallel support-path artifacts and remains slow enough that stress performance
should be addressed before adding heavier hydrology checks.

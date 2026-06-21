# Terrain Lab River Research Notes

Terrain Lab is pivoting from canyon-first work to a shared river/drainage
network model. Canyons remain important, but they should be treated as an arid,
high-incision expression of a drainage network rather than the owner of the
network logic.

## Useful References

- Génevaux, Galin, Guérin, Peytavie, and Beneš, "Terrain Generation Using
  Procedural Models Based on Hydrology" (2013): the strongest fit for Terrain
  Lab's direction. It treats rivers as primary modeling features, first builds a
  hierarchical drainage graph, then derives watersheds and terrain patches from
  that graph.
- Tarboton's D-infinity / TauDEM work: useful grounding for continuous flow
  direction and contributing area. This is the right family of ideas for
  reducing the D8-looking straight/diagonal artifacts we have been seeing.
- GRASS `r.stream.order`: practical stream hierarchy vocabulary. Strahler-style
  stream order is a good first field for differentiating tributaries from trunks
  without running a heavy erosion simulation.
- Fischer et al., "Interactive Example-Based Terrain Authoring with Conditional
  Generative Adversarial Networks" is less directly aligned, but their drainage
  basin framing is relevant as a reminder that water bodies can be generated as
  structure before height detail.
- `~/code/ref/SimpleHydrology`: useful local reference for particle erosion,
  discharge, and momentum maps. It should remain a later sandbox reference, not
  the immediate production model.

## Decision

The next shared terrain abstraction is `river network`, not `canyon network`.
The generator should expose river/drainage fields that other slices can consume:

- discharge proxy from runoff-weighted contributing area;
- stream order;
- river/channel width;
- valley or floodplain width;
- water presence;
- wetness and deposition derived from those fields.

The first implementation should stay deterministic and field-based. It should
not add a particle droplet solver, meander simulation, lakes, or animated river
water. Those are useful later once the topology and slice consumption are
credible.

## Slice Interpretation

- Temperate mountain rivers: visible river reference slice with wet channels,
  vegetation response, deposition, and simple static water presence.
- Arid mesa canyon: dry river network expression. Water presence should be zero,
  while discharge/order still drive wash width, canyon floor, wall width, and
  incision.
- Alpine glacial valley: drainage remains diagnostic/supporting unless meltwater
  becomes a focused slice.
- Desert dunes: hydrology remains diagnostic, not causal.

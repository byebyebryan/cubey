# Terrain Far-Field V1

Date: 2026-07-14

Status: implementation contract.

## Product Boundary

Terrain source v2.1 is sufficient for a directional far-field terrain product
when distance and composition are explicit runtime constraints. The supported
recipe is mountain source v2.1 with the quality renderer, layered material, and
backdrop presentation. It is a scene background, not general traversable
terrain.

The reference presentation is 1920 x 1080 with a 40-degree vertical field of
view. A selected mountain target remains at least 3200 m from every supported
camera position. The camera may move within a 200 m radius around its planned
anchor and look within 30 degrees of the selected heading. Terrain at the
1600 m midground tier and unrestricted traversal remain diagnostics rather than
supported product behavior.

The lower frustum retains at least 10 m of final-terrain clearance through the
first 300 m. Center and upper-frame rays remain free of terrain through 2400 m.
An intermediate lower-frame test rejects a nearby wall that would consume the
frame before 1200 m. Scene integrations should still let their foreground
geometry own the lowest part of the image.

## Placement And Movement

The backdrop planner selects a naturally suitable source region; it does not
move, mask, or flatten terrain. Target samples are taken at 3400 and 6600 m
from the home anchor so the 200 m movement zone preserves the 3200 m minimum.
The planner validates the center and eight perimeter positions at the home yaw
and both yaw-cone limits.

The controller clamps movement to the planned world-space zone and yaw to the
directional cone. Planned terrain-relative clearance remains authoritative.
If no valid 200 m zone exists, the planner reports the contract failure and
falls back to a fixed viewpoint; it must not silently claim far-field support.

## Frozen Boundary

This checkpoint does not change source v1, v2, or v2.1 equations, source
parameters, weathering, CPU/GPU queries, materials, vegetation coverage,
atmosphere, lighting, or exposure. It does not add a camera-relative mask or a
world-space low-relief envelope. A relief envelope remains a later fallback
only if broader seed testing shows that natural placement is insufficient.

The visual correction in this batch is limited to camera composition and
terrain coverage cracks. The quality tessellation path must evaluate shared
edges with identical world position, source footprint, and height so sky cannot
leak through the terrain surface.

## Acceptance

For mountain source v2.1 seeds `0`, `9012`, and `12345`:

- the planner publishes a valid 200 m zone and a target at least 3400 m from
  its anchor;
- every supported camera position stays at least 3200 m from the target;
- lower, center, and upper frustum contracts hold across the yaw cone;
- interactive movement and yaw cannot escape the published bounds;
- quality clay and shadow diagnostics contain no interior sky cracks;
- the 3200 m far-field views remain convincing while 1600 m stays an explicit
  negative control;
- existing source hashes and CPU/GPU parity remain unchanged.

Material, atmosphere, and lighting tuning are outside this checkpoint even if
the review pack identifies later presentation opportunities.

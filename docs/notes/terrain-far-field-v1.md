# Terrain Far-Field V1

Date: 2026-07-14

Status: implemented and visually reviewed.

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
parameters, weathering, CPU/GPU values, materials, vegetation coverage,
atmosphere, lighting parameters, or exposure. It does not add a camera-relative
mask or a world-space low-relief envelope. A relief envelope remains a later
fallback only if broader seed testing shows that natural placement is
insufficient.

The quality tessellation source footprint is now derived from camera distance,
pixel angular span, and the configured screen-space edge target. It no longer
depends on a patch's maximum tessellation factor, so shared world positions
receive the same filtered source input across patch and LOD ownership changes.

The apparent blue coverage cracks in early clay captures were not missing
terrain rasterization. Shadow, LOD, normal, ambient, and aerial diagnostics all
retained continuous coverage. The actual cause was the direct-lighting helper
dropping both diffuse and specular response when an interpolated shading normal
briefly crossed `N dot V = 0` on a visible steep slope. Diffuse response now
remains governed by `N dot L`; only the view-dependent specular response needs
a positive `N dot V`. This is a lighting correctness fix, not a parameter or
presentation retune.

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

## Validation Checkpoint

Generate the accepted pack with:

```sh
projects/terrain/capture_far_field_v1_review.sh
```

The pack is written to `outputs/terrain/far-field-v1/`. It contains native
1920 x 1080 surface and clay captures for all three seeds, 1600 m midground
negative controls, clay/shadow/LOD continuity diagnostics, the backdrop planner
report, and machine-checked review metadata.

All three required seeds selected the 3400 m tier and passed the complete zone
contract. Their worst supported-position target distances were `3267.16 m`,
`3255.38 m`, and `3207.23 m`. Worst lower-foreground margins were `10.00 m`,
`17.87 m`, and `23.70 m`; center/upper and lower-wall tests reported zero
occluded rays for every seed. Seed `0` required `272.00 m` of terrain-relative
clearance, while seeds `9012` and `12345` retained the `150 m` floor.

The far-field contact sheet remains convincing at the supported distance. The
midground sheet exposes the expected large foreground forms and insufficient
surface bandwidth, so 1600 m remains a useful negative control rather than a
quietly promoted product tier.

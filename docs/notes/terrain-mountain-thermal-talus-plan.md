# Terrain Mountain Thermal Talus Plan

Date: 2026-07-01

Revision 28 made the mountain stress profile more cohesive by moving visible
height into broad mass and ridge bodies. The remaining read is still synthetic:
peaks and shoulders are coherent, but they do not look shaped by a physical
process. The next pass should therefore move below source/profile tuning and
add a bounded process diagnostic.

## Revision 29 Target

Add a thermal erosion / talus relaxation diagnostic for
`temperate-mountain-range-stress` only.

- keep final `height_m` unchanged;
- publish erosion, deposition, and instability review fields;
- composite the diagnostic process result into `post_erosion_height_m`;
- keep default and river stress recipes inactive for these fields;
- use the existing process-layer pattern beside the gully diagnostic.

The goal is to see whether local slope relaxation makes over-steep shoulders
and synthetic peaks read more naturally in `mountain-post-erosion-perspective.png`.
This is not hydraulic erosion, sediment transport, glacial carving, snow, or
biome material work.

## Process Shape

The first implementation should be deliberately bounded:

- run a small fixed number of local 8-neighbor relaxation iterations;
- transfer material only from over-steep supported mountain samples into lower
  neighbors;
- gate activity by mountain support so the process does not affect the default
  terrain recipe;
- clamp total erosion by local relief and a hard maximum;
- expose residual instability so the next review can tell whether remaining
  sharp areas are process inputs rather than material or renderer artifacts.

The diagnostic surface is:

```text
post_erosion_height_m =
  height_m - erosion_delta_m - thermal_erosion_delta_m + talus_deposition_m
```

`height_m` remains the product surface for all existing material, river, and
summary behavior in this batch.

## Review Order

For revision 29, inspect these files together:

1. `outputs/terrain/mountain-range-stress/mountain-perspective.png`
2. `outputs/terrain/mountain-range-stress/mountain-post-erosion-perspective.png`
3. `outputs/terrain/mountain-range-stress/thermal-erosion-delta.png`
4. `outputs/terrain/mountain-range-stress/talus-deposition.png`
5. `outputs/terrain/mountain-range-stress/slope-instability.png`
6. `outputs/terrain/mountain-range-stress-1025/thermal-erosion-delta.png`

Expected success is modest: fewer obviously over-steep synthetic shoulders in
the diagnostic surface. If the pass just smears peaks into mud or produces
full-map noise, keep it diagnostic and tune or replace the process model before
promoting anything into final height.

## Revision 29 Result

Implemented as a mountain-stress-only diagnostic process. `height_m` remains the
product surface, while `post_erosion_height_m` now combines the existing gully
diagnostic with thermal erosion and talus deposition for review.

The regenerated `513` mountain stress manifest reports revision 29, 55 fields,
49 scalar outputs, `height_m.span = 1548.804`,
`post_erosion_height_m.span = 1540.493`,
`thermal_erosion_delta_m.max = 56.159`,
`talus_deposition_m.max = 63.752`, and
`slope_instability.mean = 0.0566`. The `1025` stress capture reports the same
revision and field/output counts, with `height_m.span = 1572.752`,
`post_erosion_height_m.span = 1567.492`,
`thermal_erosion_delta_m.max = 53.625`,
`talus_deposition_m.max = 58.847`, and
`slope_instability.mean = 0.0283`.

Visual read: `mountain-post-erosion-perspective.png` is smoother and less
synthetically sharp than `mountain-perspective.png`, especially along the most
over-steep faces and shoulders. The diagnostic masks are sparse enough to be
useful, but they still expose localized straight or stepped bands from the
source terrain. Keep this as a review surface until the mountain source and
process model are more mature.

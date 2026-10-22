# SDF authored content evidence

Date: 2026-10-21

Implementation follows the [authored content plan](sdf-authored-content-plan.md).
This record distinguishes native checks and assistant visual selection from
the user's assessment of the resulting content.

## Robot blockout

`projects/sdf/tools/robot_blockout.py` authors three original silhouettes using
the existing rounded-box/sphere recipe format. Twelve native 960x960 views
cover front, side, rear and three-quarter compositions. Each requires Vulkan
validation; the three hero views also pass the CPU/GPU ray oracle. Receipts
and explicit recipes are under `outputs/sdf/robot-blockout/`.

The assistant selected the medium survey proportions for further work. The
tall version has a less compact body/head relationship; the broad version has
separated leg attachments that would need adjustment. The selected blockout
is recognizable as a toy robot but still needs material separation, an authored
pose and more deliberate detail. This selection is not user visual acceptance.

The new `--clay` option supplies a neutral base color for shape inspection.
Existing scene defaults and recipe geometry remain unchanged. The blockout
renders use hard self-shadows, the shared atmosphere and no ground.

## Grouped geometry and material foundation

The version-2 `cubey.sdf.scene` format retains named CSG structure, transforms
and a material table. Its bounded postfix program supports boxes, spheres,
capsules, capped cylinders, union, subtraction and polynomial smooth union.
Rigid transforms and positive uniform scale preserve the selected distance
bound. The elementary primitive formulas were implemented directly from their
Euclidean definitions; no external primitive/operator source was copied.

The authored builder classifies complete composite expressions and samples the
same R16F brick representation. The existing batch builder remains available
for legacy recipes. Both use the shared tracer and HDR/atmosphere presentation.
Materials are resolved at surface hits without additional appearance atlases.
Opaque base color, roughness and emission are supported. Native part/base-color/
roughness diagnostics and desktop material editing expose that result.

Fixtures in `outputs/sdf/authored-fixtures/` pass required Vulkan validation,
the sampled-field CPU ray oracle and direct CPU/GPU expression/material checks.
They cover sphere, capsule, cylinder, isolated group cuts and smooth blending.
The material-only cache check retains one field build. The legacy cut fixture
still passes 263 rays without mismatch, exhaustion or sampling ambiguity.
The recipe/timeline and authored scene CPU test targets pass.

Coincident material selection keeps the earlier branch within a 1e-6 distance
tie band; geometry still uses the actual min/max. This stabilizes material
ownership without altering the distance bound. Nested smooth material blends
evaluate parameters through the full expression instead of retaining only
two material IDs. No image textures or UV coordinates were introduced.

## First colored survey robot

`tools/author_robot.py` uses the external `sdf_author.py` helper to emit the
complete named scene at `projects/sdf/scenes/survey-robot.json`. The new
`presets/survey-robot.json` selects it directly; no Python runtime is needed to
load or render the saved scene. Regenerating the file is a build-time content
operation.

The first colored version has 43 primitive leaves and 85 instructions. It
retains the survey blockout proportions, tilts the head, raises one arm and
adds local eye/vent cuts, a backpack, joint separation, dark soles and four
opaque materials. It uses capped cylinders for the optic and shins, capsules
for limbs and the antenna, and a smooth union for the torso/hip transition.
The material boundaries remain sharp on the robot.

Its first native 960x960 hero passes 425 rays with zero mismatches or exhausted
rays and four sampling ambiguities under the existing finite-grid oracle.
Maximum surface residual is 0.000117736; the largest nominal first-hit
difference is 0.0150168. Direct CPU/GPU evaluation passes 1,030 points with
maximum distance error 5.96046e-8, zero material error and no part mismatch.
The material-only cache check retains one field build. The field uses 124,114
of 262,144 bricks in the axis-64 profile, allocating 912,874,672 bytes.

These numbers identify the initial capture before the final multi-view,
interaction and performance qualification. The image is
`outputs/sdf/robot-first-colored.png`; its explicit exported scene is beside it.

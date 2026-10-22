# SDF robot joint orientation and alignment review

Date: 2026-10-22

The user found the field-kit robot broadly acceptable but called out elbow/joint
orientation and apparent upper/lower-leg misalignment. The earlier native SDF
qualification established field/rendering correctness but did not establish
mechanically coherent placement of the authored pieces. This pass corrects
the modeling and adds explicit geometric checks for that intent.

[Compare the joints and inspect the front/profile leg views](http://starship:8787/reports/e05293386c1ae7bfeac7985541938ef1bc974b737f381d64ff09044422c99702/).

## Findings and correction

The elbow caps and ivory retaining rings used a fixed +Z axis. Neither arm
bends in a fixed XY plane, so that axis was not perpendicular to its links.
The bent forearm intersected its ring, making the right ring read as a C shape.
The correction derives the hinge normal from the cross product of the upper-
and lower-arm directions. Cap and rim share that frame and the elbow pivot.
Native close-ups from both three-quarter sides show complete retaining rings.

Shoulder covers were also fixed forward. They now use the plane spanned by
the upper-arm direction and its downward rest direction. Both shoulder pivots
move from X = ±0.151 to ±0.161, and bearing radius grows from 0.035 to 0.038,
keeping the bearing seated into the torso and the oriented cover clear of it.
Caps and their scoped slot cutters share the resulting frame. Upper-arm
endpoints are regenerated from the revised shoulder and existing elbow pivots;
forearms use the elbow/wrist pivots directly.

The leg assembly had separate coordinates for the hip link, vertical shin,
ankle and collar. The upper link ended inside the shin rather than at its
upper endpoint. The shin axis was offset in depth, ankle center had another
depth offset, and feet/soles/toe grooves were shifted outward by 0.010 from
the leg center. This was noticeable even though the shapes overlapped.

Each corrected leg uses one line from hip `(±0.075, -0.170, 0.006)` to ankle
`(±0.075, -0.311, 0.018)`. The knee, shin endpoints and ankle seal lie on that
line; the cone and seal use its axis. Foot, sole and toe-groove X coordinates
match the leg. The toe still extends forward from the ankle. The feet remain
flat while the ankle bearing connects to the slightly inclined leg.

The tilted head previously rotated about its shell center, while both neck
collars remained vertical. It now pivots about the neck bearing at
`(0, 0.108, 0)`, and its upper collar follows the head's tilt. Wrist seals already
followed forearm axes, gripper cutters followed hand axes, and power-pod collars
followed pod axes; those relationships remain and are checked explicitly.

The field kit still has 82 leaves, 163 instructions and 82 saved construction
steps. The 43-shape original and 61-shape detail scenes and their generators
remain intact. The preceding 82-shape field-kit pose is retained in its published
report and Git snapshot `575c6fba`; the current field-kit presets select the
revised pose. Renderer code, shaders, palette, atlas settings and lighting
were not changed.

## Independent authoring audit

`tools/check_robot_joints.py` reads the saved scene rather than the author's
internal pivot calculations. It composes inherited transforms and uses an
independent quaternion-to-matrix expansion. Its 66 conditions cover:

- Upper-arm and forearm endpoints meeting their shoulder, elbow and wrist.
- Shoulder and elbow hinge-axis perpendicularity, centered caps and bearing seating.
- Shared cap/rim axes and seating; shoulder-slot frames and pivot alignment.
- Wrist and gripper frames; hip/knee/shin/ankle centerlines and shared shin/seal axes.
- Feet, soles and toe grooves centered laterally; power-pod collar axes and placement.
- Head rotation about the neck bearing and the upper collar following the tilt.

The preceding scene fails 24 of those conditions (42/66 pass); the revised
saved scene passes 66/66. Expected failure of the old scene is recorded at
`outputs/sdf/robot-joints-before-audit.json`; the revised receipt is
`outputs/sdf/robot-joints-audit.json`. These checks encode this static model's
intended connections, not a general articulated-animation solver. Native visual
inspection remains separate from satisfying those geometric conditions.

```sh
python3 projects/sdf/tools/author_robot_fieldkit.py
python3 projects/sdf/tools/check_robot_joints.py \
  --output outputs/sdf/robot-joints-audit-new.json
```

## Native evidence

The corrected content and audit are committed at
`5aa102381de97537db1c482741f44a33203ebf7a`. The matrix at
`outputs/sdf/robot-joints-qualified/` starts clean at that source, as does its
history run. Binary and all thirteen shader hashes match the preceding
field-kit matrix, and the previous hero is reproduced byte identical. Later
edits only assemble/document the report and make the joint audit automatic
when requesting joint-inspection views.

Geometry is `d4bbf5c36bf3fedc`, appearance `dc843f1505d4eee5`; the revised
field has 138,444 active bricks of 262,144 and the same 912,882,864-byte field
allocation. The hero build measured 91.906 ms wall time, including 1.013 ms
filter and 89.977 ms brick GPU time. Geometry updates remain synchronous.

All 83 history states pass Vulkan validation, sampled-field CPU ray checks and
direct CPU/GPU distance/material/part comparisons. Including endpoint and
midpoint/empty/final revisits, the history run has 88 oracle comparisons,
37,024 rays and 89,982 direct points. The final state has 581 rays, 385 hits,
zero mismatches or exhaustion, max nominal hit difference 0.0180185, max
surface residual 0.000120965 and five separately reported finite-grid sampling
ambiguities under the existing tolerance. Its 1,303 direct points have max
distance error 5.96046e-8, zero material error and no part mismatch.

Static/history images and export/reload/export scene bytes are identical.
Atlas-axis 80 and 92 checks pass and reproduce the axis-64 hero image byte for
byte. All nineteen cutter on/off pairs preserve geometry/appearance identity;
only the three declared rear vent previews remain occluded from the front
construction camera. The updated 62-second construction/orbit video has 1,860
frames, visits all 82 edits and builds 83 fields including empty. Every decoded
frame is screened for magenta exhaustion, with zero detected.

Eleven matched before/after pairs include the initial seven views plus opposite
elbow close-up, front/profile leg close-ups and full front. Each pair uses the
same camera, lights and material values. Rear/backpack inspections reverse the
sun equally in both versions. The final private Xvfb interaction receipt at
`outputs/sdf/robot-joints-controls/` passes resize, orbit/zoom, pause/reset,
Next/Previous, final/midpoint scrubbing, stable paused state, restart empty,
resume/pause and Escape with the same binary/shaders. It records 18 field builds.

Matched 1920x1080 1x hard-shadow static profiles, 180 frames with 30 warmup,
148 completed samples each, RTX 5070 Ti / 610.57.04:

| Pose | Median GPU frame | p95 |
| --- | ---: | ---: |
| Previous field kit | 3.164 ms | 3.406 ms |
| Revised joints | 3.079 ms | 3.318 ms |

These sum tracing and shared post, excluding construction/readback/encoding.
This small timing difference is a bounded pose comparison, not a renderer
optimization claim. The native renderer was unchanged, so the earlier primitive,
material, capacity and legacy regression matrix remains applicable by matching
binary/shader hashes; it was not rerun for this content-only correction.

`qualify_robot_detail.py --joint-views` adds the diagnostic views and now runs
the independent current-scene audit before native qualification. The original
capture's separately recorded audit has the same scene hash. To reproduce the
before/after matrix, save the prior source and create a static preset pointing
to it, then pass it as `--baseline-config` alongside the current field-kit static
and build presets. Use `--video` to capture the construction sequence.

`robot_detail_report.py --joints` packages the eleven pairs, the new build video,
all 83 state images, both independent audit receipts and hash-checked before/
after explicit scenes. It begins at the shoulder/elbow close-up and includes
video chapters for the shoulder, elbow/forearm, leg/foot and final orbit.

Publication verifies served HTML bytes. Local and published-URL browser checks
cover all eleven native image pairs, three comparison modes, wipe endpoints
and full-image restoration, five decoded video chapter seeks, playback and
captions, all 83 state images/labels, previous/next boundaries, keyboard slider
input, selected-edit seeking, local links, image decoding and 390px layout/video
ratio. No page JavaScript errors occur. Receipts are
`outputs/sdf/robot-joints-report-browser-review.json` and
`outputs/sdf/robot-joints-report-delivery.json`. Documentation navigation and
Python compilation checks pass. The geometry, source scenes, binary/shaders,
native captures and labeled video hashes match the final qualification.

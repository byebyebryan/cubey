# SDF robot field kit and the next modeling vocabulary

Date: 2026-10-22

After accepting the cleaned lens, the user asked to push modeling further and
consider additional primitive and operation types. This variant develops the
same robot as a serviceable field instrument. The original 43-shape and cleaned
61-shape detail scenes remain separate and byte reproducible. Visual acceptance
of this pass remains for the user.

[Compare the field kit and watch its construction](http://starship:8787/reports/52b40ce5c726673b029f0f22ec2dd21d50d9a297e938beb2c161fe8deec734de/).

The user subsequently found joint-orientation and leg-alignment problems in
this pass. The [joint review](sdf-robot-joint-review.md) records their correction.
The initial captures and measurements below retain their original pose.

## Content decisions

`author_robot_fieldkit.py` builds on the cleaned detail scene, preserving its
pose, proportions and ivory/teal/rubber/amber palette. It adds a tapered optic
barrel, rubber optic seal and rolled front lip; two neck collars; tapered
forearm and shin housings; wrist/ankle seals; elbow caps and ivory retaining
rings; a rounded abdominal recess with an ivory service door and teal latch;
and two tapered power pods with collars beside the vented backpack.

The rings and tapering are visible shape changes rather than surface noise.
The service-door cut is scoped to the body, and the softened display recess
stays scoped to the chest panel. The backpack vents and ivory spine remain
readable between the pods. The lens has no decorative highlight sphere.
An initial native inspection moved neck and ankle collars out of the surrounding
solids so their assembly boundaries read from the selected views.

The final explicit tree has 82 primitive leaves, 163 instructions and 82 named
history steps. The build preset scales those steps to 50 seconds, then orbits
for twelve seconds. Static and presentation presets share materials, camera,
hard self-shadows and atmosphere treatment with the preceding detail model.
Rear/backpack inspection pairs reverse the sun identically for both models.

## New contracts

| Addition | Parameters and role | Distance and appearance contract |
| --- | --- | --- |
| Torus | Major/minor radius, about local Y; optic lips, seals, retaining rings | Distance to a revolved circle; positive radii, minor strictly below major; inherited rigid transform and positive uniform scale |
| Capped cone | Bottom/top radius and positive half-length, along local Y; tapered housings and pods | Revolve the exact minimum distance to two cap segments and the side segment; a zero end is a tip, equal ends match a cylinder, both zero are rejected |
| Smooth subtraction | Two children, positive blend radius; rounded panel recesses | Polynomial smooth maximum of body distance and negated cutter distance; up to radius/4 more removal than hard subtraction; body material unless an explicit cut material wins the cutter-facing half, body owns an exact tie |

The new analytic implementations derive from these geometric constructions;
no external shader implementation was imported. Both primitive functions are
1-Lipschitz. Polynomial smooth maximum has nonnegative gradient weights that
sum to one, preserving that bound for composed distance bounds. The existing
cell-center distance/radius classifier and workspace clipping therefore remain
valid. These bounds do not turn an arbitrary Boolean composition into an exact
Euclidean signed distance everywhere; conservative traversal remains the contract.

The old 64-leaf budget was nearly exhausted at 61. The bounded replacement is
128 leaves / 256 instructions, with tree depth 24, stack depth 32 and 16 materials
unchanged. Shared C++ constants govern compiler limits and both field/renderer
program buffers. The field program adds 8,192 bytes; full axis-64 field allocation
is 912,882,864 bytes. Atlas resolution, brick capacity and the 2 GiB field ceiling
stay in place. The upfront budget calculation now includes the program buffer,
matching the existing post-allocation check.

Smooth subtraction participates in negative-leaf classification, body-first
history validation, committed-state reconstruction and cut previews. An earlier
cutter cannot silently disappear until its body is added. CPU material selection,
GPU distance evaluation and GPU surface material/part evaluation use the same
operator rules. Named histories retain one reveal per primitive.

## What to consider next

| Candidate | Decision | Content it would unlock |
| --- | --- | --- |
| Intersection | Next small Boolean candidate when an object needs it | Trim a curved form to a silhouette or make clipped/chiseled housings; define winning-branch materials and history semantics first |
| Shell / onion | Useful after a hollow-content example is chosen | Thin cases, helmet shells and cutaways; needs a unary instruction and explicit inner/outer material semantics |
| Rounded intersection / chamfer | Later, driven by a visible joint | Machined transitions and bevels; retain conservative distance bounds and define material ownership |
| 2D profile extrusion / revolution | Strong next authoring expansion for another subject | Bespoke panel silhouettes, noncircular grips and recognizable components without many overlapping boxes |
| Named linear/radial repetition helpers | Prefer explicit author-time expansion first | Fasteners, vents and ribs with stable names and inspectable history; no domain folding in the tracer needed |
| Bend/twist/nonuniform scaling/displacement | Defer until a specific model requires them | Flexible hoses and organic forms; changes to distance bounds and cell pruning need their own qualification |

The next content gain need not be another larger robot. A second recognizable
subject can test whether the vocabulary and authoring workflow generalize.
These candidates are recorded options, not promised implementations in this pass.

## Native qualification

`10c4957b` adds the vocabulary and bounded capacity extension. The initial
foundation qualification at `outputs/sdf/fieldkit-foundation-qualification/`
was captured with that implementation uncommitted; binary and all 13 shader
hashes match the final content qualification. Its nineteen fixtures include
the existing shapes/CSG/material cases plus transformed tori, tapered/pointed/
equal-ended cones, scoped smooth cuts and explicit/inherited cut materials,
and both the old 64-leaf and new 128-leaf native boundary scenes. Existing
capacity-failure rollback, legacy/authored transitions, batch/reset/subtraction
checks, original robot views and export/reload, material-only cache reuse,
legacy cut and 2,048-stick ray checks also pass. Six selected CTests pass:
recipe/authored CPU tests and four renderer/build policies.

The final field-kit content is committed at
`42edb21206f636323ba116309dde76c88e3a6a23` and clean at both main and history
qualification start. `outputs/sdf/robot-fieldkit-qualified/` retains native
commands, applied configurations, source scene hashes, binary/shader hashes,
captures, export/reload data and native/labeled video. Later changes only
assemble the report, improve qualification metadata/view labels and document
the results; no native code, shaders or scene geometry changed after capture.

Geometry is `d6259ed7730cec6b`, appearance `d4a6f0e3fd495262`; the completed
model uses 137,676 bricks of the 262,144-brick capacity. One hero field build
measured 91.608 ms wall time, including 1.012 ms filter and 89.668 ms brick GPU
time. Updates continue to rebuild synchronously.

All 83 history states pass Vulkan validation, the sampled-field CPU ray oracle
and direct CPU/GPU distance/material/part evaluation. The history validation
run includes 88 oracle comparisons, 37,024 rays and 89,982 direct points when
initial/endpoints and midpoint/empty/final revisits are counted. The final
model has 581 rays, 384 hits, zero mismatches or exhaustion, max nominal hit
difference 0.0180185, max surface residual 0.000120965 and five separately
reported finite-grid sampling ambiguities under the existing tolerance.
The final direct evaluation has 1,303 points, max distance difference
5.96046e-8, zero material error and no part mismatch.

Final committed/static images are byte identical under unchanged replay gates.
Explicit export/reload/export preserves scene bytes. Moving the identical
field to atlas axes 80 and 92 passes native checks and produces byte-identical
hero images. The preceding accepted 61-shape hero also renders byte identical
to its lens-cleanup capture with the new binary; the parity receipt is
`outputs/sdf/robot-fieldkit-regression-parity.json`.

All nineteen cut preview pairs preserve geometry/appearance fingerprints.
Only the three individually declared rear vent previews are occluded from the
fixed front construction camera. The 62-second native video visits all 82
edits in order, with 83 field builds including empty. All 1,860 frames are
decoded and screened for visible magenta traversal exhaustion; none is found.
The labeled video retains the original native capture separately. Inspection
of the finished orbit confirms that power pods, optic layers and joint seals
remain readable as the camera moves.

The private Xvfb receipt at `outputs/sdf/robot-fieldkit-controls/` passes resize,
orbit/zoom, pause/reset, Next 1–5, Previous/Next, scrub to final 82 and midpoint
41, stable paused state, restart empty, resume/pause and Escape. It records
17 field builds and the same binary/shader hashes as the native matrix.

Matched RTX 5070 Ti / 610.57.04 static profiles use 1920x1080, 1x, hard shadows,
180 submitted frames and 30 warmup frames:

| Scene | Median GPU frame | p95 | Completed samples |
| --- | ---: | ---: | ---: |
| Cleaned detail model, 61 leaves | 3.084 ms | 3.294 ms | 148 |
| Field kit, 82 leaves | 3.193 ms | 3.352 ms | 148 |

These sum trace and shared post; field construction, readback and encoding are
excluded. Both run the new vocabulary implementation, so the comparison isolates
the content change rather than mixing older shader versions.

## Delivered review and reproduction

`tools/qualify_robot_detail.py` accepts independent static/build/baseline presets
while preserving the earlier detail-study defaults. The report tool's
`--fieldkit` treatment compares the cleaned 61-shape model with the 82-shape
variant. It uses counts from the qualification, the recorded allocation and
profile data, field-kit labels and the appropriate authoring script. Subsequent
qualification receipts also record the compiler's named capacity constants.

```sh
python3 projects/sdf/tools/qualify_robot_detail.py --video \
  --config projects/sdf/presets/survey-robot-fieldkit.json \
  --build-config projects/sdf/presets/survey-robot-fieldkit-build.json \
  --baseline-config projects/sdf/presets/survey-robot-detail.json \
  --output outputs/sdf/robot-fieldkit-new
xvfb-run -a -s '-screen 0 1280x1100x24' env CUBEY_SDF_PRIVATE_XVFB=1 \
  python3 projects/sdf/tools/check_authored_window.py --history-controls \
  --config projects/sdf/presets/survey-robot-fieldkit-build.json --height 1000 \
  --output outputs/sdf/robot-fieldkit-controls-new
python3 projects/sdf/tools/robot_detail_report.py --fieldkit \
  --input outputs/sdf/robot-fieldkit-new \
  --controls outputs/sdf/robot-fieldkit-controls-new \
  --output outputs/sdf/robot-fieldkit-report-new
```

The delivered export at `outputs/sdf/robot-fieldkit-review/` additionally bundles
the compatible native foundation-regression matrix through `--sampler-regression`.
The publisher verifies served HTML bytes. Local and published-URL browser
checks cover all seven native image pairs, comparison modes and wipe endpoints,
full-image restoration after moving the wipe, five decoded video chapter seeks,
playback/captions, all 83 committed-state images and labels, previous/next and
keyboard slider input, selected-edit seeking, links, image decoding and 390px
layout/video ratio. No page JavaScript errors occur. Receipts are
`outputs/sdf/robot-fieldkit-report-browser-review.json` and
`outputs/sdf/robot-fieldkit-report-delivery.json`. Documentation navigation passes.

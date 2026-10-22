# SDF survey robot mechanical detail study

Date: 2026-10-22

The user positively reviewed the original robot and its
[edit-by-edit presentation](sdf-authored-history-evidence.md), then asked to push
the authored content further. This separate variant preserves that accepted
scene, pose, silhouette and four plain materials. Its visual acceptance remains
for the user to judge.

[Open the native detail comparison and build video](http://starship:8787/reports/8344b13c50933132bbf4db4a064de66a9f1197a9ff74fa8fdad91368966d5232/).

The initial 62-shape study below is retained as captured. The subsequent
[lens cleanup](#lens-cleanup-follow-up) removes one decorative sphere and its
history step from the current detail scene.

## Authored additions

`author_robot.py --detailed` writes `scenes/survey-robot-detail.json` and its
62-step history. The default generator still reproduces the original
`survey-robot.json` byte for byte. New static/build presets select the variant
without changing the original routes.

The pass adds 19 leaves, reaching 62 of 64 and 123 of 128 instructions:

- Two capsule hands receive local slots to become open two-finger grippers.
- A recessed display gets a dark backing plate and three raised amber gauge
  bars, reworking the original single indicator.
- Two teal head fasteners sit inside counterbores.
- Two slotted shoulder caps make the joints read as mechanical connections.
- Two shallow cuts divide the toe bumpers.
- Three rear cooling slots cut the backpack case independently of its spine.

All use existing boxes, spheres, capsules, cylinders and scoped CSG. No material
type, procedural noise, image map, UV system, primitive limit or atlas budget
was added. The four palette entries and shared atmosphere/hard-shadow treatment
remain the same. Assistant inspection selected forward-facing shoulder caps
after the first side-facing trial was less readable in the hero view.

Final geometry fingerprint is `65bef3c1d9f1df67`; appearance is
`b3fb1a5f33da987f`. The field uses 128,182 of 262,144 bricks, versus 125,200 for
the original. Axis-64 field allocation remains 912,874,672 bytes. The full
detail build in the hero capture takes 65.138 ms wall time, including 0.827 ms
filtering and 63.340 ms brick sampling GPU time. Geometry updates remain
synchronous.

## Atlas sampling correction

The first full detail qualification passed all geometry checks but failed the
existing image replay maximum: three pixels differed by more than one code
value, with max 39 at (569,223), near the antenna shadow. Mean RGB difference
was only 0.0000730613, and geometry/appearance fingerprints matched. The max-16,
mean-0.002 and fraction-above-one-0.0001 image gates were retained.

The tracer previously added brick origin to the local coordinate and filtered
through normalized atlas UVs. Physical brick ordering varies between builds.
Replacing that lookup with eight integer texel fetches and trilinear weights
computed in brick-local space removes dependence on atlas placement. A
controlled same-scene static/history pair became byte identical. This change
also applies to normals, shadow queries and the legacy tracer. It changes no
stored R16F data, field allocation, tracing limit or geometric tolerance.

`outputs/sdf/atlas-sampling-audition/` retains original shader recovery copies,
the two sampler profiles and the controlled receipt. On RTX 5070 Ti / 610.57.04,
matched 1080p 1x hard-shadow profiles (180 frames, 30 warmup, 148 completed GPU
samples) measured 2.634 ms median / 2.814 ms p95 with hardware filtering and
3.073 / 3.244 ms with local interpolation. The roughly 0.44 ms cost was accepted
for replay stability. This controlled comparison is distinct from the final
original-versus-detail measurements below.

The final qualification renders identical voxel data in atlas-axis 64, 80 and
92 layouts. Both cross-layout RGB comparisons are byte identical on the selected
device. This is native evidence for placement independence, not a claim of
bit-identical output across different GPU models or drivers.

## Final qualification

Source `48f12b3c70183fac7b97a6a72ea24330a2a8427f` was clean at the main capture
start. `outputs/sdf/robot-detail-qualified-stable/` pins the binary and 13 shader
hashes, source scene hashes, explicit scene, authoring helpers, commands, applied
settings, images, video and profiles. Subsequent edits assemble and document the
report; no native code, shader or geometry changed after this capture.

All 63 history states pass required Vulkan validation, the sampled-field CPU
ray oracle and direct CPU/GPU distance/material/part evaluation. Midpoint,
empty/reset and final revisits pass on the same field, with unchanged-state
cache reuse. Including revisits and endpoints, the history-validation run has
68 comparisons, 25,884 rays and 64,762 direct points. The final robot passes
501 rays with 306 hits, zero mismatch or exhaustion, max nominal hit difference
0.0150175, max surface residual 0.000117287 and four separately reported
finite-grid sampling ambiguities. Direct evaluation passes 1,163 points with
max distance difference 5.96046e-8, zero material error and no part mismatch.

The final history and static images are byte identical under the unchanged
image gates. Explicit export/reload/export preserves the scene bytes, names,
palette, all 62 history entries and fingerprints. Both additional atlas layouts
also pass native ray/material checks.

The 54-second native video has 1,620 frames at 30 FPS, 1280x960, with 2x internal
sampling: 44 seconds of construction and ten of orbit. Logs visit edits 1–62 in
order with exactly 63 field builds including empty. All encoded native frames
are decoded and screened for visible magenta traversal exhaustion; none is
found. Labels are added after capture, and the original native video is retained.

There are 18 cuts. Every on/off preview pair preserves geometry and appearance
fingerprints. The three rear vent previews are fully occluded from the fixed
front construction camera. They are individually declared and measured; every
other preview changes more than 100 image channels. The potentially occluded
far shoulder was also declared, but its preview is visible. Rear and backpack
inspection pairs use a reversed sun for readability, identically for both
variants; the construction/orbit video retains its fixed original sun.

The final private Xvfb receipt at `outputs/sdf/robot-detail-controls-stable/`
identifies the same binary and shader hashes. Resize, orbit/zoom, pause/reset,
Next 1–5, Previous/Next, scrub to 62 and 31, stable pause, restart to empty and
resume/pause pass. It records 17 field builds.

The sampler regression at `outputs/sdf/robot-detail-sampler-regression/` passes
the eleven existing primitive/material fixtures, nested blends, failed-build
rollback, legacy/authored transitions, batch/reset/subtraction matrices,
explicit robot replay and legacy cut/2,048-stick ray checks. It was captured
while the sampler fix was uncommitted; its recorded compiled shader and binary
hashes match the final clean-source matrix. A fresh 4,096-cut settled-demo
capture also passes Vulkan validation and contains no screened exhaustion
pixels. CPU recipe/authored targets and four renderer/build policies pass
(six selected CTests).

## Matched rendering cost

Both final variants use the corrected sampler. RTX 5070 Ti / 610.57.04,
1920x1080, 1x, hard shadows, static hero, 180 submitted frames and 30 warmup:

| Scene | Median GPU frame | p95 | Completed samples |
| --- | ---: | ---: | ---: |
| Original, 43 leaves | 2.989 ms | 3.261 ms | 148 |
| Detail study, 62 leaves | 3.078 ms | 3.314 ms | 148 |

GPU frame sums tracing and shared post. Construction, readback and encoding
are excluded. These current measurements should not be combined with the older
hardware-filtered presentation's timings as a controlled content comparison.

The visual review provides seven matched original/detail pairs (whole robot,
upper body, face, feet, rear, backpack and side), a comparison wipe, the extended
construction video and all 63 committed-state images. This remains one authored
toy with a fixed pose; it does not add general parameter-edit history or a
sculpting editor.

## Delivered review

`tools/robot_detail_report.py` assembles
`outputs/sdf/robot-detail-review/` from the final native qualification and desktop
receipts, with the optional sampler audit and existing-renderer regression set.
The original/detail buttons and comparison wipe share native camera/light
settings per view. The page also provides the labeled/original video, selected
edit seeking and an expandable viewer for all 63 committed states.

Publication verifies served HTML bytes. Local and published-URL browser checks
cover seven image pairs, three comparison modes, wipe endpoints and returning
to a full detail image, five decoded video chapter seeks, playback/captions,
all 63 edit images/labels, previous/next boundaries, keyboard slider input,
selected-edit seeking, local links, image decoding and 390px layout/video ratio.
No page JavaScript errors occur. Receipts are
`outputs/sdf/robot-detail-report-browser-review.json` and
`outputs/sdf/robot-detail-report-delivery.json`.

Documentation navigation and all 15 selected native SDF CLI recipes pass. CLI
recipes use template early exit with unavailable GPU/display variables,
separately from the native execution checks above. The work is committed in
content, qualification, sampler-fix and presentation/evidence checkpoints.

## Lens cleanup follow-up

The user found the fixed ivory dot on the lens odd. It was a literal sphere
named `sensor-glint`, including its own shadow. The detail generator now omits
that sphere and its history entry, letting actual scene lighting provide
highlights. The original 43-shape scene and default generator remain byte
identical. The current detail scene has 61 leaves, 121 instructions and 61
history entries. Geometry is `37c08624cc9a4f57`, appearance `952d6e9226c78f4e`,
with 128,160 active bricks and the same field allocation.

[Compare the native lens before and after](http://starship:8787/reports/0a6102e62987f7754d93f4baf74fd2a4cdefe6d29d9a2ff7994d8c3c7c2b14c9/).
The earlier detailed build video remains an immutable capture with the dot;
the current static and build presets both use the cleaned scene.

`outputs/sdf/robot-lens-cleanup/manifest.json` records source scene hashes,
binary/shader hashes and native commands. Captures compare the preceding
`dd563a0a` scene and current scene using identical camera, lighting and materials
for face and full-model views. This was captured with the content edit
uncommitted; the source snapshot and generator are included in the export.
All 62 scene states pass Vulkan validation, sampled-field CPU ray checks and
direct CPU/GPU distance/material/part comparisons. Midpoint, empty and final
revisits and cache reuse also pass. Final static/history images and exported
scene bytes are identical. All five captured images have zero screened magenta
exhaustion pixels. No renderer, shader, material or lighting code changed.

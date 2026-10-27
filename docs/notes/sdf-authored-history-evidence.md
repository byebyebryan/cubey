# SDF authored robot history evidence

Date: 2026-10-21

This is the V2 authored-history checkpoint for the robot at the native source
revision recorded below. Current material-enabled authored scenes use V3 while
the V2 path remains supported; see the [current SDF guide](../../projects/sdf/README.md#authored-scenes)
and [shared material contract](../architecture/material-recipes.md). The
measurements and V2 history contract below remain pinned to this checkpoint.

The [first authored robot](sdf-authored-content-evidence.md) now has an explicit
edit-by-edit presentation. This work exposes its construction using the same
geometry, pose, materials, hard shadows and shared atmosphere.

[Watch the build and inspect every edit](http://starship:8787/reports/5d6bfde3c2493278b5c68e1ddce68381942ff9d8ef60672a553aa4b6a52f0c7f/).

## History contract

The version-2 scene optionally stores a `history` array naming every primitive
exactly once, with a short label and duration weight. The survey robot has 43
steps: 37 additions and six cuts. They form torso/hip, neck, head shell and
socket/vents, optic barrel/bore and sensor layers, chest, articulated limbs,
backpack and antenna. The sequence was written deliberately rather than derived
from the postfix instruction order.

Each intermediate state is a valid grouped expression. Incomplete unions and
smooth unions retain a unary transform/material group; absent bodies keep their
subtraction empty. The loader rejects unknown/repeated leaves, invalid durations
and orders that introduce a cutter before its retained body. An empty root is
explicitly supported on CPU/GPU. Material values remain independent of history.
Export retains the complete scene, palette and history.

The final state retains geometry `583e07fe12717184` and appearance
`4a91b557ad4f421d`, identical to the preceding authored milestone. Its 43 leaves,
85 instructions, 125,200 active bricks and axis-64 allocation are unchanged.

## Native playback

`presets/survey-robot-build.json` selects 32 seconds of construction with a fixed
three-quarter capture camera, followed by a ten-second orbit. The existing
Construction/Presentation modes have authored-history semantics when loading
such a scene. Static/orbit remain supported for scenes without history; legacy
timelines keep their prior behavior.

The first 35% of each cut interval fades a cyan, depth-limited preview of the
actual transformed primitive. This is a color-only cue on the current field;
the following commitment publishes a rebuilt scene containing the subtraction.
Previous/Next and an integer Edit slider seek after commitment and pause.
Play/Space, Restart/R, camera orbit/zoom and the full timeline remain available.
A capacity failure preserves the old field and returns playback to its last
published edit.

The offline native capture has 1,260 frames at 30 FPS, 1280x960, with 2x internal
sampling. It visits all edits 1 through 43 in order. Logs record exactly 44 field
builds including the empty state; camera and preview frames reuse the cache.
Separate ASS captions add edit labels after native capture. The original native
MP4 is retained alongside the labeled presentation. Assistant inspection of the
socket and optic previews, the committed socket, assembly stages and orbit
found the intended operations visible. Rear backpack edits become visible in
the final orbit; the desktop camera can inspect them while paused.

## Qualification

Native runtime source is `874549cf5c3d2748db4719c2f37d56cba972f320`, with a
clean worktree at the main qualification start. The commands, binary/shader
hashes, scene, applied configs, 44 committed-state images, six on/off preview
pairs and native/captioned videos live under
`outputs/sdf/robot-history-qualified/`. `qualify_history.py` reproduces this set
and can also run the existing authored/legacy matrix.

All 44 states pass required Vulkan validation, sampled-field CPU/GPU ray checks
and direct distance/material/part comparisons. The same field revisits midpoint,
empty and final states; unchanged inputs preserve build count. Including those
revisits and endpoint checks, the history-validation run passes 49 comparisons,
16,781 rays and 43,393 direct expression/material points. No ray mismatch or
exhaustion occurs. Finite-grid sampling ambiguities remain separately reported
under the existing tolerance; later sensor stages retain the prior four
ambiguities. No geometric tolerance was relaxed.

Every cyan preview/on-off pair has identical geometry and appearance
fingerprints. Each visibly changes more than 100 image channels. The final
history state matches the static robot fingerprints; its RGB image differs in
113 channels, maximum one code value and mean 0.000040871. The separately
recorded replay limits remain max 16, mean 0.002 and fraction above one 0.0001.
Image comparison does not replace the ray oracle.

All 1,260 native frames were decoded and screened for the visible magenta
traversal-exhaustion color; none met the RGB threshold. The existing eleven
primitive/material fixtures, nested blends, field rollback/transitions,
batch/reset/subtraction checks, explicit scene replay and legacy cut/2,048-stick
ray checks also pass with Vulkan validation. CPU recipe and authored tests pass,
including history inheritance, local cuts, empty persistence, seek semantics,
preview timing, final fingerprints and invalid sequences.

The isolated Xvfb receipt at `outputs/sdf/robot-history-controls/` records 17
actual field builds while exercising resize, orbit/zoom, pause/reset, Next 1–5,
Previous to 4, Next to 5, scrub to 43 and 21, stable pause, restart empty and
resume/pause. The receipt identifies the same binary hash as the native matrix.
The existing static robot material-control check also passes, retaining one
field build while changing roughness.

## Costs and limits

On the RTX 5070 Ti / 610.57.04, the native video's changed-state builds range
from 0.543 to 44.209 ms wall time (median 16.532 ms across 44 different states).
These cover different amounts of geometry and are offline capture measurements.
Geometry construction remains synchronous; this playback does not qualify
smooth continuous sculpting. The field allocation remains 912,874,672 bytes;
history adds CPU metadata and a small per-frame preview uniform, with no new
geometry or material atlas.

Matched final-object 1920x1080, 1x profiles submit 180 frames, discard 30 warmup
frames and retain 148 GPU samples. Tracing plus shared post measures 2.221 ms
median / 2.436 ms p95 without shadows, 2.526 / 2.748 ms with hard shadows and
2.353 / 2.572 ms for the clay override with hard shadows. Construction, readback
and encoding are excluded from these static rendering timings.

History reveals leaves in a fixed final pose. It does not record arbitrary
parameter/transform edits or provide a sculpting editor. The native UI and
report make this object's additions, smooth connection and local removals
inspectable. User assessment of whether that presentation sells the experiment
remains separate from its technical checks.

The user subsequently described this result as great and asked to develop more
authored detail. The [separate detail study](sdf-robot-detail-evidence.md) retains
this accepted 43-shape version and extends the content within the existing cap.

## Delivered review

`tools/history_report.py` assembles the dedicated export at
`outputs/sdf/robot-history-review-final/`. The report provides the labeled and
original videos, a 44-state slider/named selector with Previous/Next, a
Watch this edit seek control, cut before/preview/after views and native receipts.
It links the preceding robot review and Cubey catalog. Publication verifies the
served HTML bytes; browser checks verify playback, six decoded chapter seeks,
caption timing, every committed image/label, selection controls and boundaries,
keyboard slider input, local artifact links and a 390px layout with the correct
video aspect ratio. No page JavaScript errors occur. The static publisher's lack
of Range support is handled with a seekable video Blob.

The local and published-URL checks are recorded in
`outputs/sdf/history-report-browser-review.json` and
`outputs/sdf/history-report-delivery.json`. Documentation navigation and all 13
selected native SDF CLI recipes pass. The CLI check uses template early exit
with unavailable GPU/display variables, separately from native execution above.
The four renderer/build policy checks and both CPU SDF targets also pass.

The native capture remains pinned to `874549cf`; subsequent edits only
assemble/document this review and remove unused Python imports. Current binary,
shader and capture hashes match the qualification manifest.

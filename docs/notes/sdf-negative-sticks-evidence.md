# Negative-sticks implementation evidence

Date: 2026-10-21

Status: native implementation and qualification complete;
[visual review published](http://starship:8787/reports/3d8baa7ee96f132b28062ace6e4878538982df72c66aea7c0094f884b2729f65/).
User aesthetic acceptance remains separate.
The [plan](sdf-negative-sticks-plan.md) defines the experiment and acceptance.

## Native geometry audition

The first generator uses the planned rounded cube, PCG32 orientations/offsets
and index-based cutter width bands without geometry retuning. It exports one
Fill plus N Remove edits under `negative-sticks-pcg32-v1`. Increasing count
preserves the earlier recipe prefix, and zero cuts retains the solid.

Receipts under `outputs/sdf/`:

- `negative-audition-axis64/manifest.json`: all nine seed/count combinations
  failed explicitly at the axis-64 capacity. Frontier demand was roughly
  474,000–571,000 cells against 262,144 capacity; no successful captures were
  claimed.
- `negative-audition-axis92/manifest.json`: all nine rendered with the axis-92
  profile. The initial sun at elevation 55 / azimuth -35 left the visible
  walls too dark for the intended presentation.
- `negative-light-audition/`: matched native captures at four sun azimuths,
  retaining geometry and camera. Elevation 35 / azimuth 160 illuminated the
  two visible faces more clearly using the existing lighting.
- `negative-audition-front-lit/manifest.json`: nine matched captures at the
  revised light. `negative-audition-selected/manifest.json` repeats that matrix
  and adds base/four/sixteen-cut stages and opposite views for two candidates.

The provisional presentation choice is seed 42 with 72 cutters: connected
openings are visible on adjacent faces, substantial walls remain, and the
opposite view retains the recognizable cube and some sky through holes. The
opposite face is darker under the fixed sun; the report should expose that
view. Seed 91 / 48 cuts is retained as a comparison. This is the agent's native
image inspection, not the user's aesthetic acceptance.

The chosen recipe contains 73 edits, geometry hash `c92ff9ee08a5af98`, and
555,062 active bricks in the initial capture. The axis-92 field allocation is
1,974,880,928 bytes, excluding frame targets and driver allocations. The cold
build was about 53 ms; this is not a steady performance qualification.

The 48-cut seed-42 scene passed the current CPU/GPU oracle: 305 rays, 220 hits,
zero mismatches, exhausted rays or sampling ambiguities; maximum surface
residual 0.000113015. Final-scene and targeted subtractive checks remain pending.
The focused CPU recipe tests pass, covering deterministic subtraction,
prefix stability, persistence, zero cuts and count-limit rejection.

Audition manifests record the parent source revision with a dirty worktree
because capture preceded the checkpoint commit. They pin the actual native
binary and compiled shader hashes. Fresh final qualification will identify the
committed implementation independently.

## Carving timeline and desktop integration

`carve` begins with the initial Fill already built. It holds the base, applies
up to four slow introduction cuts, accelerates through the remaining removals,
then orbits the unchanged final field. Timeline samples identify the next
stored cutter and its cue opacity; rendering the cue is the next checkpoint.
The motion validates the actual recipe's Fill/Remove sequence, including
explicit recipes loaded independently of the CLI scene name.

The focused CPU tests cover small recipes (0/1/3/4 cuts), all four slow commit
boundaries, accelerated/final prefixes, backward seeking, shared 30/60 FPS
logical times and incompatible recipes/timing. Native time-zero playback
rendered one edit and passed 257 oracle rays without mismatches, exhaustion or
sampling ambiguities. The desktop generator selection now controls regeneration;
changing seed/count alone retains the loaded recipe until Generate scene.
Windowed interaction qualification remains pending.

## Cutter guide and first presentation

The slow introduction shows a cyan outline of the next cutter's oriented
bounding box, clipped to the workspace and hidden behind the current solid.
The guide composites color in the existing surface pass without changing
field contents or surface depth. Its data uses the existing per-frame uniform
buffer; camera push constants remain 128 bytes. `--no-cutter-cue` disables it.

`projects/sdf/presets/negative-sticks.json` selects the 72-cut recipe, hard
self-shadows, atmosphere sky, and the planned 24-second carve/orbit sequence.
An initial native capture contains 720 H.264 frames at 1280x720 / 30 FPS, with
2x internal sampling. The introductory guide and a decoded twelve-second
frame were inspected. The final qualification will recapture the video from
committed source and inspect the remaining phases.

## Final native qualification

`outputs/sdf/negative-qualification/manifest.json` pins the native source at
`ad9948f5`, clean at the qualification start, plus binary, compiled shader and
native source-file hashes. Verification scripts were subsequently refined to
measure bounded image comparisons and safely reuse unchanged native runs;
the manifest separately records that verification context/script hash. Applied
capture settings are saved per run. No native source or runtime artifact
changed during that verification/report work.

The selected scene retains hash `c92ff9ee08a5af98`, 73 edits and 555,062 active
bricks. The final-scene oracle passed 547 rays / 372 hits, with zero mismatches,
exhaustion or sampling ambiguities and maximum surface residual 0.000120123.
Intersecting-cavity and perforated-plate checks passed at 128/129 and 256/257
total edits, including backward/reset and 64/128-edit batching. The plate gives
each late cut a distinct visible hole. Original additive batch, primitive and
2,048-stick checks also passed. Vulkan validation reported no errors.

The expected axis-64 rejection and incompatible-carve recipe rejection both
failed explicitly without producing successful captures. The chosen field
allocation remains 1,974,880,928 bytes (1.84 GiB), excluding frame targets and
driver allocations.

On NVIDIA GeForce RTX 5070 Ti / driver 610.57.04, fixed final-scene 1920x1080,
1x sampling, 180 submitted frames, 30 warmup and 148 completed GPU samples:

| Rendering path | Median trace + post | p95 |
| --- | --- | --- |
| Primary | 1.833248 ms | 2.006112 ms |
| Hard self-shadows | 2.249264 ms | 2.427456 ms |

Rendering meets the original steady 60 FPS primary / 30 FPS lit planning
targets on this device. These measurements exclude construction and encoding;
they are not a matched performance comparison with the differently framed
additive scene.

Logical carving profiling submitted 720 frames, with 688 completed GPU samples
and 690 post-warmup CPU preparation samples. Its 73 builds measured 47.8793 ms
median / 52.7165 ms p95 / 53.0569 ms maximum. The `sdf.prepare_geometry` CPU
span had p95 50.412477 ms and maximum 55.40626 ms, including queue waits.
Unchanged frames reuse the completed field. These periodic stalls remain a
limit on live carving; a smooth offline video does not qualify smooth desktop
updates. No asynchronous builder or update optimization was introduced.

## Image replay boundary

Explicit save/load and generated recipes have matching geometry hashes and
pass the unchanged geometric oracle. Byte-identical PNGs proved too strong an
image acceptance rule: independently rebuilding the same input also changes a
small number of sampled image values. The precise hardware/numerical cause
was not isolated; allocation order and normalized atlas interpolation remain
possible contributors, not established diagnoses.

At the fixed 38-cut middle state, generated versus loaded images differed in
2,088 of 3,686,400 RGBA channels, with mean absolute difference 0.0005778 on
the 0–255 scale, maximum 11, and only 16 channels differing by more than one.
An identical-input rebuild had maximum 6 and similarly small mean difference.
The recorded image check requires maximum <=16, mean <=0.002 and <=0.01% of
channels differing by more than one. This separate image check does not relax
the SDF ray/sampling tolerance or establish exact pixel reproducibility.

The cue-disabled base and 30/60 FPS cue captures differ by at most one code
value. The guide visibly changes the preview while the requested geometry
prefix remains one. Logical prefix/cue state tests are deterministic.

## Desktop and presentation review

The isolated Xvfb desktop check passed resize/orbit/scroll, pause/reset,
selection of Carve, scrubbing to end/middle/base, seed editing without an
implicit regeneration, and explicit negative-sticks regeneration. Observed
field prefixes were `[73, 1, 73, 39, 1, 73]`; camera/resize and staged seed
editing caused no extra builds. Seed 43 changed the complete recipe hash to
`293678f5fc53d17f`, retaining 73 total edits and the selected generator.
See `windowed-interaction.json`, its command/log and native screenshots.

The fresh video has 720 H.264 frames, 1280x720 at 30 FPS for 24 seconds,
with 2x internal sampling. Decoded images at 0/2.3/3/8/12/16/20/23.9 seconds
show the base, guide, cutting and orbit against the shared atmosphere sky.
The back faces remain darker under the fixed sun. No ground plane, terrain,
GI or smooth Boolean operators were added.

The connected browser service was unavailable. An isolated headless Chrome
check instead passed all six video chapters, actual decoded playback, six
capture views, five keyboard-operated carving stages, nine decoded audition
images, local links, a 390px layout without overflow and no page JavaScript
errors. `report-review.json` pins the reviewed HTML hash; desktop/mobile
screenshots were also inspected. This checks report behavior, not the user's
assessment of whether the sculpture is compelling.

The initial published snapshot passed entrypoint-byte verification, but its
HTTP chapter seeking failed despite passing in the local file export. The
publisher returns the MP4 without HTTP Range support; Chrome buffered the
video but exposed a seekable range of zero. The first served-browser pass
statement was premature and was corrected.

The revised report reuses the earlier SDF report's full-download-to-Blob
approach, enables chapters after media metadata is ready, and retains a direct
download fallback. All report controls, decoded playback and mobile layout
passed again over a local HTTP server without Range support.

The corrected published snapshot contains 217 intended files / 13,132,605
bytes. The shared publisher verified HTTP 200 and matching entrypoint bytes.
A separate isolated Chrome check at the actual served URL confirmed a seekable
Blob, all six chapter seeks with decoded frames, and native image decoding;
the HTML matched the reviewed hash. See `outputs/sdf/negative-delivery.json`.
The existing report server was reused; the earlier additive report remains
linked and unchanged.

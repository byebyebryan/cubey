# Negative-sticks implementation evidence

Date: 2026-10-21

Status: checkpoints A/B/C complete; final qualification and report pending.
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

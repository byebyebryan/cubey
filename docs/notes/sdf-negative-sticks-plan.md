# Negative sticks: procedural carving plan

Date: 2026-10-21

Status: proposed next visual experiment; implementation has not started.
Reviewed against the native V1 source at `42ef34da`. The additive demonstration
and its [qualification evidence](sdf-implementation-evidence.md) remain the
comparison baseline. This plan follows the user's choice of a negative ball
of sticks after reviewing other procedural directions.

## Purpose and visual target

Begin with a solid, subtract substantial sticks, and show separate holes
joining into a connected interior. Finish with a lit orbit that reveals large
remaining surfaces, internal walls and sky through the openings.

The experiment should communicate repeated solid editing and changing
connectivity without an artist-authored asset. Its educational subject is the
path from an ordered subtraction recipe to cached distance bricks and a shaded
surface. Mesh Booleans can produce the same geometry; the demonstration does
not establish an exclusive SDF effect or a performance advantage over meshes.

Success requires both a readable carving sequence and an interesting final
object. Correct rendering alone does not establish either. Inspect a small
image audition before investing in presentation polish.

## Initial decisions

| Area | Planned choice |
| --- | --- |
| Starting solid | One rounded cube, using the existing rounded-box primitive. “Negative ball of sticks” names the idea; a sphere is not required. The cube provides recognizable before/after faces. |
| Cutters | Thick, randomly oriented rounded boxes that pass through the solid. Broad cuts first, smaller cuts later. |
| Distribution | Seeded randomness with offsets spread across a central disk in each cutter's local transverse plane. Do not reuse the additive generator's placement on a spherical shell. |
| Shading | Existing plain material, shared atmosphere sky/lighting and hard self-shadows. Initial audition uses AO off. |
| Scene integration | Add `negative-sticks` alongside `sticks`; preserve the existing scene, preset and playback behavior. Choose any new default only after reviewing captures. |
| First presentation | Solid hold, a few legible cuts, accelerated carving, then a clean orbit. Diagnostic views are separate captures. |

New primitives, smooth Boolean operators, terrain, a ground plane, GI,
destruction physics and sculpting controls are outside this experiment.
Primitive rounding already exists; it does not imply smoothly blended Boolean
seams. Detached pieces, if produced, remain in place.

## Deterministic geometry recipe

Add a generator identified as `negative-sticks-pcg32-v1`. A generated recipe is
one `Fill` edit followed by N `Remove` edits. For this scene, `--count N` means
N cutters; the UI and receipts distinguish cuts from the total N+1 edits.
Zero cutters produces the intact solid. Limit N to 65,535 so generated files
fit the existing 65,536-edit recipe loading contract.

Use the existing specified PCG32 generator and explicit edit serialization.
Same seed and count must reproduce the full ordered list. Increasing count
must preserve the previous list as a prefix: choose widths by absolute cut
index, not by the requested total. Record final constants under the generator
version; subsequent incompatible changes need a new generator identifier.
The exported explicit recipe remains authoritative for replay.

These are starting values for the audition, in the bounded unit workspace;
they are not measured quality or capacity claims:

| Parameter | Initial value/range |
| --- | --- |
| Base half extents / rounding | `(0.27, 0.27, 0.27)` / `0.018` |
| Cutter long-axis half extent | `0.65`, along local X; the existing workspace clipping still applies |
| First four cutter transverse half extents | Independently sampled in `0.04–0.05` |
| Cutters 5–16 transverse half extents | `0.03–0.04` |
| Subsequent transverse half extents | `0.02–0.03` |
| Cutter rounding | `0.004`; this expands the box beyond its half extents under current semantics |
| Cutter transverse offset | Uniform disk of radius `0.20`, using radius `0.20 * sqrt(u)` |
| Orientation | Existing uniform random quaternion construction |
| First count / seed audition | Counts `24, 48, 72`, seeds `7, 42, 91` |

Construct each cutter in its own coordinates, with center `(0, y, z)`, then
rotate both its center and axes by its quaternion. The offset disk lies inside
the base cube's inscribed sphere, so every candidate intersects the original
solid; earlier cuts can still make later candidates redundant. The long axis
extends beyond the base in both directions to create through-holes.

The remaining walls and open spaces should dominate the image. Do not add
thousands of cuts to compensate for a weak result. The first tuning options
are count, width bands and offset radius. Surface-guided placement, connectivity
solvers and automatic seed scoring are not prerequisites.

## Image audition before presentation work

1. Generate the nine seed/count combinations and capture final shaded images
   from one fixed three-quarter camera, with identical light and material.
   Save their explicit recipes, configurations and construction statistics.
2. Inspect at normal display size. Select at most two promising candidates;
   capture the intact base, four cuts, sixteen cuts and final state, plus the
   final object from the opposite side.
3. Choose one candidate for presentation, or revise the bounded recipe once
   if all candidates have the same clear weakness. Record what changed and
   preserve the initial comparisons.

Visual checks: the initial solid is recognizable; cuts change the visible
surface; several openings join; broad walls remain; the opposite view is also
readable; some holes show sky; cavities retain enough illumination to show
their shape. An image that reads mainly as a black cavity or a nest of thin
slivers fails the audition. Tune the existing sun, exposure and framing before
adding lighting features.

Start with the explicit axis-64 atlas profile. If it overflows, record that
failure and select 80 or 92 explicitly for the next comparison set, or reduce
the scene. Never silently enlarge the profile or drop cuts. Subtraction can
increase surface area and active bricks even as solid volume decreases; fewer
edits do not guarantee lower field memory than the additive demonstration.

## Carving playback and cutter cue

Add a separate `carve` motion. The current construction/presentation motions
begin at prefix zero, which would show an empty workspace. Carving instead
starts at prefix one and applies only removal edits during playback.

The initial preset uses `duration = 24`, `construction-seconds = 16` and
`orbit-turns = 0.5`:

| Logical time | Visible action |
| --- | --- |
| 0–2 s | Hold the intact solid. |
| 2–8 s | Apply the first four cuts individually, with a short cutter cue before each commit and a pause to inspect the result. |
| 8–16 s | Apply the remaining cuts faster from the same ordered recipe. |
| 16–24 s | Hold the completed field and orbit through half a turn; hide the cutter cue. |

Express the first three phases as 1/8, 3/8 and 1/2 of construction time; the
orbit occupies the remaining duration. Use logical time for prefix and cue
state, including exact endpoints, direct seeking and captures at different
frame rates. At most four cuts receive the slow introduction. Handle N=0 and
N<4 explicitly: distribute the introduction over the available cuts, hold if
there are no remaining cuts, and show the intact base throughout construction
when N=0. Do not repeat or invent edits to fill the introduction.
Require positive construction time shorter than duration for this motion.

Validate the actual recipe when selecting `carve`: exactly one initial Fill,
then only Remove edits. This also permits exported/reloaded recipes whose
CLI `scene` value differs. Reject incompatible recipes clearly rather than
guessing from a generator name or silently changing geometry.

After the static audition passes, add one restrained cue for the slow cuts:
an oriented outline of the next cutter's bounds, clipped to the workspace and
occluded by the current solid. Identify it as cutter bounds; primitive rounding
means these bounds are not the exact rounded surface. Commit the actual
stored edit, hide the outline, and hold the result. The fast phase can omit
the cue if it becomes distracting.

Keep this cue project-local and color-only: it must not change field contents,
hit depth, shadows or the CPU/GPU geometry comparison. The current camera push
constants already occupy 128 bytes. If shader data is needed, extend the
existing per-frame uniform buffer with matching host/GLSL layout rather than
raising the push-constant requirement. Test cue visibility against hit depth,
including inside openings, and disable it for final stills and timing runs.

## Source changes and checkpoints

| Checkpoint / commit scope | Files and completion evidence |
| --- | --- |
| A. Generator and first image audition | `sdf_recipe.cpp`, `sdf_config.h`, focused recipe tests, and a small project-local audition capture helper. Add the scene enum and count semantics. Produce native stills and explicit recipes before choosing final generator constants/preset. |
| B. Playback and desktop integration | `sdf_timeline.{h,cpp}`, `sdf_app.cpp`, timeline tests. Add `carve`, preserve prefix one, and fix regeneration to use the selected generator: it currently hardcodes `make_recipe("sticks", ...)`. Label seed/count/regenerate controls by scene. Explicit loaded content changes only on an intentional regeneration action. |
| C. Cutter cue and presentation | `sdf_renderer.{h,cpp}`, `shaders/sdf.frag` and related local uniform declarations only as needed, plus `presets/negative-sticks.json`. Capture the slow introduction and completed orbit for inspection. Reuse the existing field builder and tracing semantics. |
| D. Qualification and evidence | Scene-specific capture/profile tooling, project guide and a new evidence entry/report. Record exact runtime artifacts, recipes, settings, limits and remaining issues. Preserve the prior report. |

Use reviewable local commits as work proceeds under the existing commit-as-you-go
instruction. The first checkpoint is a visual audition, not a full presentation
or renderer redesign. Runtime defects discovered through subtraction should
be isolated with a small failing fixture before changing the field pipeline.

The current `tools/qualify.py` and report assume the additive preset and fixed
capture names. Keep that baseline usable. Add a bounded negative-sticks path
or a dedicated local helper; do not relabel old additive receipts as evidence
for the new scene or build a generic experiment framework.

## Validation and acceptance

Geometry and persistence:

- Check Fill-first/Remove-tail order, deterministic seed behavior, increasing
  count prefix stability, zero cuts and total-edit limits. Round-trip an
  explicit subtractive recipe with every operation and its hash intact.
- Use a small known fixture with intersecting through-cuts. Compare CPU/GPU
  rays through holes, across internal walls, from inside a cavity and from
  inside remaining solid. Include an axis-parallel ray and a grazing case.
- Check subtractive playback across total-edit counts 128/129 and 256/257:
  these correspond to cutter counts 127/128 and 255/256 because of the base
  edit. Exercise backward seeking, reset and 64/128-edit batching with a
  deterministic fixture whose late cuts actually change the surface.
- Require no unexplained oracle mismatches, no exhausted rays and clean
  Vulkan validation. Report sampling ambiguities separately under the current
  finite-grid tolerance; do not relax it to pass the scene.

Playback and presentation:

- Check base at time zero, every introduction commit boundary, final tail,
  N=0/1/3/4, backward seeking, and equal logical states at 30/60 FPS.
- Verify regeneration keeps the chosen scene and updates the explicit recipe;
  loaded recipe playback obeys its contents. Camera, lights and cue-only
  changes must not rebuild the field. Exercise windowed pause, restart,
  scrubbing and resize on the selected scene.
- Inspect native shaded and normal captures for seams, missing walls, dark
  cavity artifacts and shadow bias problems. Review both cutter cue visibility
  and cue-free final views. Keep traversal/brick views outside the main clip.

Performance and delivery:

- Measure construction separately at representative prefixes, including CPU
  queue waits, GPU build stages, active bricks and allocated bytes. Current
  updates synchronously wait for the GPU and rebuild a partial tail. A smooth
  offline capture is not evidence of smooth interactive carving.
- Profile the fixed final scene at 1080p, 1x sampling, with primary tracing and
  hard shadows separately, reporting median/p95 and device/settings. Retain
  the original planning targets of 60 FPS primary and 30 FPS lit rendering;
  treat these as targets to check, not inherited results. Record live carving
  frame times separately if describing it as interactive.
- Deliver the seed audition, chosen explicit recipe/preset, shaded stills,
  short carving/orbit video and bounded correctness/performance receipts.
  Source checks, native visual inspection and the user's aesthetic judgment
  are separate acceptance results.

If the audition remains visually weak, report the native images and stop
presentation expansion. If only construction is slow, keep the visual result
and document the live-update limit before proposing an optimization. Neither
outcome automatically expands this task into new lighting, authoring or AI
content systems.

# SDF sculpture study: four subjects

Date: 2026-10-22

Status: implemented, qualified and delivered for visual review after the user's
acceptance of the survey robot experiment. The
[study evidence](sdf-sculpture-study-evidence.md) records completed checkpoints
and the gallery; the sections below retain the execution and acceptance plan.

## Objective and value

Author and present four distinct SDF sculptures: a stone guardian bust, a
pierced ceramic vessel, a wind-carved rock arch and twisted driftwood. Each
gets a reproducible explicit scene, static and construction presets, native
inspection views and an edit-by-edit presentation. Finish with a comparison
gallery and an honest account of what worked for each subject.

The robot established that scripted authoring can produce a successful small
assembly. This study tests continuous forms: merging masses, shaping rounded
transitions, hollowing a solid and joining cavities into through-openings.
Those changes should be understandable while watching the construction, with
little explanation. The final surface could also be represented by a mesh;
the demonstration is the SDF construction process and its results, without a
claim that SDF rendering is uniquely capable or cheaper.

The learning outcome matters too: record which forms are straightforward with
the current vocabulary, where it becomes awkward, and whether a small new
authoring operation earns its complexity. All four subjects receive a native
blockout and a finished candidate; an appealing first piece does not replace
the other three experiments.

## The four briefs

| Order and subject | Intended design | Construction moment to show | Main review risk |
| --- | --- | --- | --- |
| 1. Stone guardian bust | A compact, broad-faced guardian with a heavy brow, strong cheeks and jaw, short swept horns and a simple cropped base. Slight asymmetry gives it character. | Broad masses blend into one face; eye sockets and the mouth change its expression; nostrils and horn-root transitions receive a close inspection. | A stack of spheres with facial parts attached. Establish the forehead-to-nose profile, cheek planes and expression before ornament. |
| 2. Pierced ceramic vessel | An asymmetric, opaque ceramic body with a generous mouth, one flowing handle and a few deliberately placed openings. Use a restrained single-color finish. | Hollow the body, open the mouth, fuse the handle and pierce the outer wall into the interior. | A decorative solid with fake recesses, a plugged handle or an accidentally perforated/thin base. Inspect inside and around every opening. |
| 3. Wind-carved rock arch | One composed rock mass with unequal supports, a strong arch and a secondary eroded channel. Broad directional cuts establish the silhouette. | Two cavities meet, break through and expose a passage; later cuts refine the arch and overhang. | Another randomly perforated ball. Keep a clear load-bearing silhouette and a small number of directed channels. |
| 4. Twisted driftwood | A tapered trunk with two or three flowing branches, fused roots/knots, a hollow end and a few long grooves. Start with plain warm clay. | Curved strokes merge at the trunk; hollowing exposes the inside; long cuts follow the flow of the wood. | A bundle of straight capsules, lumpy joints or grooves that read as holes drilled across the grain. |

Use clay to judge every blockout. Final colors and roughness may distinguish
stone, ceramic and wood, but the shape must already work without surface
patterns. Procedural color/roughness variation is an optional final comparison,
not a required foundation task. No UV mapping or image textures are involved.

## Starting foundation and boundaries

The [project guide](../../projects/sdf/README.md#authored-scenes) owns current
behavior. At this planning checkpoint the authored path provides boxes,
spheres, capsules, capped cylinders, tori and capped cones; hard union and
subtraction; smooth union and subtraction; named local groups; and opaque
plain materials. The saved expression compiles to mirrored CPU/GPU programs.

The current bounds are 128 leaves, 256 instructions, 16 materials, tree depth
24 and a 32-entry evaluation stack. Stay within those bounds for each scene.
Use one subject per loaded field. Start with the axis-64 atlas; its allocated
field is 912,882,864 bytes. Retain the existing 2 GiB field-allocation ceiling.
Measure active-brick use and headroom for both final and intermediate states.

Keep shared atmosphere/sky, hard self-shadows and the existing HDR/post path.
Use no ground plane or terrain background. Metallic environment lighting,
glass, GI, a new lighting system and a general interactive sculpting editor
are separate projects. Learned generation, model training and imported/fitted
geometry remain in the [later content exploration](sdf-projectr-port-exploration.md).

The accepted robot, its generators/presets and published evidence remain
available. Shared changes must retain its behavior; the sculpture study gets
separate scene files and presets.

## Execution sequence

### A. Audition all four in the native renderer

Write deterministic subject generators using the existing Python helpers.
For each subject make two inexpensive proportion/silhouette variants and
capture matching front, profile and three-quarter clay views. Show at least
one important intermediate carve or blend as well as the final blockout.
Do this for all four before investing in final detailing or broad new tooling.

Select one variant per subject against its brief, recording the native images,
the choice and the main defect to fix. Establish construction ordering during
this pass: the demonstration must work as a sequence, not just as a final
expression. These are coarse candidates, not four fully polished models.

Checkpoint: all four have a recognizable native silhouette, a plausible
construction sequence and an explicit list of any missing capabilities.
Commit generators, explicit scenes, basic presets and the blockout decisions.

### B. Close the shared gaps exposed by those blockouts

Prefer small build-time helpers for symmetry, taper, stroke placement and
consistent naming. Approximate curved forms first with a bounded chain of
existing shapes and smooth unions. Count the compiled leaves, instructions,
tree depth and stack use before refining them.

Curved/tapered rounded strokes are the likely useful addition, especially for
driftwood and horns. Evaluate one representative curved join and one curved
cutter. If existing shapes give visible segment joints or spend too much of
the scene budget, add one narrowly defined stroke primitive. Specify its
parameters, conservative distance bound, degenerate cases, material rules and
history/preview behavior before implementation. Prove CPU/GPU agreement and
safe field classification before using it across the sculptures.

This is a decision gate, not a promise to implement a large shape catalog.
Hollowing can begin as outer-body minus inner-body CSG. Ellipsoids, profile
revolution, shell operators and deformations are candidates only if a native
blockout identifies a concrete deficiency. Nonuniform scaling, arbitrary
bends/twists and geometric noise need conservative bounds; they are not quick
substitutes for a supported distance operation. If the current limits cannot
support a brief, first simplify its composition or improve the representation
and measure the result; do not silently raise capacity.

Generalize the existing qualification and report workflow just enough to
accept subject metadata: scene/export names, inspection cameras, construction
chapters, important intermediate states and explicitly justified occluded
previews. Current robot-specific camera targets, joint checks, cutter
allowlists and file names must not become defaults for the new pieces. Keep
existing robot entry points compatible and reuse the native validation path.

Checkpoint: any new geometry contract passes focused checks, the shared
capture path works on a non-robot scene, and the accepted robot remains a
regression reference. Commit shared capability separately from model polish.

### C. Finish and qualify each subject in order

Finish the guardian, vessel, arch and driftwood one at a time. For each:

1. Refine the selected clay blockout's primary and secondary forms. Inspect
   front, both three-quarter sides, profile and rear before adding small cuts.
2. Make a second substantive modeling pass addressing the observed defects.
   Compare the two passes with matched native images. Spend further passes on
   a specific visible problem, rather than indefinitely increasing detail.
3. Add a restrained material treatment and retain a clay comparison. Check
   openings and joins with a useful inspection light as well as the hero light.
4. Compose the construction history and camera so the important blends,
   hollowing and breakthroughs are visible. Preview removals in red. Give
   meaningful steps time to read; accelerate minor repeated details.
5. Run native qualification, inspect the resulting images/video, fix defects,
   then save the evidence and commit the complete subject checkpoint.

Target roughly 30–60 seconds of construction followed by an 8–12-second orbit,
adjusted to the actual edit count. Use a steady construction camera; select it
to expose the important cuts. Additional inspection stills or short chapter
close-ups can cover an interior hidden from that view. Do not require every
small rear cut to face one camera or accept an invisible main breakthrough.

The current history reveals each primitive once within the final CSG tree;
it does not record moving an existing point or changing a brush parameter.
Use genuine CSG prefixes for the shaping story. Labels such as “carve cheek”
describe actual removals; do not imply a grab, relaxation or freehand smoothing
operation that the application does not perform. Any extension to that history
model needs its own contract and tests.

### D. Compare and close the study

Publish one gallery with the four hero images, clay/material comparisons,
construction videos, selectable committed states, inspection views and links
to explicit scenes, generators and evidence. Keep subject lighting/camera
choices recorded; use matched conditions when comparing revisions. Verify the
published report's images, video seeking, state navigation and narrow layout.

Write a short verdict per subject: visual strengths/defects, which SDF
operations carried the result, representation limits and measured costs.
Recommend the next content/tooling direction from these results. Record user
visual acceptance separately from the assistant's assessment and technical
qualification; the loop can deliver all four for review without claiming
the user has accepted all four.

## Acceptance and evidence

### Visual review

- Each subject reads at hero size and from its inspection views in plain clay.
  Merged forms have intentional transitions; there are no unintended floating
  pieces, plugged openings, seams, paper-thin walls or clipped silhouettes.
- Each construction sequence has a clear before/after moment demonstrating
  blending, hollowing or a change in connectivity. Repetition and detail alone
  are insufficient. A viewer can see the main change without lengthy captions.
- Inspect intent independently of renderer correctness. For the bust, check
  facial/horn placement and profile; for the vessel, interior/wall/base samples
  and open handle/piercings; for the arch, open passage and connected supports;
  for driftwood, joined branches and a hollow end. Combine saved-field probes
  and native views where appropriate. Do not infer these properties only from
  the same coordinates used to generate the shapes.

### Technical qualification

Reuse the existing authored-scene and history qualification. Check every
committed prefix, backward/reset/revisit behavior, CPU/GPU distance/material/
part agreement, sampled-field rays, export/reload identity, and static versus
final-replay image agreement. Screen the videos for exhaustion and inspect the
important steps. Preserve the current oracle tolerances and report finite-grid
ambiguities separately. Declare hidden minor previews individually, with a
reason; do not copy the robot's allowlist.

For a new primitive/operator, additionally cover its conservative bound,
degeneracies, grazing/inside rays, workspace limits and cutter preview bounds.
Run relevant legacy regressions when shared renderer/compiler/shader code
changes. Subject-specific art edits need focused checks and final qualification;
they do not require repeating an unrelated full renderer matrix each time.

Record scene/config hashes, source revision and dirty state, native binary and
shader hashes, hardware/settings, geometry build time, steady 1080p frame
median/p95 and field allocation/occupancy. Compare timing under matched
conditions with the accepted robot, and investigate significant regressions.
Construction rebuilds are synchronous: offline build playback does not establish
interactive brush latency. Keep construction, steady rendering and video
encoding costs separate.

### Delivery and completion

Each subject delivers a deterministic generator, an explicit versioned scene,
static/build presets, hero/clay/inspection captures, a construction video,
native qualification receipt and a concise evidence note. Generated media can
live in report/output storage; Git retains source, settings and evidence links.

Use reviewable commits for the blockout audition, each shared capability, each
subject and the final gallery/closeout. Commit as the loop progresses. Publishing
the study report is part of delivery; Git push follows the user's authorization
for that action. Do not mix unrelated repository changes into these commits.

A weak art pass triggers a concrete revision to proportion, flow or carving.
If it remains weak, retain the native candidate, state that outcome and continue
the remaining subjects. Technical failures remain open work: four scripts or
attractive stills alone do not complete the loop. Completion means all four
have reviewable native candidates with the required presentation and passing
technical evidence, the shared changes are reviewed, and the gallery and
per-subject verdicts are delivered. A rejected artistic experiment is a valid
study result, but must not be relabeled a visual success.

## Goal-loop handoff

The executed objective was:

> Deliver the four-subject SDF sculpture study in this plan: audition all four
> in native clay, add only the shared capabilities their forms need, finish
> the guardian bust, pierced vessel, carved arch and driftwood, qualify each
> scene and its genuine edit-by-edit presentation, publish the comparison
> gallery with measured evidence and honest verdicts, and commit reviewable
> milestones as work progresses. Preserve the accepted robot and the existing
> rendering boundaries.

Proceed through the planned checkpoints without requiring approval for every
modeling choice. User feedback steers the active study. A proposed change to
the four-subject scope, resource ceiling or excluded rendering/editor features
should be made explicit before treating it as part of this loop.

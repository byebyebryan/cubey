# SDF guardian and driftwood: form and detail pass

Date: 2026-10-22

Status: proposed refinement plan, pending implementation. The user ranks the
guardian strongest and driftwood weakest in the delivered
[four-subject study](sdf-sculpture-study-evidence.md). This pass develops those
two pieces; vessel and arch remain comparison subjects.

## Purpose and baseline

Test how far scripted SDF authoring can carry an existing sculpture through a
deliberate refinement pass. The guardian needs richer secondary forms and
carving while retaining its successful expression. Driftwood needs a stronger
wood-like structure as well as detail. Increasing the number of cuts is not
the success criterion.

Review baseline: source `0c57ca06`, the current explicit scenes, and the native
hero, profile and rear views in
`outputs/sdf/sculpture-{guardian,driftwood}-qualified/captures/`. The original
[gallery](http://starship:8787/reports/b155234e48461677cf2e6d7bd69569f4f6fc3a79c87ac94e430ad4af1980ed08/)
remains the published first-pass record. Inspection for this plan found:

| Subject | Preserve | Visible weaknesses to address |
| --- | --- | --- |
| Guardian | Broad face, heavy brow, empty sockets, swept horns and compact bust silhouette. | Socket rims and muzzle are simple; cheek cuts and forehead V look engraved rather than integrated into the forms. The rear skull/neck is largely blank. Horn segment bands are visible from the side/rear. |
| Driftwood | Flowing trunk, unequal forks, rear branch and hollowing demonstration. | Smooth pointed limbs resemble an antler or coral. The circular knot resembles a drilled recess; thin straight grooves do little to establish grain. Rear surfaces are largely blank and segmented taper is visible. |

Native correctness already passes for those saved scenes. That establishes a
technical baseline, not the quality of their modeling or any future revision.

## Guardian brief: a more resolved carved stone sentinel

Keep the current character and overall proportions. Concentrate detail around
the face, then connect it to the horns and cropped shoulders. Prefer three
coherent groups of changes to a surface covered in unrelated marks.

1. **Face and expression.** Shape a distinct upper socket overhang and softer
   lower rim; give the outer corners a deliberate turn. Refine the nose wings,
   cheek-to-muzzle transition, mouth corners and lower lip/chin relationship.
   Replace or reshape the existing isolated cheek lines and forehead V so the
   creases follow raised forms and reinforce the same stern expression. Keep
   the sockets empty; no inserted eye dots or modeled highlights.
2. **Horn, ear and skull transitions.** Develop the horn roots into the temple
   and brow, carve a shallow inner ear and carry one or two broad planes around
   the rear skull. Resolve sampled bands before judging any intentional horn
   carving. Try restrained longitudinal fluting near the horn roots, fading
   toward the tips; avoid a ladder of evenly spaced rings.
3. **Bust and stone character.** Give the neck/shoulder transition a readable
   front and rear structure while retaining a solid connection and the cropped
   base. Add only a few asymmetrical edge chips and one short crack at a chosen
   corner, horn root or base edge. Weathering should break an edge or catch a
   silhouette, rather than just draw lines across the face.

First compare two small face treatments in matched native clay: a restrained
rim/crease treatment and a more angular cheek/muzzle treatment. Choose one
before adding wear. Preserve broad quiet areas on the forehead and cheeks;
the full-size face must remain readable when the close-up is removed.

The construction's focal moments are the sockets changing expression, creases
being carved against the new facial forms, and a small edge chip exposing a
fresh plane. Extra accessories, teeth, armor and a larger pedestal are outside
this brief.

## Driftwood brief: a water-worn root fragment

Retain the idea of a twisting forked fragment, but make the grain and damaged
ends explain its shape. Aim for recognizable stylized wood in plain clay,
then use the warm material to support it.

1. **Composition and volume.** Audition two coarse variants: a compact twisted
   fork and a more leaning root fragment. Use fewer dominant, unequal limbs,
   with one moving clearly into depth. Replace several sharp horn-like tips
   with blunt, oblique broken ends. Flatten or scoop selected trunk surfaces
   so the cross-section is less uniformly round. Maintain connected branch
   roots and a clear silhouette from both three-quarter views and the profile.
2. **Knot, hollow and breakage.** Replace the round front dimple with an
   irregular elongated knot cavity following the trunk. Let a raised grain
   shoulder wrap around it. Develop one primary broken/hollow end with an
   uneven rim, a few substantial splinter ridges and a longitudinal split
   opening into that end. One smaller broken branch can repeat the idea at a
   different scale. Avoid needle splinters and paper-thin remaining walls.
3. **Grain flow.** Establish a few broad, shallow channels that turn with the
   trunk, diverge into branches and bend around the knot. Alternate raised
   ridges and recesses rather than incising parallel lines on a smooth tube.
   Add a limited number of finer tapered grooves after the broad flow reads.
   Continue some features around the side/rear and into exposed broken ends;
   do not leave every mark on the front projection.

The key construction moment is a solid broken stub becoming hollow and split,
followed by channels changing its surface relief. Show the relevant intact
state before each removal. A knot may remain a recess; do not label it a
through-hole unless the saved geometry and native views establish that.

Select the form variant before investing in fine grain. If it still reads as
an antler in clay, revise limb proportions, ends and cross-sections first.
Procedural color stripes or more small grooves cannot close that gate.

## Shared authoring work, only where the models need it

The current helper samples Catmull–Rom centerlines at five segments per span,
interpolates radii linearly and hard-unions rounded tapered segments. Its
surface grooves sample a front-facing XY path onto the first +Z surface.
These are explicit leaf chains, not a continuous spline sweep or an arbitrary
surface brush. See the [segment contract](sdf-rounded-stroke-contract.md).

Start with a small native comparison using one guardian horn and one driftwood
trunk/junction, including clay rear and grazing-light views. Determine whether
the bands come mainly from path/radius interpolation, segment joins or sampled
field resolution. Compare against direct-expression probes where useful.
Try continuous, overshoot-controlled radius interpolation and bounded adaptive
segment placement at authoring time. Keep the current version reproducible.
Do not assume that increasing samples, smoothing every joint or increasing
atlas resolution fixes the observed defect; the first study already found
that smoothing every short segment inflated the joins.

Prefer an authoring-only improvement that emits supported primitives. For
grain on the side/rear, allow a build-time local projection frame or explicit
3D cutter paths, with missed-surface and accidental back-wall checks. This
does not require a new interactive brush or runtime scene format.

An exact curve primitive is a separate decision within this pass, not a
prerequisite promised in advance. If the focused comparison cannot produce
acceptable curves within budget, document the defect and choose either a
simpler form or one narrowly specified new representation. A new runtime
operation requires a conservative distance/classification contract, mirrored
CPU/GPU evaluation, degeneracy tests, preview bounds and legacy qualification
before use. Do not let that become a general deformation or primitive project.

## Budgets and preserved scope

| Subject | Baseline leaves / instructions | Refinement approach |
| --- | ---: | --- |
| Guardian | 76 / 151 | Spend remaining capacity on facial and structural detail; replace weak existing cuts where appropriate. |
| Driftwood | 94 / 187 | Reallocate first: 60 leaves currently describe the main strokes and 30 describe thin surface grooves. Fewer, higher-impact forms/cuts should earn the detail budget. |

Keep the runtime bounds of 128 leaves, 256 instructions, 16 materials, depth
24 and stack 32. Aim to retain roughly 12 leaves of headroom rather than
treating 128 as a detail target. Keep the axis-64 field and existing allocation
ceiling; record peak occupancy over construction as well as the final object.
If a scene does not fit, simplify or improve its representation explicitly.

Use separate refinement scene/preset identities and reproducible generator
selection; generating them must not overwrite the first-pass scenes or
blockouts. Keep vessel, arch and the accepted robot unchanged as baselines.
Any shared helper change must retain their reproducibility or be opt-in.

Keep shared atmosphere, hard self-shadows, no ground plane and plain opaque
materials. Procedural color/roughness variation is optional after the clay
passes, with a plain-material comparison. UV/image textures, AI generation,
GI, glass, terrain and a sculpting editor remain deferred.

## Execution, evidence and commit checkpoints

1. **Preserve and diagnose.** Pin the old scenes/views, introduce separate
   refinement outputs, and run the focused curve comparison. Record the
   chosen representation and its costs. Commit any shared capability
   separately after its relevant checks.
2. **Resolve the guardian.** Compare face treatments, select and refine one,
   then add horn/bust detail and restrained wear. Keep matched baseline,
   secondary-form and final clay captures. Inspect all sides before committing
   the subject's scene, generator, presets and modeling-intent probes.
3. **Resolve the driftwood.** Compare the two form variants without fine grain,
   select one, then add knot/end structure and finally grain. Keep matched
   form/detail captures and record any rejected treatment. Commit the complete
   subject checkpoint.
4. **Qualify and present.** Qualify both changed scenes and their full
   construction histories. Publish a focused before/after report with a film
   per subject, selectable edits, red subtraction previews, inspection views
   and detail close-ups. Commit the evidence and final assessment.

The existing history reveals leaves inside the final CSG tree. Repositioning
an old feature during authoring does not become a recorded deformation.
Present baseline/revised models as matched comparisons; build the revised
model with its own truthful leaf-reveal sequence. Group film chapters around
meaningful shaping changes, accelerating repeated stroke segments. Ensure
close-ups show an operation in context as well as its resulting detail.

Use native hero/front/opposite/profile/rear/detail and clay views with matched
camera/light settings for before/after comparisons; add broken-end or facial
close-ups as needed. Separate technical passage, the assistant's visual
assessment and the user's artistic acceptance. The guardian must retain its
expression and silhouette; driftwood must gain convincing wood structure and
depth. A more elaborate result that reads worse is a rejected revision.

For the two final scenes, reuse committed-prefix/revisit/reset qualification,
CPU/GPU distance/material/part and field-ray checks, export/reload identity,
static/final-replay identity, cutter visibility and video exhaustion checks.
Check solid neck/branch connections, retained walls and intended cavity/split
openings independently of rendering correctness. Probe before/after states of
the main hollowing/splitting operations; test their visibility in native views.
Do not relax existing tolerances or copy hidden-preview exceptions blindly.

Record source/scene/config/binary/shader identities, geometry rebuild costs,
matched 1080p median/p95 frame times and final/peak active bricks. Investigate
significant regressions in performance or sampling quality. Art-only changes
need the changed-subject gates, not another full four-subject matrix at every
iteration. Shared runtime changes additionally need focused math/native checks
and relevant legacy regressions. Rebuild first if the native binary is absent
or no longer corresponds to the source being qualified.

Bound the art iteration: one audition, a selected form/detail pass and one
correction pass per subject. Deliver retained candidates and a candid verdict
if a visual weakness remains; passing tests cannot convert it into an artistic
success. Review the report in the browser before publishing and keep its
primary assets accessible within the publisher's 100 MiB limit. Commit as the
implementation progresses; Git push requires authorization for that action.

## Proposed goal-loop objective

> Refine the existing guardian and driftwood according to this plan: preserve
> their first-pass baselines, resolve the visible curve/authoring limitations
> only as needed, deepen the guardian's sculpted character, establish stronger
> wood forms and grain in driftwood, qualify both revised scenes and truthful
> edit histories, publish matched native before/after evidence, and commit
> reviewable milestones. Keep the other subjects, rendering scope and runtime
> ceilings intact, and report visual weaknesses separately from technical
> qualification.

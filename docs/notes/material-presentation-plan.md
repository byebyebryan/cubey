# Material workshop presentation pass

Date: 2026-10-23

Status: implemented and technically qualified; the new shot choices await user
visual review. The [presentation evidence](material-presentation-evidence.md)
records the selected stories, source identities, committed media and delivery.
The original proposal below retains the intended scope and completion boundary.
The current twelve-material appearance is accepted for the bounded library
milestone. Its [review fixes and qualification](material-library-evidence.md#review-and-refinement)
are committed and pushed through `b72c7c5c`. This plan develops the presentation
of stages A and B of the [three-demo roadmap](material-demo-roadmap.md).
The guardian construction showcase remains a separately selectable stage C.

## What this pass should achieve

Give each material demo a clear visual idea that reads at README size, plus a
good starting point for someone running it. The current reports are useful
comparison and qualification records: the latest contains 57 stills and three
short films. The next deliverable should curate that work into two independent
stories rather than repeat the evidence matrix.

| Demo | Main takeaway | Proposed opening |
| --- | --- | --- |
| PBR materials | Procedural structure and surface response make materials differ under the same light. | A small composed group with contrasting silhouettes and surfaces. |
| SDF materials | A saved solid material field also evaluates on surfaces exposed by construction and subtraction. | A readable wood block before a through-cut, followed by the finished bore and end grain. |

The final shape alone does not establish an exclusive SDF advantage. The SDF
story should show the operation and its material continuity. Shared recipes
connect the two demos; pixel-identical raster and SDF glass is not a claim.

## Proposed deliverables and boundary

- Two separate eight-second, silent clips and a poster for each, following the
  existing [showcase visual language](../showcase.md#suite-visual-language).
  Target native 1080p/60 masters and 720p/60 gallery copies using the existing
  capture/encoding path. Encoded cadence is separate from live performance.
- Project-owned presentation presets usable for native viewing and headless
  capture. Keep the existing workshop available for inspection and editing.
- One compact browser presentation with the two clips, a short explanation of
  each idea, at most twelve supporting stills, runnable recipes and links to
  the full technical reports. Keep the evidence tables behind those links.
- Project README posters, commands and concise explanations after shot review.
  Root-gallery inclusion is a later editorial decision; the existing five
  published highlights and their attachments remain the retained public set.

This is presentation work using the accepted library, shapes and shared
studio/atmosphere/cloud services. New materials, material realism, guardian
remodeling, terrain, ground planes and renderer features have separate scopes.
There is no material graph, general scene editor or shared cinematic framework
in this plan.

## PBR shot: contrasting surfaces under one light

Start the audition with four existing specimens: wood on the bored block,
rusted steel on a sphere, cloth on its oriented swatch and frosted glass on a
sphere. Compose their sizes, spacing and poses deliberately rather than treating
the twelve-sphere diagnostic grid as the finished shot. Keep each material in
the same object-anchored domain used by its workshop recipe.

If frost lacks a readable transmitted background in this composition, audition
one alternative using bronze or dry clay in that slot. Clear/frosted optical
comparisons remain available in the supporting presentation. Do not redesign
the environment or glass implementation to rescue the group shot.

Proposed eight-second beats:

| Time | Picture and motion |
| --- | --- |
| 0–1.5 s | The complete, initialized group holds; all four subjects are readable. |
| 1.5–6.5 s | One eased, partial camera arc reveals the wood's end grain, rust response and weave. Start near 20 degrees; choose the angle from the audition. |
| 6.5–8 s | Hold the strongest ending frame; no material switch or diagnostic ending. |

Audition the fixed studio and accepted frozen low-sun clouds, then choose one.
The selected shot holds its environment, exposure and material values. Avoid
simultaneously sweeping parameters, sun time and camera motion. The poster must
read at roughly 400–600 pixels wide without labels explaining every specimen.

The complete twelve-entry gallery remains the workshop's catalog view. The
curated arrangement is an explicit presentation preset, not a recipe or catalog
change. The viewer can open the existing controls to inspect the same materials;
a larger UI redesign is not needed to make the preset useful.

## SDF shot: reveal the inside of a solid material

Use the existing wood block and cylinder bore. Save an explicit two-operation
authored history: body, then subtraction. Give the opening enough obliqueness
to read the block and end grain; make the bore visibly penetrate the object.
Keep material coordinates fixed across both states and sample the same recipe
on the new interior.

| Time | Picture and motion |
| --- | --- |
| 0–1.5 s | Hold the already-present solid wood block. |
| 1.5–3.5 s | Introduce and commit the through-cut using the existing operation/history route; let the viewer see the cavity appear. |
| 3.5–6.5 s | A small camera reveal makes the hole wall, exit and end grain legible together. |
| 6.5–8 s | Hold the finished material view. |

The workshop's red subtractor preview can explain the operation in the
supporting educational view. Audition whether it helps the short film; a red
preview is a deliberate authoring visualization, not a change to the material.
Do not imply continuously moving geometry if the runtime only commits saved
states. Label an edited comparison or transition as such if used.

If the wood interior does not read at README size after one framing correction,
use the existing rust/fresh-steel CSG cut as the bounded alternative. That story
is explicit cut-material ownership, not automatic removal of rust or physical
weathering. Select one story for the final clip rather than packing both into
eight seconds.

The existing complete guardian construction and settled limestone/marble
results can be linked as the next authored-content chapter. A new guardian film,
robot revision or sculpture is not required to finish these two stories.

## Current capabilities and implementation gaps

| Area | Verified starting point | Small proposed addition |
| --- | --- | --- |
| Mesh presentation | The gallery renders all catalog entries using one selected specimen shape; focused sphere/block/swatch/bored-block views exist. | A fixed, project-owned layout choosing a recipe, existing mesh and transform per specimen. Reuse material handles and existing scene resources. |
| SDF construction | Authored history supports grouped operations, committed prefixes and subtractor previews. The material workshop's generated bored block currently has no saved history. | A small authored presentation scene with explicit body/cut history and the resolved shared material. Check that history and V3 material data survive round trips together. |
| Camera and timing | Mesh capture has a linear orbit; SDF has orbit/construction modes. The legacy SDF `presentation` timeline ends in diagnostic views. | Only the project-owned easing, holds and beauty-view ending required by the selected shots. Preserve legacy timeline behavior and host frame cadence. |
| Lighting | Both consumers use fixed studio or the accepted shared atmosphere/cloud environment. | Select and save coherent existing environment settings; no new background renderer. |
| Capture and delivery | Native headless video, poster extraction, report export and a provenance-aware showcase media package already exist. | Reuse their conventions for the selected shots and keep qualification inputs separate from the curated page. |

Keep scene layout, camera and timing in the demo/presentation configuration.
Keep them out of shared material recipes. A layout must retain the per-specimen
glass thickness estimate and the existing safe material-publication boundary.
Any new option is opt-in; existing gallery, workshop and capture commands retain
their behavior.

## Implementation sequence and acceptance

1. **Audition before extending controls.** Make at most two PBR compositions and
   two SDF framings, comparing studio/cloud lighting selectively. Use actual
   native specimens and present a small contact sheet. Choose the strongest
   readable direction before building the final sequence. If an image needs
   shader or modeling work to become convincing, select the simpler alternative.
2. **Implement the selected presets and motion.** Add only the layout, explicit
   history and bounded timing gaps identified above. Verify native viewing,
   reset/seek where applicable and reproducible headless state selection.
   Keep commits separated by PBR layout, SDF sequence and media packaging.
3. **Capture and review.** Produce two clips and posters with initialized
   openings, legible middle events and settled endings. Allow one focused
   correction per story. A clean partial arc or discrete construction reveal is
   sufficient; do not expand the goal into continuous sculpting or refraction.
4. **Package the selected presentation.** Retain exact commands, source/binary/
   shader/recipe/environment identities, native masters, derivative/poster
   hashes and meaningful limitations. Publish a new immutable report linked to
   the earlier qualification reports. Update project documentation, then review
   whether either clip merits a separate root-gallery candidate.

Technical checks are scoped to the changes: focused layout/timeline/history
contracts, relevant material round trips, required Vulkan validation on the
selected captures and native windowed inspection of the presentation presets.
Check that geometry operations do not re-anchor material coordinates, that
optical routing remains stable and that the held final frames stay in the
intended beauty view. Profile a new multi-specimen layout or changed update path
at the existing 1080p/1x settings; retain setup and field-build costs separately.
Do not rerun the full 57-image matrix without a changed renderer or a specific
regression concern.

Review film duration/frame count, posters, decoded playback and report links on
desktop/mobile and the delivery host. Keep source/native, HTTP delivery and
human visual acceptance distinct. Success means that the two stories are
understandable and attractive at their intended viewing size, not simply that
their captures pass validation.

## Retained evidence

- [Twelve-material library and review qualification](material-library-evidence.md).
- [Current library report](http://starship:8787/reports/97f536f0e315dd8cb3fa726846f782fbb832ea85ddefa3b7a35b956b33e9d63f/).
- [Original mesh/SDF workshop qualification](material-workshops-evidence.md).
- [Guardian closeout and presentation boundary](sdf-guardian-closeout.md).
- [Showcase media ownership and publication conventions](../media/showcase/README.md).

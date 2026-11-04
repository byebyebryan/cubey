# Sculptural SDF clay drafts

Date: 2026-11-03

Status: rejected by user review for undefined form. Retain the three native
clay candidates and their evidence as an unsuccessful audition. The subsequent
[reference research](sdf-sculptural-references-2026-11.md) proposes a more specific
form study; none of these drafts is selected for further detail.

[Compare the drafts](http://starship:8787/reports/18163705d047cc16342f756fe76b795f23f968e3b0dd69a4dc4ab05e61a0220e/).
Each has a carved hero, another angle and a separate uncarved modeling state.

The user reconsidered the observatory and the earlier dioramas because their
regular, mechanical construction distracted from the sculptural feeling they
wanted. Their original references were Dreams and Inigo Quilez's Shadertoy work;
Guardian felt closer in character, without settling its exact shape. This pass
returns to silhouette, gesture, fused masses and carved negative space. It
retains the graphics-demo scope rather than reopening a desktop editor or VR.

## Candidates and unresolved forms

- **Coiled sleeper:** a lowered head, weighted back, tucked belly, folded limbs
  and wrapping tail. The clearest immediate subject, although the front paw and
  lower jaw still merge visually. It needs a clearer contact and weight pass.
- **Forest spirit:** an uneven face emerging from a leaning trunk, with unequal
  boughs, roots and hollows. It is closest to Guardian's subject family. The
  face still reads as a mask and some cheek/branch joins remain abrupt.
- **Eroded refuge:** an outcrop with a carved chamber, entrance and upper
  lightwell. It retains an architectural possibility through negative space.
  It is the most abstract candidate; the approach and interior floor need a
  more legible relationship before it convincingly reads as an inhabited place.

One neutral clay material and common lighting hold attention on form. Broad
smooth unions and explicit tapered strokes replace repeated ornament. Smooth
subtractions establish the features and hollows. Organic form is an authored
choice that suits this demo; it is not exclusive to SDF rendering.

Round 1 exposed an empty arch beneath the creature, drilled-looking eye tools
on the spirit and a detached approach on the refuge. Round 2 settled the body,
reworked the expression and softened the approach. The selected final cohort
also projects both spirit eye tools onto its actual face. Earlier rounds are
retained with their own copied inputs and source snapshots.

This is a form audition. Choose the gesture and subject before further detail;
the native validation below is separate from the user's visual acceptance.

## Native evidence and reproduction

The selected cohort is `outputs/sdf/sculptural-drafts-20261103/final`: nine
1440×1080 Balanced/SS2 captures, all with required Vulkan validation. All were
inspected directly. Six scene/preset pairs regenerate byte for byte. Leaf names,
history coverage and Boolean polarity are checked by the authoring script.
Mass histories are prefixes of their respective carved histories, but these
are separate static states, not a qualified history animation or editing demo.

The runtime executable and all 220 SPV fingerprints match before and after
capture. No renderer, shader or existing scene was changed. The existing
roughly 913 MB field allocation is unchanged; this pass is not a performance
qualification and does not retain field dumps.

A separate Analytical refuge image retains similar broad crest/curve
transitions. That is a bounded modeling observation, not a renderer diagnosis.
Some stroke joins and shading transitions remain visible in the drafts.

```sh
rtk proxy python3 -B projects/sdf/tools/author_sculptural_drafts.py
rtk proxy build/release/projects/sdf/sdf --config projects/sdf/presets/sculptural-drafts-2026-11/coiled-sleeper-carved.json --render-mode balanced --supersample 2
rtk proxy python3 -B projects/sdf/tools/author_sculptural_drafts.py --capture-dir outputs/sdf/sculptural-drafts-fresh
rtk proxy python3 -B projects/sdf/tools/sculptural_drafts_report.py --output outputs/sdf/sculptural-drafts-report-fresh
```

Capture/export directories must be new. The report builder uses the selected
final cohort. Native captures retain exact scene/config copies, commands, logs,
image hashes, runtime/shader hashes and all six authoring dependencies.

The [summary](../evidence/sdf-sculptural-drafts-2026-11/summary.json) and
[retention manifest](../evidence/sdf-sculptural-drafts-2026-11/retention.json)
pin the raw evidence and runtime dependencies. The
[publication receipt](../evidence/sdf-sculptural-drafts-report-2026-11.json)
records HTTP delivery separately from browser acceptance. Browser navigation
failed with the existing automation adapter error, so controls and responsive
layout have static checks but no in-browser acceptance. Full-size image links
work independently of JavaScript. The original Guardian, dioramas, observatory
and their reports remain preserved studies of the previous direction.

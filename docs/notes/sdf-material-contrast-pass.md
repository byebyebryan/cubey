# SDF material contrast pass

Date: 2026-10-23

Status: implementation authorized; local commit milestones, no push.

The first material report establishes useful metal, glass, limestone and marble
looks. User review finds the uniform clay too glossy/smooth, ceramic too close
to metal, and the inherited gray-green reference insufficiently neutral. The
technical checks from that report do not establish those artistic goals.

This pass changes only the three study material records and the minimum tools
needed to inspect them. Preserve guardian geometry, accepted stone source
presets, metal/glass transport, lighting/exposure and the original glass gate.

1. Retain the first report and exact native inputs as the before reference.
   Inspect the old clay's diffuse/reflection contributions independently and
   verify roughness reaches shading. Keep these views available as diagnostics.
2. Make the study reference neutral gray and uniform, with no pattern. Try warm
   ivory ceramic and a softer glaze. For dry terracotta, use near-maximal
   roughness and filtered fine granular bump with little color variation.
   Reuse the established procedural sampler rather than revising marble.
3. Inspect matching sphere/guardian normal and close views. Permit one corrective
   look pass if necessary, then settle or report the remaining limitation.
4. Qualify the procedural CPU/GPU samples, serialization, uniform-material
   shortcut and appearance-only caches. Check all four accepted looks against
   the first report and retain the bounded glass verdict. Record fresh native
   captures, films and scoped profiles with their artifact identities.
5. Publish a paired before/after report with explicit diagnostic and visual
   acceptance boundaries. Verify delivery from Carbon and commit the closeout.

Acceptance is visual: clay should read dry and granular at the normal view and
remain legible up close, without sparkling noise or large color stains.
Ceramic should offer a pale glazed contrast to the warm metal. The neutral
reference remains normally lit; normal/roughness/component views provide
diagnostics separately. No new material families, image textures, coating
layers, glass bounces or general material editor are part of this pass.

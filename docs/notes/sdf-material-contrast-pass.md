# SDF material contrast pass

Date: 2026-10-23

Status: implemented, natively qualified and published. Local commit milestones;
no push.

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


## Diagnosis and selected result

The original clay has specular roughness 0.90 throughout its surface. Its
bright, smooth-looking areas remain in the diffuse-only image; reflection-only
radiance is comparatively weak. Reflection removal would not by itself fix the
perception. Component views use normal exposure/tone mapping, so displayed
images cannot be added numerically after display conversion.

Neutral gray now uses sRGB `777777`, uniform roughness 0.76 and no pattern.
Ceramic uses warm ivory `E3D7BE` at roughness 0.34, retaining a simple dielectric
glaze. Those first-trial settings were retained.

The first clay trial's height 0.0015 / grain 240 was too strong and pebbly.
The corrective phase first isolated finer, shallower grain at height 0.00035 /
grain 320; that alone retained the smooth diffuse appearance. The selected
result adds opt-in diffuse roughness 0.6, keeps the original `B87453` color
uniform, and uses specular roughness 1.0 with 0.04 procedural variation. Grain
uses the existing filtered analytic-noise/bump sampler under the reusable
`granular` type. There are no large color stains or geometry displacement.

The direct-light diffuse response follows
[Oren–Nayar](https://pbr-book.org/3ed-2018/Reflection_Models/Microfacet_Models).
`optics.diffuse_roughness` maps [0,1] to sigma [0,90] degrees. Zero retains
Lambert exactly. Environment diffuse uses only its conservative A coefficient
with the existing irradiance cube: there is no full angular rough-diffuse IBL
integration or energy-preserving multiple-scattering model. The field and
accepted material families retain their existing paths. Bright diffuse areas
under the fixed studio key remain; this is a bounded rough-surface study,
with user artistic acceptance still separate from technical qualification.

The previously reserved attenuation-W optical-table lane stores this scalar,
retaining the 32-byte layout, CPU/GPU blend/cut ownership and appearance-only
hash/cache behavior. Old zero defaults preserve exports. Glass rejects a
nonzero diffuse roughness. No general optics editor is added.

`--debug 10` and `11` expose opaque diffuse and reflection radiance respectively
under study lighting. They exclude emission, retain the presentation exposure
and tone map, and reject glass. The existing raw roughness/normal views remain
available. The normal combined expression is retained for unchanged materials.

## Evidence checkpoints

`f86f011e` records the bounded plan. `1812e7c8` commits the selected material
records, diagnostics, granular persistence and opt-in rough diffuse response.
The final native binary SHA-256 is
`96056124eefd0acd608dfbc9b41a11c762a0692b31dc9c76960706e95f647c23`.

Five focused CPU suites pass, including independent Lambert limits, reciprocity
and cosine-weighted hemispherical energy checks for rough diffuse reflection,
as well as granular and optical persistence/identity tests. The final native
foundation passes 34 runs with rough diffuse, optics, patterned material and
normal CPU/GPU checks at the existing bounds, mixed smooth ownership, explicit
metal cut materials, furnace controls and exact granular/glass export-reload
images. The new scalar and pattern edits preserve distance-brick caching.

The fresh comparison has 28 stills, twelve glass diagnostic views and seven
240-frame native films, all decoded and screened by the existing checker.
The four accepted material records and all geometry/camera inputs are unchanged.
All sixteen accepted material stills plus four original atmosphere/inspection
controls remain byte-identical to the first report's native images (20/20).
Glass retains the original sphere-qualified / guardian-experimental verdict and
1.1068% worst crossing-cap sample against the unchanged 1% gate.

Native window checks pass roughness editing on granular clay without a field
rebuild, and disabled glass controls with unchanged model pixels. Both retain
one geometry build. All 28 generated catalog scenes/presets reproduce exactly.
The focused CTest selection passes 16 checks and the scoped documentation suite
passes all 20 CLI recipes. Browser discovery still finds no connected browser;
HTML source/script checks and HTTP delivery are separate from browser review.


Fresh sequential 1920×1080 / 1× / hard-shadow profiles use 180 submitted frames,
30 warmup and 148 completed GPU samples per case, with no competing owned GPU
job. Clay medians are 2.170 ms on the larger-screen-coverage sphere and 1.429 ms
on the guardian; ceramic is 2.155 / 1.422 ms. These content costs sum shading
and shared post, excluding field construction, IBL setup, readback and encoding.
Setup/build measurements and raw pass CSVs remain separate in the report.
There is no material-performance ranking across the differently sized subjects.

Final artifacts are `outputs/sdf/material-contrast-{foundation,comparison,
regression,cut-history,profile,native-controls,glass-controls}/`. Native
component inspections retain `material-contrast-before/`, the first grain
trial `material-contrast-trial-a/`, the fine-grain isolation probe
`material-contrast-trial-b/` and the selected `material-contrast-final-look/`.
The before diagnostics have their own earlier binary/shader identity; final
qualification, gallery, profiles, controls and selected look share the final
identity. Three CLI admission checks reject glass component views, atmosphere
component views and unsupported debug values. No shader or material edits
follow the selected result.


## Publication

The [updated material report](http://starship:8787/reports/d7311c3fe7081ab555c02dad8742497d9cc57fb04424c652a350adf95641eb51/index.html)
contains all seven current materials, twelve changed-material before/after
comparisons, native films and the clay component diagnostics. All 544 files
(64,526,406 bytes) match their served HTTP bytes. Its source snapshot precedes
this publication receipt and final documentation links. The shared report
server remains running.

Static paths, asset hashes and JavaScript syntax pass. A DOM event harness
checks 28 gallery image cases, seven swatches, twelve glass diagnostic choices,
twelve paired before/after cases, 32 clay component choices and stale-video/
error handling. These are source/script checks, not browser layout or playback
acceptance. The harness's initial stale “Dry clay” label expectation was updated
to the actual selected-material label; no product change was needed.

Carbon resolves to its known Tailscale address, but both hostname and direct-IP
SSH delivery-check attempts time out. This does not establish whether Carbon's
browser can open the report; only Starship HTTP byte delivery is verified in
this pass. Browser discovery also remains empty. Neither limitation is reported
as a rendering or material failure.

No renderer/material edits follow the selected result. Final closeout adds
tools/documentation and completes the pattern-kind header comment. The existing
metal/glass/stone settings and the original glass admission threshold remain
settled. Artistic acceptance of the revised terracotta remains with the user.

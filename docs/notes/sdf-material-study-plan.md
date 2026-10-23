# SDF material set and sphere/guardian showcase

Date: 2026-10-22

Status: implementation loop authorized on 2026-10-22. The
[guardian closeout](sdf-guardian-closeout.md) remains settled. This is a separate
material experiment on fixed subjects, with local commit milestones and a
separately gated glass verdict.

## Objective and completion boundary

Build a small, reusable SDF material set and demonstrate it first on a simple
sphere, then on the unchanged guardian. Add dry clay, glazed ceramic and satin
bronze, followed by one bounded tinted-glass experiment. Retain plain,
limestone and the accepted subtle marble as references. Publish one report
with paired sphere/guardian views, guardian turntables, measurements and
explicit supported/experimental verdicts.

Success means recognizable, distinct materials that support the subject and
remain stable in motion. It does not require reproducing reference photographs
or building a general material graph. The opaque set must stand on its own;
glass may finish as sphere-only or deferred on the guardian if the bounded
transport path fails its quality or cost gate. That outcome must be reported,
rather than represented as qualified glass support.

## Two fixed subjects, one material catalog

- **Sphere:** one native SDF sphere, proposed radius 0.25 model units, used to
  inspect highlight shape, roughness, reflection, Fresnel edges and glass
  thickness. It is the first correctness and appearance checkpoint.
- **Guardian:** the settled mirrored sculpt, unchanged geometry identity
  `d5fc89f0abe076f0`, 1,011 primitives and the same 65 history operations. It
  tests concavities, thin horns, detailed normals and material readability on
  an actual piece.
- A deterministic authoring catalog supplies exactly the same material record
  to both subjects. There are no hidden guardian-only color, roughness, tint or
  exposure corrections. Camera framing may differ by subject but stays fixed
  across materials. Stone patterns retain their existing model-space values;
  do not retune them to improve the sphere.
- Emit separate material-study scenes/presets. Existing guardian scenes,
  presets, reports and the construction film remain untouched. No guardian
  remodeling or new construction-film production belongs to this loop.

| Material | Initial appearance target | Scope |
| --- | --- | --- |
| Plain reference | Existing plain guardian material | Reuse values unchanged |
| Limestone | Accepted warm, rough stone | Reuse values and pattern unchanged |
| Subtle marble | Accepted Balanced detail and 0.08 cream variation | Reuse unchanged; no new vein work |
| Dry terracotta clay | Earth color, roughness around 0.90 | Uniform opaque material first; no drying cracks or new noise algorithm |
| Glazed ceramic | Deep teal, roughness around 0.22 | Simple glossy dielectric; no crackle, layered glaze, porcelain scattering or clearcoat system |
| Satin bronze | Warm conductor color, roughness around 0.32 | Isotropic metal; no brushing, patina, scratches or oxidation |
| Tinted solid glass | Smooth interface, faint green absorption, IOR around 1.5 | Homogeneous bounded transmission, separately gated |

The four new looks are initial candidates, not adopted numeric art settings.
Choose one final look per material after at most two look-development passes.
The current `--clay` flag remains compatible and keeps its pixels: it is a gray
inspection override with roughness 0.36. Clarify its UI/report description as
neutral inspection; the new dry-clay preset is a real catalog entry.

## Rendering and lighting contract

The SDF path currently implements opaque dielectric shading: base color,
roughness, emission, direct specular and atmosphere diffuse irradiance. It
does not consume specular environment lighting or expose metal/transmission
parameters. Cubey already owns generated IBL resources and common PBR helpers;
reuse those, without importing the whole glTF renderer or adding a new engine
material graph.

Introduce one explicit opt-in material-study lighting route:

- Use one fixed existing generated environment with its bright key/fill lobes,
  diffuse response, roughness-prefiltered reflections and DFG lookup. Freeze
  orientation, intensity, direct-light settings, exposure and tone mapping
  before material look development. No HDR hunt, environment editor or
  per-material lighting rigs.
- The visible background, reflected radiance and glass environment lookup use
  the same environment and orientation. Follow the shared irradiance/DFG
  normalization contracts; do not add atmosphere SH and cube diffuse twice.
- The existing atmosphere route remains the default and retains its accepted
  appearance. New study resources are owned and created for the selected route,
  with valid per-frame bindings and normal teardown order. There is no dynamic
  probe/cloud integration project in this loop.
- New optical properties must not be silently ignored: a scene requiring them
  in the legacy route gets a descriptive unsupported-mode error. The new study
  presets explicitly select the study route.
- Preserve the geometric tracer, distance bricks, field resolution, depth,
  silhouettes and authored geometry/history. Reflect the environment only;
  recursive reflections of other SDF surfaces and scene objects are excluded.

## Material data, ownership and persistence

Recommended additive scene contract: an optional `optics` object per material,
backed by a small parallel CPU/GPU record. Keep the existing 32-byte
`SurfaceMaterial`, 80-byte pattern record and 64-byte geometry instruction
layouts. A proposed 32-byte optics record packs metallic/transmission/IOR/
attenuation distance and RGB attenuation color with one reserved lane.

- Existing base color, roughness, emission and procedural parameters retain
  their meanings. New fields are `metallic` in [0,1], `transmission` restricted
  to 0 or 1 in this experiment, `ior` in [1,2.5], linear `attenuation_color` in
  (0,1], and finite positive `attenuation_distance` in model/world units. Reject
  nonfinite and invalid values instead of silently clamping authored data.
- Defaults are metallic 0, transmission 0, IOR 1.5, white attenuation and
  distance 1. Omitted/default optics preserve legacy appearance fingerprints
  and exports; non-default optics enter the appearance hash with an explicit
  record tag/index. They never enter geometry identity or field construction.
- For opaque materials, base color is dielectric diffuse color or conductor
  reflectance according to metallic. Metallic/Fresnel energy treatment follows
  the existing shared PBR vocabulary. White-furnace and zero-metal controls
  check that reflected and diffuse energy are not both counted in full.
- Hard ownership, explicit cut materials, tie handling and existing smooth
  blend weights apply to the new opaque fields on both CPU and GPU. Preserve
  full versus one-material-shortcut equivalence. Save/reload/export and
  material-only editing must include optics without rebuilding the field.
- Glass is restricted to one homogeneous material over the whole scene, with
  metallic 0, transmission 1, neutral surface base, no emission and no
  procedural/bump pattern. Tint comes from absorption. Reject mixed-media or
  patterned-glass scenes in this loop. Existing multi-material opaque scenes
  remain supported.
- Glass uses fixed smooth-interface roughness 0.05, matching the renderer's
  existing numerical floor. Reject other glass roughness values in this slice
  and disable unsupported opaque controls when glass is selected. Rough/frosted
  transmission is not implemented by blurring reflection alone.
- Initially author optics through saved scenes/presets; the report supplies
  material/subject selection. Preserve the existing native controls, without
  building a native material-library browser or general optics editor. A graph,
  new texture slots and image/UV authoring are outside scope.

## Bounded glass contract

The existing tracer already supports rays starting inside solids. Qualify that
behavior for entry/exit refraction before relying on it for the guardian.

- Implement an iterative air/glass path with at most **four surface crossings
  including the primary hit**: up to two entry/exit pairs. Use oriented
  geometric interface normals, Snell refraction and Fresnel reflection. This
  permits one re-entry across separated parts without recursive transport.
- Use measured in-medium path length for Beer-Lambert absorption; do not use
  an artist-painted thickness map or a constant guardian thickness. Thin horns
  should remain less tinted than the thick head with the same material.
- Sample the common environment for reflected/refracted radiance. Total
  internal reflection terminates with a documented environment fallback;
  it does not start a recursive reflection tree. A crossing-budget fallback
  is deterministic and separately counted. Neither is a claim of exact
  multiple internal reflection.
- Glass has no diffuse body lobe. Do not apply the opaque self-shadow/AO factor
  to transmitted background radiance. Transmissive shadows, caustics,
  dispersion, scattering, rough transmission, nested media and opaque objects
  viewed through glass are outside this homogeneous environment-only study.
- Preserve the existing traversal-exhaustion failure signal. Crossing-cap/TIR
  fallbacks get separate diagnostics; they must not hide exhausted traces,
  repeated self-hits, nonfinite values or wrong inside/outside classification.

Glass first passes an analytic sphere, grazing/TIR cases and a small separated-
solid/cavity fixture. Set and document field-resolution-aware direction,
exit-point and thickness bounds on those fixtures before guardian look tuning;
do not relax existing distance/material validation bounds to accommodate it.

Proposed guardian admission limits: no nonfinite/exhausted rays or persistent
self-hit artifacts; at most 1% of visible glass pixels reach the crossing cap
in selected views/orbit samples; target at most 8 ms median GPU time at
1920×1080, supersample 1 on the current RTX 5070 Ti. TIR fallback incidence is
reported separately and visually reviewed. These are experiment gates, not
unmeasured performance promises. If the bounded implementation fails, retain
qualified sphere evidence and publish the guardian limitation. Do not respond
by adding more bounces, denoising or a separate optimization project.

## Six implementation checkpoints

1. **Baseline and fixtures.** Commit this plan when the loop starts. Capture
   source/config/scene/binary/shader identities for the settled plain,
   limestone, marble and neutral inspection controls before shader changes.
   Define the common sphere, fixed study environment and shared catalog. Freeze
   the capture cameras and analytic optical fixture acceptance bounds.
2. **Opaque material foundation.** Implement additive optics parsing, storage,
   hashing, serialization, CPU/GPU ownership and the opt-in IBL/metal shading
   route. Prove omitted/default optics neutrality, legacy pixel regression,
   shortcut equality, resource lifecycle and appearance-only caching. Use
   neutral/roughness/metallic sphere controls before sculpt captures. Commit.
3. **Opaque look audition.** Evaluate dry clay, teal ceramic and satin bronze
   on the sphere first, then the guardian with exactly the same parameters.
   Choose one look each within the iteration budget. Retain the three stone/
   plain references unchanged. Capture native hero/detail views and commit.
4. **Glass experiment.** Implement and qualify the capped homogeneous path on
   analytical and native fixtures. Review the tinted sphere, then the guardian
   silhouette, cavities and thin horns in motion. Apply the admission gate;
   commit either qualified support or a clear retained experiment boundary.
   Allow one initial implementation and one bounded correction pass. If the
   fixture gate still fails, leave glass disabled and report the spike; if only
   the guardian gate fails, retain qualified sphere support with that limit.
5. **Integration qualification.** Check persistence, mixed opaque ownership,
   explicit cut materials, normal separation, caches and legacy images; run
   focused native/CPU and build/documentation checks. Exercise a small cut
   history to show material continuity rather than replaying every unchanged
   guardian prefix. Profile each supported case without competing owned GPU
   work; separate shading, cold field and IBL setup costs. Commit fixes only
   when they address a measured correctness or acceptance failure.
6. **One final showcase.** Publish the sphere/guardian comparison and short
   guardian orbits with source receipts, supported/experimental labels, costs
   and limits. Inspect native stills/motion and available browser controls,
   hash-verify served files and check delivery from Carbon when available.
   Update the project guide/evidence and commit the completed milestone.

Implementation commits are local milestones; pushing is a separate requested
action. The user authorized execution after reviewing this plan.

## Report and acceptance matrix

- Seven catalog entries at most: three unchanged references and four new looks.
  Show sphere and guardian together for each; mark unsupported glass/subject
  combinations instead of substituting an opaque approximation.
- Two native stills per subject/material (normal and closer inspection), plus
  one eight-second guardian orbit per supported material. A single responsive
  report exposes material/subject selection, matched views, actual parameters,
  a short explanation of each response and the measured cost table.
- The sphere explains reflection/roughness and thickness; the guardian shows
  whether the same material supports a sculpt. Sphere rotation is not another
  animation project. No refreshed 65-step construction film is required.
- Native controls/round-trip checks, opaque CPU/GPU parity, glass analytic
  fixtures, zero field rebuilds for material edits, stable geometry identity
  and finite bounded frame/transport work are technical acceptance. Artistic
  recognizability and user preference remain separate decisions.
- Preserve existing error tolerances, exact accepted neutral/stone PNG
  regressions, immutable old reports and the distinction between local native,
  browser-interaction and HTTP delivery evidence. Package below the 100 MiB
  publisher limit without dropping primary comparison assets.

## Effort limits and deferred work

One fixed environment, one sphere, one unchanged guardian and one final look
per new material. Allow one initial audition and one corrective visual pass
per new look. Correctness fixes do not authorize additional art features or
new materials. After the second look pass, select the readable candidate or
record that the attempt is limited; do not create an open-ended third study.

The common opaque foundation and bounded glass experiment are the renderer
work budget. Further marble realism, clearcoat layering, anisotropy, scratches,
crackle, patina, bubbles, new noise families, subsurface scattering, GI, dynamic
probes, recursive optical transport, texture/UV workflows and a material graph
are deferred. Close the loop when the opaque set and bounded glass verdict are
delivered, including honest failures and source-scoped evidence.

References: [shared Cubey PBR/IBL contracts](../architecture/pbr-ibl.md),
[Filament material vocabulary](https://google.github.io/filament/main/materials.html),
[Khronos transmission](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission)
and [volume/absorption](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_volume).

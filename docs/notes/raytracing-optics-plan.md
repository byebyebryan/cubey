# Ray tracing reflections and refraction plan

Date: 2026-11-05. Status: proposed next implementation contract; planning only.
Source audit: `fdd13397`, branch `raytracing-first-pass-20261104`.

## Intended result

Extend the [first hardware pass](raytracing-first-pass-results.md) into an
optics demonstration lit by Cubey's shared atmosphere. The scene should show
the dragon reflected in a floor and a curved mirror, and neighboring geometry
seen through a clear glass prism. Camera motion should reveal parallax,
occlusion and objects outside the primary view.

Shared sun and sky lighting supplies the illumination. New rays supply scene
visibility through reflection and transmission. Diffuse interreflection is a
later milestone; it is not required for these effects.

Implement in three independently reviewable stages: shared clear-sky lighting,
sharp scene reflections, then clear-glass scene refraction. Keep the original
first-pass scenes and captures as regression controls. The original SDF/horse
work remains in its separate checkout.

## Scope

| Included | Contract |
| --- | --- |
| Shared environment | One `AtmosphereEnvironmentRuntime`; coherent sun/sky, diffuse irradiance and shared environment bindings |
| Opaque shading | Existing diffuse/GGX response with shared lighting; rough environment specular remains an IBL approximation |
| Scene reflection | Ideal sharp mirror surfaces, including a curved mirror; reflected hits use the same opaque shading as primary hits |
| Glass | One closed, homogeneous dielectric volume per scene, camera outside; reflection, refraction, Fresnel, total internal reflection and absorption |
| Visibility | Opaque RT shadows; bounded straight-through glass shadow transmittance, explicitly approximate |
| Delivery | Native controls, saved presets, generated fixtures, numerical diagnostics, matched captures and cost evidence |

Diffuse GI, caustics, area-light sampling, rough scene reflections, frosted
transmission, dispersion, nested/overlapping media, camera-inside-glass support,
animated geometry, texture mapping, temporal accumulation and denoising are
outside this pass. Retain centered single-sample primary rays and document
silhouette aliasing.

Full cloud composition and cloud shadows are also deferred. The shared cloud
runtime remains the future owner; do not introduce a project-local cloud field.
Clouds can later populate the same environment inputs, with separate visible
and shadow products. Neither full cloud integration nor a new HDR asset is a
prerequisite for the first scene reflection.

The dragon stays opaque. Its preserved source has holes and zero-area triangles;
this milestone does not repair it or declare it a solid dielectric. No change
to the pinned mesh or prepared GLB identity is required.

## Visual and scene contract

Use the existing dragon, a finite floor, a generated closed mirror sphere and
colored opaque markers. Start with a matte/clay dragon so the mirror contribution
is easy to inspect. Mirror reflectance is explicit linear RGB, bounded to [0,1].
Do not label the current golden dielectric response as a physical metal.

Reflection captures must include a curved mirror case and an off-screen marker.
A floor alone is insufficient evidence because a planar reflection pass can
reproduce that arrangement. Prove which scene object a secondary ray hits.

Use a generated closed prism for the initial glass result. A parallel slab is
the numerical control; a triangular prism makes directional bending visible.
Show a colored marker and part of the dragon behind the prism. A glass sphere
is an optional follow-up after mesh tessellation/normal convergence, not a
dependency of this milestone.

All generated fixtures run without downloaded data. Freeze their vertex/index
generation, winding, material IDs, transforms and bounds in tests. Closed glass
must have consistently outward-oriented faces, positive volume and two incident
faces per undirected edge. Use positive instance scales and disjoint objects.

Set one RT scene unit to one metre for the new atmosphere scenes. Translate the
scene into a local surface frame with the floor at world y=0; preserve the
source dragon's normalization. Derive atmosphere altitude, camera position,
ray lengths and attenuation distances from that same frame. Keep the camera
above ground in the surface regime. Existing legacy presets retain their
original coordinates and radian-valued sun controls.

Select final framing from native captures, then freeze three evidence views:
reflection overview, curved-mirror/off-screen proof and glass-prism detail.
Keep material, geometry, sun, exposure and camera unchanged across paired
transport ablations. Midday, low-sun and sunset captures exercise shared light
coherence without making a day/night or celestial-UI project.

## Source audit and ownership

The current RT shader shades in ray generation after a hit payload returns.
It has no material table, mirror/glass transport, environment sampler bindings
or shared atmosphere owner. `Instance.color.w` currently distinguishes the
floor; replace that convention with a checked project-local material ID.
Preserve explicit CPU/GPU layouts and add admission/range checks.

The existing [SDF environment adapter](../../projects/sdf/sdf_environment.cpp)
is the closest integration precedent. Reuse the engine environment owner,
render probe resources, shared PBR helpers and host controls where applicable.
Do not copy that renderer's bounded environment-only glass route as scene
refraction: the new rays must reach neighboring geometry after exiting glass.

| Owner | Required change |
| --- | --- |
| `projects/raytracing` | Static optical scenes, material table, path scheduling, typed config, RT descriptors, diagnostics and evidence |
| `AtmosphereEnvironmentRuntime` | Existing lighting/probe ownership; a narrow opt-in consumer-stage contract if required |
| `AtmosphereReflectionProbe` | Barriers covering RT sampling and later probe rewrites, with current consumer defaults retained |
| Shared material structures | Reuse base/roughness and `MaterialOptics` IOR/attenuation meanings; no full material-library port |
| Shared shader helpers | Reuse existing BRDF math; extract only a small interface/absorption helper if both consumers can retain qualified behavior |
| Existing host/HDR post | Frame slots, completion, UI, capture, exposure and display transform |

The current atmosphere probe hardcodes fragment-read stages on both sides of
updates. A matching image layout does not make those barriers sufficient for
an RT reader. Thread an explicit supported consumer-stage request through the
shared owner, retaining internal fragment reads and existing defaults. Do not
emit RT stages on a device that has not enabled RT.

Keep tracing outside the current render graph and import its HDR output with
the existing explicit handoff. This scope does not need a generic RT graph
domain or a cloud depth-composition adapter. Use the existing GPU owner queue;
no extra queue, async scheduler or scene backend abstraction is required.

## Shared lighting contract

Use one environment state to derive the visible sky, direct light direction,
color/intensity, diffuse irradiance and reflection environment. Convert legacy
radian controls explicitly at the adapter boundary; shared atmosphere angles
are in degrees. Avoid a second independently editable sun.

Sample clear-sky radiance for misses on primary, reflected and transmitted
paths. Evaluate opaque surfaces reached by any path with the same shared sun
and sky settings, including geometric shadow visibility. Shared irradiance is
environment illumination, not local diffuse bounce lighting.

Ideal mirrors and clear glass get their specular contribution from traced
paths or the explicit environment-only comparison route. Do not add a cached
IBL specular term on top of the same traced lobe. Ordinary rough opaque
materials may retain shared prefiltered environment specular; those surfaces
do not claim rough scene reflections.

Retain the shared probe cadence and previous/current binding semantics. Freeze
and settle the environment before reproducible stills. Exercise refresh during
interaction: old/current probe textures and descriptors must survive every
consuming slot before reuse or replacement. Lighting, IOR and material edits
must not rebuild BLAS/TLAS. Exposure is applied once through shared HDR post.
Camera/material/environment uploads and descriptor edits belong to a completed
frame slot or an explicitly completed immutable-generation swap.

## Bounded optical transport

Continue the full Vulkan KHR pipeline. Closest-hit returns geometry/material
observations; ray generation schedules radiance and shadow queries sequentially.
Keep pipeline recursion depth one. Optical path length is a separate budget
from shader recursion depth.

For mirrors, reflect the incident direction and trace the actual scene. At
the next opaque hit, evaluate direct/shared environment lighting; a miss
samples the shared sky. Fresnel/reflectance weights must not amplify energy.
Additional mirror encounters consume the declared path budget.

For glass, use a bounded deterministic Whitted-style split rather than random
branch selection. Sum reflected and transmitted contributions with Fresnel
weights from exact unpolarized dielectric Fresnel. The existing SDF
`opticalInterface` uses Schlick's approximation; keep that qualified behavior
unchanged rather than silently replacing it or treating it as the exact oracle.
At each boundary use the correct incident/transmitted IOR, account for
total internal reflection, and track whether the path is inside the admitted
glass instance. Apply Beer attenuation to the actual in-medium segment length.
Handle radiance/IOR scaling consistently; do not copy sampling probabilities
from a Monte Carlo reference into deterministic branch weights.

Preserve the outward geometric normal and front/back classification before
face-forwarding any shading normal. Geometric normals govern prism/slab optical
interfaces, medium transitions and side-aware offsets. Opaque shading and the
smooth mirror sphere may interpolate shading normals with a geometric-side
check. Test repeated crossings, grazing incidence and reflected rays originating
inside the volume. No nested medium stack is needed for one disjoint volume.

Initial proposed limits are eight specular/interface events per branch,
32 radiance queries and 128 total trace calls per pixel, with at most eight
interface queries for each shadow path. Use a statically bounded pending-path
store and deterministic processing order. Freeze the counters and termination
semantics before scored qualification. No silent environment fallback on
budget exhaustion: record the status and discarded throughput separately from
genuine misses. Start with a throughput cutoff of 1e-4 in the maximum RGB channel;
disable it for numerical control cases. All scene hit queries execute on the
GPU; CPU oracles are qualification tools only.

Direct-light shadows through glass require their own policy. The current
terminate-on-first-opaque-hit boolean route cannot serve every optical material.
Keep its fast path for opaque-only scenes. In glass scenes, integrate Fresnel
transmission and absorption along the straight shadow segment, stop at opaque
blockers, and report interface/budget failures. This is a projected visibility
approximation: it does not follow bent sun paths or generate caustics.

## Configuration and comparison controls

Retain the default legacy fixture and first-pass options. New presets explicitly
select atmosphere lighting and optical scenes; a missing dragon still fails
with preparation guidance. Scope the new typed settings to:

- Environment: `legacy` or `atmosphere`; one shared sun, fixed exposure and
  frozen environment state for captures.
- Scene: generated reflection/refraction fixtures and dragon-backed variants.
- Reflection source: `environment` or `scene`, preserving material reflectance.
- Transmission source: `environment` or `scene`; the former still traverses the
  glass interfaces but samples the environment after exit.
- Glass: IOR 1.0–2.5, linear RGB transmittance and positive reference distance
  in metres. Reject rough/nested dielectric settings rather than substituting
  the existing frosted-glass approximation.
- Diagnostics: secondary hit ID, path class, interface/event count and explicit
  termination reason, beside the existing geometry views. Path limits and
  detailed counters belong in advanced config/diagnostics, not the main panel.

Save/reload must reproduce the actual camera, scene/material settings, optical
route and environment state. GUI labels must distinguish sharp mirrors,
environment-only comparison and scene transport. Freeze the exact schema names
as part of the first implementation checkpoint; the categories above are the
behavior contract, not a claim that those options already parse.

## Implementation stages and acceptance

| Stage | Work | Gate |
| --- | --- | --- |
| P0 Reference and controls | Reproduce pinned Khronos reflection sample; build generated optical fixtures and independent CPU ray/interface oracles | Known-good donor capture, reference license/identity, fixed numerical tolerances and fixture identities |
| P1 Shared clear sky | Add the shared environment owner, lighting/samplers, RT consumer synchronization and deterministic settling | Primary/secondary environment lookup agreement, matching sun/shadow direction, clean live probe refresh and original-route regression |
| P2 Scene reflections | Material table, ideal mirrors, reflected scene hits, off-screen/curved cases and comparison controls | Reflected hit/depth/direction agreement; nearby occluders affect the reflection; camera parallax and secondary shadows work |
| P3 Clear glass | Closed prism, deterministic Fresnel branches, medium state, absorption, TIR and straight shadow transmittance | Slab/prism numerical cases and neighboring-scene refraction pass; budget/unsupported states are visible |
| P4 Native qualification | Controls/save/resize/lifecycle, frozen captures, timings and evidence index | Validation/regressions pass; costs and approximations are reviewable; physical and human visual acceptance retain separate receipts |

Do not advance a failed geometry or synchronization gate by tuning the hero
image. Reflection-only is a useful checkpoint if glass exposes a larger scope;
it is not completion of this proposed reflection-and-refraction pass.

## Numerical and visual checks

Keep the first-pass geometry tolerances unchanged. New optical control cases
use independent double precision with initial limits of 1e-5 for unit direction
vector error, 1e-4 metres for regular secondary hit position, 1e-5 absolute
Fresnel error and 1e-4 relative absorption error above the numerical floor.
Exclude declared edge/critical-angle guard bands from strict equality scoring;
test the classification on both sides of those bands separately.

Cover a mirror plane against an equivalent virtual camera, a curved mirror
against CPU triangle rays, reflected occluders and an off-screen colored target.
For glass, cover IOR=1, normal/grazing incidence, Snell bending, a parallel slab,
prism deviation, entry/exit classification, critical-angle TIR and thickness-
dependent attenuation. Check energy weights, transparent shadows, opaque
blockers and deliberately exhausted budgets. Compare against a 12-event,
64-radiance-query, 256-total-query control with threshold termination disabled.
Score linear HDR values away from declared silhouette/edge guard bands:
normalized mean absolute error at most 0.1% and 99th-percentile normalized
per-pixel error at most 1%, using max(1, reference luminance) as the normalizer.
Report maximum error, discarded throughput and terminated-pixel counts too.
These are proposed acceptance limits, fixed before scored qualification;
a miss requires an explicit budget/scope decision rather than hidden fallback.

Native review should show the same sun and sky in direct view and secondary
paths, plausible curved reflection/parallax, actual neighboring geometry through
glass, and stable contact/occlusion as the camera moves. Check face seams,
self-intersection, detached reflections, bright/dark glass boundary artifacts
and exposure consistency. Do not present missing caustics or diffuse bounce
as implementation bugs in this bounded model.

Exercise Vulkan/synchronization validation for multiple slots, repeated probe
refresh, material/IOR/sun edits, resize/recreate, teardown and partial failure
with retry. Record unchanged AS build counts under appearance-only edits.
Run focused RT/environment/probe checks and the dev suite. Compare representative
existing SDF, mesh-PBR and atmosphere output after shared synchronization/helper
changes; quantify any legacy RT image difference and explain its cause.

## Costs and closeout

Use the same GPU/driver identity workflow as the first pass. Measure the three
frozen optical views with environment-only and scene transport: three serial
repetitions, 1920x1080, 180 frames, 30 warmup and 150 completed GPU samples per
configuration. Keep shader diagnostics/counters out of timing runs.

Report tracing and scene/post costs with their original scope. Add a total-frame
GPU span that also encloses shared environment updates, plus a separate update
span. Measure frozen settled frames and a deterministic moving-sun refresh run
separately. The original 0.19–0.22 ms first-pass numbers do not predict these
costs. Keep the proposed 16.7 ms total-frame p95 screen on the RTX 5070 Ti;
a miss gets a bounded diagnosis, not a resolution drop or unbounded optimization.

Separate asset preparation, scene upload/AS build, atmosphere/probe startup,
pipeline setup and launch-to-completed-PNG from steady frame time. Record new
material/probe/uniform resources, requested resident/peak bytes and the same
allocation-scope limitations as the first pass.

Deliver frozen presets, native stills and a short deterministic camera sequence,
numerical and lifecycle receipts, source/binary/shader/asset identities, matched
costs, retention and the operational guide. Source push, merge and public-site
promotion remain separate. This planning change does not authorize runtime
implementation.

After this milestone, choose between rough/stochastic transport and diffuse GI
based on the visible result. The mesh/SDF comparison remains a dedicated follow-up
using the same scene, material and lighting contracts; clear-glass preparation
of the open dragon is not smuggled into that comparison.

## References

- [Reference-first workflow](../architecture/rendering-feature-workflow.md).
- Khronos [ray tracing reflection sample](https://docs.vulkan.org/samples/latest/samples/extensions/ray_tracing_reflection/README.html),
  source pinned to the existing donor revision
  [`56a72bd1c43268781a75d73a520e4458b6cac766`](https://github.com/KhronosGroup/Vulkan-Samples/tree/56a72bd1c43268781a75d73a520e4458b6cac766/samples/extensions/ray_tracing_reflection).
  Its ray-generation loop is a scheduling reference; its closest-hit shadow
  call uses recursion depth two. Cubey keeps those calls sequential in raygen.
  Adapted donor code must retain NVIDIA's Apache-2.0 notices.
- PBRT 4 [Dielectric BSDF](https://pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF)
  for interface/Fresnel/transmission conventions; it does not supply validation
  evidence for Cubey's bounded deterministic implementation.
- [Shared environment runtime](../../include/cubey/engine/atmosphere_environment_runtime.h),
  [probe synchronization](../../src/cubey/render/atmosphere_reflection_probe.cpp),
  [material optics meanings](../../include/cubey/procedural/surface_material.h)
  and [current SDF optical limits](../../projects/sdf/README.md#frozen-wood-study).

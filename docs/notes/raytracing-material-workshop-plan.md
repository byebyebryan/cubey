# RT material workshop implementation plan

Date: 2026-11-05. Status: active implementation contract; P0-P2 recorded, P3 in qualification.
Source audit: `66f6882f6a1813fd270806298dbc74ee8019b8d5` on
`raytracing-first-pass-20261104`. Current reflective-dragon runtime: `421708f9`.
Execution is tracked in the [workshop progress and frozen contracts](raytracing-material-workshop-progress.md).

## Intended result

Add a material workshop to the existing hardware RT project. Forward PBR,
SDF tracing and hardware RT consume the same twelve material recipes and produce
matched specimen captures under Cubey's shared atmosphere. The complete milestone
includes both clear and frosted glass. Bronze is the first vertical slice, not
an alternative definition of completion.

Use the existing groundless sphere layout and close specimen views. The showcase
needs no dragon, extra decorative objects, plinth, terrain or ground plane.
Cornell lighting, diffuse GI and further dragon art direction remain separate.

The RT workshop has two optical comparison routes. The environment route makes
material-response comparisons easier; the scene route adds reflected/refracted
neighboring specimens. Material agreement and transport differences receive
separate evidence. No pair of whole images is required to be pixel-identical.

## Scope and boundaries

| Included | Contract |
| --- | --- |
| Material content | The current catalog of twelve V1/V2 recipes; unchanged schema, source values and canonical fingerprints |
| Surface response | Shared color, metallic response, perceptual roughness, diffuse roughness, procedural patterns, relief normals, sheen and optical values |
| RT optics | Sampled rough GGX reflection, qualified clear interfaces and rough frosted transmission; bounded specular chains |
| Lighting | Existing shared sun, sky, SH diffuse illumination, probe resources and HDR post; frozen clear sky first |
| Specimens | Matched spheres; opaque block/swatch views for material-coordinate and normal checks; closed optical control shapes |
| Presentation | Three-renderer stills, all-twelve groundless gallery, orbit and matched clear-sky dawn/dusk captures |
| Operation | Native recipe selection/edit/reset/save/reload, bounded progressive sampling, diagnostics, lifecycle checks and scoped costs |

Diffuse interreflection, local area lights, Cornell reproduction, caustics,
dispersion, nested/overlapping media, camera-inside-glass, image textures,
arbitrary material graphs, geometry animation, denoising and history reprojection
are outside this milestone. Material emission can be visible directly or through
specular paths; it does not introduce diffuse emissive lighting.

Cloud composition is a later integration gate using the existing cloud owner
and its visible/reflection/shadow products. The first matched films are clear-sky
variants. Existing cloud films and accepted renderer defaults remain unchanged.

## Current integration gaps

| Current source | Consequence for this work |
| --- | --- |
| [Recipe contract](../architecture/material-recipes.md) and [shared evaluators](../../include/cubey/procedural/surface_material.h) already serve forward/SDF | Add an RT consumer, not another catalog or procedural material implementation |
| RT uses a [64-byte optical material](../../projects/raytracing/raytracing_scene.h) and separate global glass settings | Workshop records need resolved recipe data and per-material optics; preserve the legacy optical ABI |
| [RT opaque shading](../../projects/raytracing/shaders/trace.rgen) uses fixed dielectric F0 and cached environment specular | The current mirror/clay selector is not a bronze implementation; add shared metallic response and sampling |
| [Shared PBR helpers](../../shaders/cubey/pbr.glsl) use perceptual roughness squared, correlated Smith visibility and indirect energy compensation | Freeze these conventions before adapting a donor sampler; copying a sampler does not establish the same material |
| [RT construction](../../projects/raytracing/raytracing_renderer.cpp) admits one glass volume and uses scene-global IOR/attenuation | The twelve-material gallery needs the two disjoint glass bodies, with the correct material selected on every segment |
| Forward/SDF use `Ground` reflection probes; the current RT adapter uses `SkyOnlyNoGroundOcclusion` | Match the full workshop environment state, not only sun angles |
| Forward/SDF currently filter procedural detail using raster derivatives or a conservative SDF footprint | RT needs an explicit footprint carried through primary and secondary paths |
| Current RT has one centered primary ray and no persistent accumulation | Rough transport needs deterministic sampling, correct history invalidation and capture-time semantics |

## Material identity and ownership

`assets/materials/catalog.json` remains authoritative. The opaque set is plain,
limestone, marble, clay, ceramic, bronze, wood, rusted steel, kraft cardboard and
woven cloth. Glass and frosted glass complete the twelve.

`cubey::asset` retains parsing, validation, persistence and canonical content
fingerprints. `cubey::procedural` retains pure CPU/GLSL surface evaluation. The
RT project supplies its material record adapter, surface-hit inputs and optical
transport. Shared render math is reused where the model agrees. No recipe is
rewritten to make an RT image look closer to another renderer.

A resolved RT record carries the existing surface, pattern, optics, library and
affine-domain values, plus only the routing metadata needed by the RT consumer.
Use checked C++/GLSL layouts and stable material IDs. Keep the existing legacy
64-byte records and diagnostic outputs available for current RT scenes.

A capture records both the canonical source-recipe fingerprint and any effective
instance-domain transform. The existing [authoring script](../../projects/pbr_materials/tools/author_library_showcase.py)
rebases SDF domains to compensate for specimen translations. Such generated SDF
recipe bytes can differ from the canonical asset while evaluating the same local
material point. Compare canonical identity and effective coordinates separately.
Do not demand identical post-adaptation SDF recipe hashes or normalize pattern
coordinates by mesh bounds. Optical distances remain in geometry units.

The adapter preserves the existing metallic/roughness/Fresnel conventions. It
does not substitute a complex-IOR conductor model for the RGB metallic recipe.
V1 clear glass retains its documented sharp-interface approximation; its stored
roughness and fingerprint remain intact. The explicit V2 rough-glass flag enables
the rough optical route for frosted glass. Report both resolved content and the
selected transport response. Cloth sheen uses the existing shared Charlie
response and environment approximation; scene-to-scene sheen scattering is not
a new transport lobe in this pass.

## Specimen and environment contract

The first bronze comparison uses one radius-0.25 sphere at the origin, a
45-degree vertical camera and distance 1.05, with yaw 0 and pitch 0.15 radians.
Forward and RT use the same positions, connectivity and supplied smooth normals
from the existing sphere builder. Record any conversion for welded seams,
degenerate triangles or outward winding. SDF uses its corresponding analytic
specimen. Geometry approximation is identified independently from material error.

Start from the current 64-latitude/128-longitude mesh. Test a denser control before
qualifying curved glass. Geometric normals govern boundary classification and
ray offsets. Do not switch to interpolated glass-boundary normals just to hide
faceting. A needed refinement is an opt-in matched comparison mesh with recorded
costs; accepted forward defaults remain preserved. Opaque block/swatch adapters
can be narrowly shared after the sphere works; this is not a general mesh API
refactor. Optical controls use demonstrably closed, consistently wound bodies.

The final library layout follows the existing presentation: radius 0.075, four
columns by three rows, horizontal spacing 0.252, vertical spacing 0.195, camera
distance 0.855, yaw/pitch zero and 45-degree vertical FOV. Preserve catalog order.
Use the current authored local-domain mapping. No support geometry is added.

The first clear-atmosphere comparison freezes sun elevation 8 degrees, azimuth
-155 degrees, camera altitude 2 metres, exposure 0 EV, moon/night content disabled,
visible background `SkyOnlyNoGroundOcclusion`, reflection probe `Ground`, extent
128, five mips and update rate 4 Hz. These are new comparison presets; existing
presets are not rewritten. Freeze/settle the probe before scored stills.

A common manifest stores environment angles in degrees. Each consumer converts
to its native controls explicitly, including the legacy RT azimuth convention.
There is no second editable sun. Record the full environment/probe configuration,
intensity gain and HDR display transform. Sampling stays in linear HDR; exposure
and display conversion happen once after resolving samples. The workshop camera
uses the common local pose without the legacy RT floor clamp; the separate
2-metre atmosphere origin supplies the surface-frame altitude. Intensity gain is
one in each adapter; the prototype's intensity value 3 already maps to that gain.

The comparison gallery disables specimen shadows and uses environment-only
optical routes, matching the existing gallery's purpose. A separate scene-optics
preset enables local optical interaction and explicit visibility. The SDF gallery
continues to resolve to its environment after a glass exit; this plan does not
extend SDF inter-object transport or forward thickness estimation.

## Lighting and transport accounting

The environment comparison preserves the accepted shared PBR direct/SH,
prefiltered environment, DFG/energy-compensation and sheen conventions. Its glass
route traces the selected body's interfaces but resolves to the environment after
exit. Differences from forward thickness estimates and SDF central-path/filtered
transport are identified, not corrected with material-specific color changes.

The scene route replaces the GGX environment lobe with traced specular transport.
Each opaque hit contributes shared sun/sky diffuse illumination, the existing
sheen approximation and visible emission, then continues the sampled GGX lobe.
Glass applies its material's interface response and in-medium absorption. Diffuse
hits do not launch diffuse GI paths. Cached environment specular is not added
again on top of the traced lobe. Apply the agreed energy compensation once.

The raw shared sky includes a sun disk. In the sampled scene route its specular
energy must not also be added as an independent analytic sun-specular term.
Diffuse direct sun remains in the local diffuse response. P0 freezes sun-only,
sky-only and combined numerical controls before integration. For rough reflection,
a BSDF/sun-cone mixture can sample the complete incoming scene radiance with its
combined PDF, avoiding a second added specular-light estimate. Exact delta paths
retain their deterministic direction and sample the shared sky on a miss.
A donor's light-sampling scheme may replace that proposal only after the same
accounting checks pass; this does not authorize changing existing consumers.

Sample/evaluate/PDF must describe the same lobe and normal convention. Preserve
valid rejection/absorption events rather than renormalizing failed samples.
Fresnel branch probabilities, reflection/refraction Jacobians and radiance eta
weights belong in the optical estimator. Use geometric-side checks for perturbed
normals. Relief remains shading appearance, without actual displacement geometry.
Glass sun shadows retain bounded straight-path Fresnel/Beer transmittance; bent
sun paths and caustics are outside the scene-optics claim.

Keep default per-sample limits at 8 events, 32 radiance calls and 128 total calls;
retain the 12/64/256 no-cutoff control. Count any additional visibility/proposal
rays. Report terminated sample counts and discarded throughput per material.
Exhaustion discards the residual contribution with an explicit status, without
silently acquiring fallback sky. Any budget revision is an explicit gate decision.

## Filtering and multiple optical bodies

The surface evaluator consumes an object-space point and footprint. Reconstruct
primary differentials or a conservative ray cone from the camera and hit distance;
carry a documented footprint through subsequent reflection/refraction and angular
spread. Transform the domain and height gradient using the existing chain rule.
The footprint cannot remain zero or be guessed independently for each material.
Freeze the actual propagation formula and control cases in P0.

Test object scale, nonidentity domains, camera distance, grazing views and rough
secondary paths. Forward/RT conversions must reconstruct equivalent local points.
Pattern filtering can differ in conservatism, but visible frequency/normal changes
need component evidence. More samples do not replace a missing filtering policy.

Support sequential traversal of separate, closed homogeneous dielectric bodies.
A segment's medium ID selects that body's recipe IOR, attenuation and distance.
Exiting one body and later entering the other is valid. Nested/intersecting bodies
and a camera inside a dielectric remain rejected before rendering. Admission uses
conservative disjoint bounds; geometric winding, positive volume and boundary
incidence are checked during preparation. Glass tables are per material, never
copied from the currently selected UI item into all specimens.

Appearance edits such as roughness, IOR and attenuation retain geometry/BLAS.
Catalog IDs and optical routing stay stable during a published library snapshot.
Cross-family selection uses a prepared route or an explicit completed instance
update; it must not silently invalidate cull masks or rebuild BLAS. Reload follows
the current workshop policy: validate a complete candidate, publish atomically,
and require restart for roster/route changes. Failed reload preserves the last
working snapshot. P0 records selection/TLAS behavior before promising no AS work.

## Sampling history and native operation

Keep the existing deterministic RT scenes available. Workshop sampling is opt-in
and uses reproducible pixel/sample seeds and jittered primary rays. Allocate one
bounded FP32 accumulation history and sample-count state per view generation;
material and camera uploads remain owned by retired frame slots. Queue ordering
and image barriers protect history reuse. Resize/recreate retires the complete
old generation before destruction. No extra queue or asynchronous scheduler is
needed.

Camera, geometry, resolved material, domain/pattern toggles, lighting/probe
generation, optical route and transport settings change the history identity.
Random jitter/sample index do not. Start a new epoch on a relevant edit and never
mix different states. Exposure/display-only edits re-resolve existing linear HDR
history without discarding samples. A layout/selection change is a scene edit.
Keep the native sample count and accumulation/reset state visible.

Presentation time and sampling time are separate. A still completes its requested
sample target before readback. Each video output frame freezes camera, sun and
weather, accumulates that state, resolves/captures once, then advances authored
time. Sample iterations do not advance the sun or orbit. Interactive motion resets
history instead of reprojecting it; any resulting noise is visible. Moving-sun
films are bounded offline captures and do not establish real-time convergence.

Provide catalog/family selection, isolated specimen and gallery views, pattern/
bump switches, material edits/reset and recipe import/export using existing typed
configuration and native host behavior. Preserve the recipe controls' meanings.
Freeze exact workshop flags, sample-target/output-frame semantics and schema
round trips in P0; this plan does not claim those options already parse.

## Reference selection

Use the [reference-first workflow](../architecture/rendering-feature-workflow.md).
The existing pinned Khronos path remains the AS/pipeline/owner baseline. The new
work is a material/estimator port into that working path, not a new renderer
framework or glTF viewer.

Reference snapshots inspected during planning:

| Reference | Revision | Purpose |
| --- | --- | --- |
| [NVIDIA vk glTF renderer](https://github.com/nvpro-samples/vk_gltf_renderer/tree/10e0b5d7b599c3c448aaee4f517fe417c642cad8) | `10e0b5d7b599c3c448aaee4f517fe417c642cad8` | Preferred runnable rough-PBR visual reference |
| [NVIDIA shader functions](https://github.com/nvpro-samples/nvpro_core2/blob/8746c6d36bebe652516b9a1bf3cce46422413bf7/nvshaders/bsdf_functions.h.slang) | `8746c6d36bebe652516b9a1bf3cce46422413bf7` | GGX reflection/transmission sampling and evaluation source to audit |
| [Archived NVIDIA vk raytrace](https://github.com/nvpro-samples/vk_raytrace/tree/1f5d00d5bbda01e65dac95202f59b2db9fb1bd2d) | `1f5d00d5bbda01e65dac95202f59b2db9fb1bd2d` | Compatible historical visual-reference candidate if the modern sample requires unrelated toolchain work |
| [PBRT source](https://github.com/mmp/pbrt-v4/tree/b0bdf461ba5dd6504b70ebf0932acc0fe7aa744f) | `b0bdf461ba5dd6504b70ebf0932acc0fe7aa744f` | Independent mathematical/reference-renderer checks |

These are source snapshots, not a qualified build or a paired dependency set.
P0 pins the chosen runnable sample and its actual dependencies, verifies its
license/notices and captures a native rough-metal/dielectric reference before
Cubey adaptation. NVIDIA shader headers and PBRT source identify Apache-2.0;
retain the exact notices for any adapted files. The current Slang sample is not
a mandate to migrate Cubey's GLSL/compiler/framework. An incompatible reference
requires a smaller compatible choice, not an unrelated host upgrade.

Audit formulas against Cubey conventions rather than importing the donor's full
layer stack. PBRT's [conductor](https://www.pbr-book.org/4ed/Reflection_Models/Conductor_BRDF),
[rough dielectric](https://www.pbr-book.org/4ed/Reflection_Models/Rough_Dielectric_BSDF)
and [BSDF interface](https://www.pbr-book.org/4ed/Reflection_Models/BSDF_Representation)
provide sampling/evaluation/PDF references. CPU quadrature/oracles must not simply
call the same ported sampler used by the GPU and label that independent evidence.

## Implementation stages

| Stage | Deliverable | Exit gate |
| --- | --- | --- |
| P0 Reference and contracts | Runnable donor capture; formula/convention table; environment, footprint, history and route rules; native schema | Sun/sky accounting, correlated-Smith/GGX/compensation choice and independent numerical cases frozen before adaptation |
| P1 Bronze vertical slice | Shared bronze recipe on one sphere in all three consumers; cached and sampled RT routes; sampling history and live edits | Content/component checks, roughness sweep, furnace and native lifecycle pass; matched three-way still and scoped costs |
| P2 Opaque library | All ten opaque recipes; domain/footprint, procedural normal and sheen support; opaque block/swatch inspections | Every opaque recipe passes the component/minification/edit matrix, without special-case appearance tuning |
| P3 Dielectrics | Qualified clear and frosted responses, per-material optics and the two disjoint glass specimens | Interface/absorption/TIR/rough-limit controls and independent-body traversal pass; all twelve recipes supported |
| P4 Matched workshop | Groundless twelve-sphere gallery, isolated views, three-way clear stills and orbit/dawn/dusk media, operational guide | All acceptance groups, current identity/cost receipts, preservation and native review complete |

A failed material, estimator or synchronization gate is fixed before adding more
recipes or tuning a showcase image. A checkpoint can produce review artifacts;
it is not completion of the twelve-material milestone. A separate implementation
request may bound execution to a checkpoint. Execution status and measured refinements are recorded in the linked progress document.

## Acceptance and measurements

Freeze scored inputs and tolerances before the relevant implementation stage.
The following initial limits are part of the proposed contract; a miss requires
an explicit accuracy/budget/scope decision rather than relaxed hidden thresholds.

| Check | Initial gate |
| --- | --- |
| Content identity | Canonical recipe serialization/fingerprint round trips unchanged; effective SDF/mesh domains recorded separately |
| Surface sampling | CPU/GPU component differences at fixed material points no more than 5e-4 absolute; perturbed-normal error no more than 0.5 degrees on regular inputs |
| BSDF arithmetic | Direction error no more than 1e-5; evaluation/PDF difference no more than 1e-6 absolute plus 1e-4 relative away from declared singular bands |
| Optical controls | Retain existing 1e-5 direction/Fresnel, 1e-4 m position and 1e-4 relative absorption limits on qualified regular cases |
| Energy and sampling | Finite/nonnegative results; independent normalization/rejection-mass checks; white-furnace energy no more than 1.02 and agreement with the chosen model's independent integral within 0.02 |
| Still convergence | 256 samples versus an independently seeded 4096-sample control, per material; normalized mean absolute error no more than 0.5%, per-pixel maximum-channel p95 no more than 2% |
| Native lifetime | Clean Vulkan/sync validation across edits, history reset, probe refresh, resize, teardown and partial failure/retry; no residency growth from appearance editing |

Validate the high-sample reference's stability against 8192 samples on the hardest
bronze/frosted cases. Use linear HDR normalized by `max(1, reference luminance)`.
Convergence scoring uses each specimen's primary-hit region, with a declared
one-pixel silhouette guard; background cannot dilute a material's result. Report
all-pixel results, maximum error, guarded pixels, status counts and discarded
throughput as well. Two fixed independent seeds guard against a favorable sample
sequence. The reference and default must obey the same material/lighting model.

The matrix covers metallic 0/partial/1, roughness and grazing views, nonidentity
domains, distance/minification, pattern/bump toggles, surface-scale invariance,
normal-side validity, IOR=1, normal/grazing transmission, TIR, thickness-dependent
absorption, sequential glass bodies and deliberate budget exhaustion. Test
isolated sky/specular/diffuse/sheen components to detect duplicate contributions.
Glass mesh-density controls distinguish geometry error from BSDF error.

Run focused recipe/domain/BSDF tests, legacy RT geometry/optics and retry checks,
shared environment/probe checks and the full dev suite for final integration.
Keep the known broader sync baseline failures separately scoped; a previous
clean focused run does not qualify new workshop code. Compare accepted forward,
SDF, furnace and RT controls against preserved bytes or fixed numerical limits.
Do not rewrite their goldens to accept a material port.

Measure three serial 1920×1080 repetitions per isolated material and final gallery,
180 iterations, 30 warmup and 150 completed samples per GPU pass. Use one new
sample per measured iteration and a target high enough to avoid an idle capped
history. Disable validation/raw readback for timing and overlap no owned GPU work.
Measure scene optics and environment comparison separately, with frozen and
refreshing sky separated. Record tracing, environment update, resolve/post and
whole project GPU spans, plus AS/startup, requested/allocated memory and resident
sample-history cost. Record source, binary/shader, recipes, environment, seed,
samples, driver and actual build flags. The old 0.6 ms demo cost is not a forecast.

Initial RTX 5070 Ti screens are total-frame GPU p95 no more than 16.7 ms per
one-sample iteration and no more than 2 seconds of project GPU work for a settled
256-sample gallery still. Report launch/setup/readback separately. Forward/SDF
single-frame cost and RT time to a stated sample target are different scopes;
the comparison must show both. A miss triggers bounded diagnosis or an explicit
quality/budget revision, not silent resolution or sample-count reduction.

## Delivery and ownership

| Owner | Bounded change |
| --- | --- |
| RT project | Workshop config/scene/material adapter, per-material optical state, sampled shader path, accumulation and diagnostic/native controls |
| Shared asset/procedural layer | Reuse current recipe/evaluator contracts; change only for a demonstrated missing shared value or reusable pure function |
| Shared render math | Small independently checked sampling/PDF helpers where conventions agree; preserve existing consumer behavior |
| Shared environment owner | Existing sun/sky, probe, consumer-stage and completion contracts; no new sky model or cloud field |
| Specimen/presentation authoring | Reuse actual sphere data and layout; factor private opaque block/swatch builders only for real shared use; emit opt-in clear RT/forward/SDF presets and a common manifest |
| Host and tools | Existing frame/capture/HDR ownership; bounded sample-target driver if required; native save/reload and comparison report |

Keep recipe UI semantics aligned; a small project binding is acceptable. Do not
create an editor framework, interchangeable-renderer framework, generic material
graph or new permanent comparison project to host this consumer.

Deliver all twelve supported recipes, native isolated/gallery controls, matched
clear captures and films, reference/license provenance, component/transport and
lifecycle receipts, completed-sample costs, retention and the operational guide.
Preserve the original SDF/horse worktree, previous RT milestones and existing
public material media. Source push, merge and public-site promotion remain
separate from implementation and internal evidence-report publication.

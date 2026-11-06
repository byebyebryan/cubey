# RT material workshop implementation progress

Date: 2026-11-05. Branch: `raytracing-first-pass-20261104`.
The [implementation plan](raytracing-material-workshop-plan.md) defines completion.
All twelve materials remain required. This record tracks the gates and frozen
implementation choices; an isolated math probe does not constitute a workshop.

## Gate ledger

| Gate | State |
| --- | --- |
| P0 runnable reference | Native archived NVIDIA donor built and externally captured; source/dependencies pinned below |
| P0 sampling arithmetic and estimator controls | Isolated native GGX probe passes independent direction, evaluation/PDF, integral/rejection and analytic sun/sky mixture controls |
| P0 integration contracts | Choices below frozen; opt-in native configuration admission/round trips pass; record layout and history implementation checks belong to the integration |
| P1 bronze | Passed at 74f42e3c; matched three-way stills, two-seed convergence, reference stability, furnace/component, native lifecycle and serial 1080p costs recorded |
| P2 ten opaque recipes | Passed at afd050c5; all ten materials, four routes, content/components/filtering, two-seed convergence and stability, native operations and serial costs recorded |
| P3 two dielectric recipes | Pending |
| P4 full matched workshop and delivery | Pending |

P0 source checkpoint: `0cdbb8ab`. The [compact P0 receipt](../evidence/raytracing-material-workshop-2026-11/p0.json)
pins the reference, native executable/shader, numerical results and capture hashes.
Focused RT checks pass 10/10 and documentation checks pass 7/7. The original
eleven protected SDF/horse files retain their saved hashes. No full workshop
qualification or public-site promotion has occurred.

## Reproduced reference and provenance

The preferred modern sample uses Slang. The compatible archived visual candidate
builds without changing Cubey's GLSL/compiler/host, so it is the selected runnable
reference for this pass:

| Input | Pin |
| --- | --- |
| `nvpro-samples/vk_raytrace` | `1f5d00d5bbda01e65dac95202f59b2db9fb1bd2d` |
| `nvpro-samples/nvpro_core` | `3713d803fc8f48351d0e713456923233d1232afc` |
| Khronos Vulkan headers, SDK 1.4.313.0 | `409c16be502e39fe70dd6fe2d9ad4842ef2c9a53` |
| Official enum-string helper | `KhronosGroup/Vulkan-Utility-Libraries`, tag `vulkan-sdk-1.4.313.0`, `include/vulkan/vk_enum_string_helper.h` |
| Sampling source, `nvpro_core2` | `8746c6d36bebe652516b9a1bf3cce46422413bf7`, `nvshaders/pbr_ggx_microfacet.h.slang` |

The old loader generator fails against the current system registry. Configure
the donor with `Vulkan_generate_extensions=OFF` and the pinned headers, and put
the official enum helper in `nvpro_core/vulkan/`. The NVIDIA source/shaders are
unchanged. GNU 16.2.1 builds the donor in Release, `-O3 -DNDEBUG`, while Cubey
retains its existing Clang dev build. No system SDK replacement was performed.

Ignored reference source, logs, executable, generated control glTF and captures
are retained under `outputs/raytracing/material-workshop/reference`. Its native
receipt records source/dependency/binary/control/capture identity. The controls
are three closed, groundless spheres: rough bronze, an opaque dielectric and
rough glass. The sample runs its default Disney model and studio HDR. This
qualifies a runnable visual baseline, not equality with Cubey's material model
or a converged Cubey image. Native exit is zero and the capture log contains no
validation messages; validation layers were not enabled in that donor run.

Reproduction after preparing the pinned sources:

```sh
cmake -S vk_raytrace -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DVulkan_generate_extensions=OFF \
  -DVulkan_INCLUDE_DIR="$PWD/vulkan-headers/include"
cmake --build build -j 6
```

Run those commands inside the reference directory. Capture from the Cubey root
on an explicitly owned virtual display:

```sh
env CUBEY_RT_PRIVATE_XVFB=1 xvfb-run -a -s '-screen 0 1280x800x24' \
  python3 projects/raytracing/tools/capture_material_reference.py \
  --reference outputs/raytracing/material-workshop/reference \
  --output outputs/raytracing/material-workshop/reference/capture-rerun
```

NVIDIA sampling source is Apache-2.0; attribution is retained in the project
NOTICE and adapted shader. The exact third-party license remains in
`projects/raytracing/third_party/LICENSE.Apache-2.0`. Downloaded donor HDR assets
stay in internal ignored evidence; no public redistribution is implied.

## BSDF and estimator conventions

| Quantity | Frozen choice |
| --- | --- |
| Direction/frame | Both scattering directions point away from the surface; local +Z is the face-forward shading normal |
| Roughness | Existing perceptual value `r`; isotropic GGX alpha is `r*r` |
| Opaque Fresnel | Existing RGB metallic interpolation and Schlick; no complex conductor IOR substitution |
| Evaluation | Cubey's correlated Smith and GGX D denominator floor; an algebraically stable D expression avoids peak cancellation |
| Sampling | NVIDIA visible GGX normal mapping; analytic normalized NDF/G1 density; no BRDF denominator floor in the PDF |
| Rejected samples | Retain null-event probability; never retry or normalize to the surviving hemisphere |
| Compensation | Existing DFG white-conductor compensation once per evaluated GGX continuation; no cached specular added to the scene route |
| Glass | Exact V1 delta interfaces; V2 GGX microfacet reflection/transmission, Fresnel branch probabilities and radiance eta/Jacobians, qualified before P3 admission |
| Normals | Smooth supplied/relief normals for opaque BRDFs; geometric normals for glass classification, offsets and side checks |

The donor's full uncorrelated Smith layer stack and its older glTF transmission
shortcut are not copied. The separate double-precision integration oracle uses
D/Lambda equations and solid-angle quadrature rather than the ported sampler.
Normal-incidence rejection mass also has the independent analytic control
`1/(1+alpha*alpha)`. Two quadrature resolutions check the grazing controls.

The initial native probe found float cancellation at the sampling disk rim.
The port retains the donor's mapping but evaluates the residual as
`1-u + u*sin(phi)^2*dot(stretchedView.xy,stretchedView.xy)` on the compressed
semidisk, and `1-u` on the other half. This avoids cancellation before the
small-alpha unstretch. The limits were not changed. BRDF D uses the equivalent
`hx*hx + hy*hy + alpha*alpha*hz*hz` denominator, retaining the shared floor.
Existing forward/SDF helpers and their output remain unchanged.

The qualified probe scores 131,072 inputs. Maximum direction error is
`7.0948212e-6` against the unchanged `1e-5` limit; the largest evaluation/PDF
error uses 0.009532 of its allowed absolute-plus-relative tolerance. Forty-three
independent integration cases have maximum furnace-integral error 0.001577 and
rejection-mass difference 0.002997. These are isolated-probe results; compensation,
surface/domain evaluation and full shared-atmosphere transport remain unqualified.

For scene transport use a 50/50 mixture of the BSDF proposal and a sun-centered
uniform cone. Evaluate the complete incoming scene radiance with the combined
PDF, including both proposal densities. The cone half-angle is 0.15 radians;
it covers the shared raw cube's disk/halo/filter support, not only the physical
solar radius. The BSDF proposal still covers the complete lobe. No independent
analytic sun-specular term is added. Shared direct sun remains in diffuse
illumination. Exact delta events keep their deterministic directions.

The isolated probe has a deliberately wider analytic cone and constant sky:
it checks sky-only, cone-only and combined energy independently. Full shared-sky
generation/filter/accounting checks remain a P1 integration gate. The P0 furnace
is uncompensated GGX; shared compensation and the complete diffuse/sheen model
must still pass their P1/P2 controls.

## Concrete integration choices

The native schema admits `--scene materials`. The current renderer checkpoint
supports an isolated bronze sphere; other catalog selections/specimens reject
explicitly until their integration gates are implemented.
`--material bronze|<catalog-id>|gallery`
selects an isolated specimen or all twelve. `--specimen sphere|block|swatch|cut-block`
uses existing specimen semantics; unsupported checkpoint combinations reject
explicitly. Material import/export uses `--recipe-input`/`--recipe-output`.
Pattern/bump toggles use `--pattern`/`--bump` and their negative forms.
`--material-roughness` defaults to -1 (recipe content); existing legacy RT
`--roughness` keeps its current meaning. Catalog IDs remain stable in each
validated snapshot. Native changes edit owned resolved recipe values.

The existing `--reflection-source` and `--transmission-source` controls select
environment or scene transport. The new workshop defaults both to environment,
with shadows disabled and the plan's matched atmosphere/camera values. Legacy
scenes retain all their existing defaults. A scene-optics preset enables local
interactions explicitly. Recipes and environment manifests remain separate.

The GPU adapter's checked record contains surface, pattern, optics, affine domain
and library data, followed by routing metadata. The legacy 64-byte optical ABI
stays separate. One fixed-capacity recipe table is uploaded per retired frame
slot. Source fingerprints and resolved/edit fingerprints are both recorded.
SDF's effective rebased domain is separate from canonical source identity.

Prepare isolated and gallery TLAS routes once, sharing the admitted specimen
BLAS resources. An isolated instance resolves the selected stable catalog ID in
shader data; a gallery instance uses its fixed ID. Workshop masks admit both
opaque and dielectric routes so selecting a prepared material cannot stale an
optical cull mask. Views select a prepared TLAS descriptor after slot retirement.
Sphere meshes are built at both .25 and .075 radii, matching forward's actual
local geometry/material coordinates rather than scaling one radius silently.
Future additional specimen routes have explicit prepared costs.

The optical admission checks apply separately to active isolated/gallery routes.
Gallery dielectrics have disjoint bounds and per-body closed/wound mesh checks.
Inactive isolated geometry cannot participate in gallery traversal/admission.
Appearance edits reuse AS. Roster/routing reload still requires restart.

## Footprint and sample history

Carry a ray-cone width and angular spread. Primary spread is
`2*tan(verticalFov/2)/imageHeight` and initial width is zero. A segment of length
`t` adds `t*spread` to width. Surface footprint divides by
`max(abs(dot(ray,geometricNormal)),1e-4)` and transforms into object/material
units with the existing identity fast path and conservative nonidentity matrix
bound. It is independent of material identity and optical absorption units.

Reflection retains width. Propagated spread adds `2*alpha` for a rough event and
`2*width/radius` for the known sphere curvature. Transmission additionally scales
spread by `max(eta, eta*abs(cosIncident)/max(abs(cosTransmitted),1e-4))` before the
rough-event increase. TIR takes reflection propagation. This is a bounded
engineering cone approximation, not exact ray differentials or a proof of a
hard bound on GGX tails. P2 requires measured minification/domain/secondary-path
controls; additional specimens need an explicit curvature policy.

`--samples` is a bounded target per fixed view state, default 256, maximum 8192;
`--seed` defaults to 7. Native interaction advances one sample per rendered
iteration until capped. Render-only measurements also use one new sample per
iteration and choose a cap above the measured run length. PNG capture completes
the target before readback. Each video output frame freezes authored time,
completes the target, captures once, then advances presentation time. Existing
`--frames` continues to count output/host iterations, never SPP implicitly.

Use one RGBA32F sum/count history per active view generation and existing
per-slot HDR output images. The count is exact in float throughout the admitted
sample range. Each ray dispatch reads the prior count and writes its pixel once;
the jitter/sample index derives from count plus pixel and seed. Queue-ordered
RT read/write barriers serialize dispatches. New epochs clear the history once
with transfer-to-RT synchronization. Resolve the accumulated average into the
existing HDR/post route; exposure is applied once after accumulation.

History identity includes camera/extent, geometry/route, resolved material table,
pattern/bump/domain state, lighting/probe generation, transport/budgets and seed.
Exposure/display edits re-resolve the same linear history. Random jitter and
sample index do not invalidate it. The renderer owns epoch/sample diagnostics;
resize retires the entire old history generation through existing host ownership.
No reprojection, extra queue or asynchronous scheduler is introduced.

## P1 measured integration corrections

These findings refine the initial contracts using native comparisons. They do
not change accepted forward/SDF defaults, material assets or the completion gates.

**Camera pose.** Forward's quaternion orbit lowers the eye for positive pitch;
the earlier SDF/RT spherical orbit raises it. The workshop RT adapter now follows
forward's convention. A common pitch of +0.15 therefore maps to -0.15 in an
existing SDF preset. Match physical eye/rotation, not just the numeric pitch.
Legacy RT keeps its original convention and floor clamp. The workshop bypasses
that clamp and keeps the separate 2-metre atmospheric surface frame.

**Visible versus reflected atmosphere.** The workshop uses the existing
`AtmosphereBackgroundFrame` for a groundless visible sky, alongside the shared
runtime's `Ground` reflection probe. A per-slot background texture passes that
linear HDR result to ray generation. No second atmosphere model or cloud owner
was added. The first native bronze environment capture completes its target
sample count without Vulkan/sync messages.

**Solar intensity convention.** The shared [sky source](../../shaders/cubey/atmosphere.glsl)
explicitly uses renderer units without photometric calibration. Its visible
disk uses source 22, while [CPU lighting](../../src/cubey/render/atmosphere_environment.cpp)
supplies an independently authored analytic irradiance. Simply integrating the
raw visible disk while omitting analytic sun specular produces a much dimmer
highlight than the existing workshop model. The initial single-emitter
assumption therefore did not hold for those two products.

The sampled RT route now captures the shared shader's `SunDisk` component at the
same 128-square cube faces, ground policy and probe generation. It subtracts
that component from raw sky radiance, then replaces it with a finite solar emitter
whose RGB radiance is `sharedDirectIrradiance/(pi*sin(radius)^2)`. The angular
radius remains the shared 0.004675 radians. The visible sky and cached comparison
route keep their accepted presentation. This is an adapter to the shared light's
renderer units, not calibrated lux or per-segment atmospheric transport.

The 50/50 mixture now proposes the actual solar cone, replacing the provisional
0.15-radian visual-halo proposal. The shared analytic sun is not also added as a
scene-specular term. `--workshop-lighting combined|sky|sun|furnace` isolates the
inputs; full component/flux checks remain a P1 gate. The extra component capture
uses the same shared atmosphere shader and cube-face basis and is counted in
setup/update/resident costs.

**Cutoff semantics.** Raw path weights depend on the proposal PDF. A small weight
for a bright solar emitter is not a small radiance contribution. The legacy
1e-4 throughput cutoff visibly clipped the sampled highlight. The new workshop
defaults to zero and rejects nonzero raw-weight cutoffs. Event/radiance/total-ray
limits remain 8/32/128. Legacy deterministic optics retain their prior cutoff.
No convergence tolerance was loosened.

**Solar support at float precision.** A diagnostic caught two pixels where
re-evaluating membership in the tiny solar cone at a miss disagreed with the
mixture denominator; one exploratory 256-sample mean contained a rare bright
outlier of 139.8 against a 4096-sample value of 0.1222. A sampled light now carries
its known PDF/support by construction; a BSDF proposal evaluates that support
once and carries it through the miss. Radiance and the mixture denominator use
the same classification. The failed probe is retained under ignored
`bronze-solar-pdf-probe` outputs. These exploratory captures are not accepted
milestone evidence; final qualification requires fresh source-pinned captures.

## Qualification commands

The isolated BSDF control uses the existing ray-generation pipeline and bounded
raw observation buffer; it does not depend on workshop scene support:

```sh
cmake --build --preset dev --target cubey_project_raytracing \
  cubey_project_raytracing_tests -j 6
uv run projects/raytracing/tools/qualify_material_bsdf.py \
  --output outputs/raytracing/material-workshop/bsdf/qualification-rerun
```

The test captures 131,072 native inputs with validation/sync checks and reports
the original fixed arithmetic/direction limits, numerical integrals and retained
rejection mass. Failed early probe receipts remain in ignored outputs. A passing
probe is preparation for integration; all P1-P4 image, operation, retention and
cost evidence remains outstanding.

## P1 implementation checkpoint

The isolated bronze consumer now loads the shared catalog through a checked
368-byte recipe record, evaluates the existing procedural library, and offers
cached or sampled reflection. Other visual materials remain guarded until their
stages pass. A record probe already checks all twelve entries against the separate
shared CPU evaluator; this establishes content/layout parity, not twelve-material
rendering support. Four 9,216-point cases (canonical, patterns off, relief off,
and a nonidentity affine domain) passed the fixed component/normal limits, with
maximum component error 2.08e-5 and no validation/sync messages.

The stochastic GGX evaluator uses the normalized analytic distribution and
correlated Smith visibility. The old punctual evaluator retains its denominator
floor for legacy arithmetic. Reusing that floor in a sampled integral reduced a
polished bronze furnace center from approximately [0.67954, 0.38643, 0.13843] to
[0.00386, 0.00219, 0.00079]. The separate normalized evaluator fixes the estimator
without rewriting existing punctual consumers. Independent quadrature, rejection
mass, solar-flux and direction controls pass; the maximum direction error is
7.10e-6 against the unchanged 1e-5 limit.

Per-pixel 8-by-8 stratification and balanced BSDF/solar proposal pairs preserve
the same marginal mixture and sample-prefix identity. Exploratory 640-by-480
bronze comparisons at seeds 7 and 31 against independent 4096-SPP seed 19 gave
normalized mean errors 0.002624/0.002600 and p95 errors 0.018861/0.018741. These
meet the original 0.005/0.02 limits. Fresh committed-source replay, the 8192-SPP
stability control, scoped timings and the remaining P1 matrix are still required.

Native preview and edit checks exercised exposure-preserving history, recipe
edit/save/load, rejected input retaining the snapshot, canonical reset and resize.
Appearance edits retained the two original AS builds. The test uses its own Xvfb
display and actual native controls. A 1000-edit residency stress route is available
as --stress-edits; it is a lifecycle probe rather than a timing preset.

## P1 accepted evidence

The [P1 receipt](../evidence/raytracing-material-workshop-2026-11/p1.json) pins
74f42e3c, binary/shaders, unchanged recipes and the scoped results. Two 256-SPP
scene seeds pass the fixed mean/p95 gates; the independent 8192-SPP control has
mean 0.000811 and p95 0.005308 against the 4096-SPP reference. Eight furnace
roughness controls remain finite/nonnegative; center agreement with shared DFG
is within 0.00934, below 0.02. Combined sun/sky additivity passes. Three serial
1080p repetitions per optical route retain exactly 150 completed one-sample
iterations after 30 warmups. Total-frame p95 is at most 0.975 ms cached and
0.996 ms sampled. Setup, validation and raw readback are outside those timings.

Native scene selection/edit/save/load/rejection/reset/resize passes. Exposure
preserves history. One thousand edits produce 1000 epochs, fixed requested
residency of 1,890,400 bytes, two AS builds and one target generation with clean
Vulkan/sync validation. Matched forward/SDF/cached-RT bronze stills were visually
reviewed at the common physical camera pose. Sampled RT shows its finite-sample
noise without changing the canonical bronze. The original eleven protected
SDF/horse files remain unchanged. P2-P4 remain required.

## P2 opaque implementation checkpoint

All ten opaque recipe families now use the same RT adapter and procedural
evaluator. Oren-Nayar direct diffuse/A-only sky approximation, Charlie sheen and
the shared sheen-energy attenuation extend the bronze model without changing
assets or the accepted forward/SDF helpers. The actual recipe-record probe also
checks rough-diffuse and sheen terms against separate double-precision formulas.
The library/component/minification/operation/cost gates remain in qualification.

Existing forward sphere, beveled block, swatch and bored-block builders now live
in the narrow shared material_specimen_mesh header. Forward construction is
unchanged; its matched bronze PNG retains SHA256
989cf2505b36ea1ac6af15111f40fb103896ee52fd704a675878de3e3c279e02.
RT widens indices and orients opaque triangles against supplied outward normals.
Four BLAS and four single-specimen TLAS routes are prepared once. Selection and
appearance edits use the retired slot descriptor/recipe table and do not build
AS. Glass and the full gallery remain explicitly guarded for P3.

The ray-cone transform now uses sqrt(matrix-one-norm times matrix-infinity-norm),
a conservative operator bound that is exact for uniform and axis scale. This
avoids a sqrt(3) filtering jump on a uniformly scaled mesh. Shared recipe-domain
filtering keeps its existing Frobenius policy. The opt-in object-scale control
prepares a positive uniform transform at startup; recipes stay in local geometry
coordinates and optical segments stay in world geometry units. Secondary angular
spread uses the specimen curvature (sphere, bevel, swatch bevel or bored hole)
and the world-to-object scale bound. It remains an engineering cone policy, not
a bound on the unbounded GGX tail.

Native family/material/specimen selection, recipe detail controls and keyboard
navigation are available in the existing workshop panel. Base color, roughness,
metallic, footprint, diffuse/specular and sheen views expose component evidence.

## P2 accepted evidence

The [P2 receipt](../evidence/raytracing-material-workshop-2026-11/p2.json) pins
afd050c5, its executable/shaders, actual compiler/flags, recipes and scoped results.
All ten opaque recipes pass two 256-SPP seeds against independent 4096-SPP controls
in both optical routes, plus 8192-SPP reference stability. The worst mean is
0.002624 and p95 0.018865 (bronze), under the original 0.005/0.02 limits.

The matrix covers four specimen routes per material, pattern/bump switches, a
grazing swatch, distance, component sums and matched double-size/distance local
coordinates. Scale comparisons preserve base-color samples exactly. The center
footprint distance ratio is 2.321, consistent with the longer camera-to-surface
segment. All 160 furnace roughness/route cases are finite/nonnegative; sun/sky
component additivity passes. GPU/CPU surface, rough-diffuse and sheen records
meet the unchanged component/normal limits.

Sixty serial 1080p timing runs retain 150 completed one-sample iterations per pass
after 30 warmups; the maximum total-frame p95 is 1.144 ms. Native edit/import/
rejection/reset/resize and family/material/swatch selection pass with clean sync
validation, fixed residency and eight prepared AS builds. Focused RT tests pass
14/14. The forward sphere capture retains exact PNG bytes, and separate C++
comparison against the old private builders proves exact PbrVertex/index bytes
for block, swatch and bored block. All ten cached-RT sphere captures were visually
reviewed. P3/P4 remain required; these are bounded frozen-clear-light results.

## P3 dielectric math preparation

The new isolated `--dielectric-contract` probe audits rough interfaces before
visual glass admission. It uses the qualified visible-GGX mapping, exact
unpolarized Fresnel, correlated Smith, the reflection/refraction Jacobians and
radiance-mode eta weights. V1 remains a delta interface; index-matched IOR=1 is
also delta. Rejected microfacets keep their null probability. The implementation
follows [PBRT 4e dielectric equations](https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF),
with the project conventions above; it does not import another renderer layer.

The first probe exposed critical-angle cancellation. Fresnel now compensates
`cosine² + eta² - 1`; transmitted evaluation uses both known interface cosines
and a direct transmittance expression. The CPU oracle independently uses the two
polarized reflection coefficients. Arithmetic compares the actual recorded float
operands, as in the opaque probe: normalizing them again in double precision
changes the input of a sharp grazing lobe. Angular quadrature is separate and
never calls the ported sampler. Original tolerances remain unchanged.

Exploratory controls pass 131,072 arithmetic inputs, 208 angular integral cases
with 32,768 sampled events per row, and 32 rough-to-delta energy controls at
roughness .05. Maximum direction error is 2.29e-6 and scalar Fresnel error 1.88e-7.
Evaluation/PDF error uses at most 0.344 of the allowed absolute-plus-relative
limit. Integral energy error is at most .01951 against .02; rejection-mass error
is at most .00846 against .025. Two quadrature orders agree within .000192 on
nine harder controls. The rough-limit energy difference is at most .00632.

The predeclared regular arithmetic region excludes `abs(1-sin²(thetaT)) <= 1e-5`
or an outgoing cosine at most 1e-4. These boundary observations are reported,
retained in estimator sums and never retried. A single interior interface can
amplify radiance by eta scaling; the <=1.02 exterior furnace gate applies to
outside interfaces and, later, complete closed bodies. None of these isolated
checks establishes geometry admission, two-body transport, glass convergence or
complete workshop support. Fresh committed-source replay remains required.

The history key now explicitly includes the shared probe generation, in addition
to environment uniforms. Contract probes dispatch once and do not increment
progressive sample counts, including when launched on the materials scene.

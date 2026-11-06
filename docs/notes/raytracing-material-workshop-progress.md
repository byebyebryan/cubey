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
| P1 bronze | Pending shared recipe, environment, history, native operation and matched capture gates |
| P2 ten opaque recipes | Pending |
| P3 two dielectric recipes | Pending |
| P4 full matched workshop and delivery | Pending |

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

The native schema now admits `--scene materials`. The scene fails explicitly
until the P1 renderer adapter is installed; it never falls through to a legacy
triangle scene. `--material bronze|<catalog-id>|gallery`
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

# Dragon optics: research and proposed next pass

Date: 2026-11-07. Research against the uncommitted dragon/slab candidate on
`raytracing-first-pass-20261104`, based on published `3c1cda12`.
This is a proposal, not an implementation or a new qualification receipt.

Follow-up: the [first sampled opaque dragon slice](raytracing-dragon-sampled.md)
is implemented locally. Its own receipt and limits are separate from this
research snapshot; glass admission remains pending.

## Recommendation

Keep the dragon, the accepted thin convex marble slab and the shared open
atmosphere. Bring the material workshop's sampled transport to this scene, then
make **actual clear glass** the main optical demonstration after mesh admission.
Use clay, ideal mirror and the library's Satin bronze as controls. A slightly
elevated three-quarter view should put recognizable marble veins behind and
below parts of the body; test that composition before assuming it will work.

The visible target is specific: **marble veins bending through the dragon,
thickness-dependent tint, and reflections of its own body and the slab that
move with the camera**. Another shiny-sky image would not meet the goal.
Diffuse GI is a separate lighting feature, rather than a prerequisite for these
paths. Caustics could become a useful later demonstration on this same base.

## What exists, and why the current result is subtle

The current shiny dragon is an **opaque ideal mirror**. Its native selector
supports clay or mirror; it does not select glass. It takes one centered primary
sample per pixel, has no sample history, and ignores roughness in mirror mode.
The marble base uses the shared procedural recipe, environment PBR and traced
sun shadows. This does not give the dragon the workshop's material transport.

The [material workshop](raytracing-material-workshop-guide.md) already has shared
recipes, sampled GGX reflection, sun/BSDF multiple importance sampling, clear and
frosted dielectrics, deterministic pixel jitter and FP32 progressive HDR history.
Reusing that work is the substantial next step; writing another integrator is
unnecessary. Its present qualification is for prepared specimens and disjoint
closed bodies, not this imported scan.

A fresh, paired 128×96 diagnostic used the current marble preset, mirror RGB
`(.94,.95,.96)`, and identical camera/light settings. Of 1,765 valid primary
dragon pixels, the first scene-reflection rays reached:

| First reflected destination | Samples | Fraction |
| --- | ---: | ---: |
| Sky | 1,163 | 65.9% |
| Dragon itself | 479 | 27.1% |
| Marble base | 123 | 7.0% |

No observed primary-dragon path reported a transport-status flag in this small
sample. These are first-hit counts from one low-resolution centered sample per
pixel, not full-resolution coverage, a perceptual score, or a performance result.
The scene/environment mean absolute linear RGB difference on those pixels was
0.06745; a changed-pixel metric alone does not establish a readable reflection.

Our interpretation is that a smooth sky dominates this view and is already
represented by environment reflection. Local hits exist, but are confined to
complex body regions and the lower silhouette. The native 1080p capture also
shows fine stippling and jagged detail. Because this route has no stochastic
sampling, that appearance should not automatically be diagnosed as Monte Carlo
noise or used to justify a denoiser.

## Exact asset audit

The cached GLB's SHA-256 is
`3f9dc36d34ce148e92ac4d096339a5cc48a9c4a86126b9963b052676a8a2260e`.
A read-only analysis found:

| Property | Result |
| --- | ---: |
| Source vertices / triangles | 437,645 / 871,414 |
| Exact unique positions / duplicate positions | 435,545 / 2,100 |
| Zero-area triangles excluded from analysis | 108 |
| Remaining triangles | 871,306 |
| Boundary edges / edges with more than two faces | 0 / 0 |
| Two-face edges with inconsistent winding | 0 |
| Vertex-connected surface components | 1 |
| Algebraic signed volume | 0.0556057 scene-unit³ |

Exact coincident positions were identified in memory; the source GLB and its
preserved opaque index stream were not modified. This is encouraging evidence
for a small optical preparation step. It does **not** establish vertex-link
manifoldness, absence of self-intersections, or a valid embedded dielectric
volume. CGAL explicitly distinguishes combinatorial mesh validity from geometric
defects and provides self-intersection predicates; use an offline robust check
rather than treating closed edges as a complete certificate.
([CGAL polygon mesh processing](https://doc.cgal.org/latest/Polygon_mesh_processing/).)

The [Stanford repository](https://graphics.stanford.edu/data/3Dscanrep/) warns
about holes in its Dragon reconstruction, but its landing-page counts differ
from our pinned member. Neither that generic warning nor this narrower audit
should be substituted for checking the exact optical input.

## Research implications

| Topic | Consequence for this project |
| --- | --- |
| Reflection versus glass | A dielectric needs Fresnel reflection, refraction and total internal reflection, with a consistent radiance convention. A smooth interface and a rough transmitting surface are distinct models. Reuse the existing clear-glass implementation and its controls. [PBRT dielectric BSDF](https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF). |
| Sampling | Jitter and reconstruction address undersampling; local surface filtering addresses the material footprint. More samples do not repair invalid volume geometry or path truncation. Separate silhouette aliasing, procedural shimmer, variance and mesh faceting in comparisons. [PBRT sampling theory](https://www.pbr-book.org/4ed/Sampling_and_Reconstruction/Sampling_Theory), [Igehy, Tracing Ray Differentials](https://graphics.stanford.edu/papers/trd/). |
| Rough specular light | Visible-normal GGX sampling and light/BSDF MIS reduce difficult specular variance. Both mechanisms already exist in the workshop; preserve them when adding dragon geometry. [Heitz, GGX visible normals](https://jcgt.org/published/0007/04/01/paper.pdf), [PBRT better path tracer](https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer). |
| Boundary robustness | Secondary origins must avoid self-hits without stepping through nearby geometry. Test the current offset against horns, thin folds and grazing rays before choosing an upgrade; do not simply increase epsilon. [NVIDIA self-intersection analysis](https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/). Its DXR error constants are not automatically a Vulkan guarantee. |
| Motion quality | Progressive still accumulation loses history when the camera changes. A temporal reflection denoiser is another integration: for example, AMD's implementation consumes depth, motion vectors, normals, roughness and variance/history resources. It is not a generic drop-in glass solution. [FidelityFX Denoiser inputs](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/denoiser/). |

## Shared foundation contract

| Foundation | Proposed use |
| --- | --- |
| Material library | Keep canonical recipes and object-domain evaluation. Balanced marble stays on the base; bronze and glass use their existing recipe identities. Record instance overrides separately from source recipes. |
| Atmosphere and lighting | Retain one `AtmosphereEnvironmentRuntime`: shared sun, clear sky, diffuse SH and sky specular resources. Keep the workshop's solar sampling/MIS treatment. Do not add cached environment specular on top of the same traced lobe. |
| Presentation | Accumulate linear HDR before shared postprocessing; apply exposure once. Camera/material/light/transport edits reset history; exposure alone preserves it. Save/reload includes effective materials, source modes and sampling settings. |
| Scene | Exactly the dragon and the existing asymmetric convex slab, with flat caps and 30 mm thickness. Keep their placement, contact relationship and canonical marble pattern. |
| Clouds and aerial perspective | Current RT uses a directional clear-sky cache, not cloud composition or atmosphere integration along each segment. Later cloud work must use shared owners and define primary, reflected and transmitted visibility together. No separate RT sky/cloud implementation in this pass. |

## Proposed sequence and acceptance

### 1. Establish the visible target

Use the existing scene to audition a small camera/sun sweep, keeping the slab
fixed. Include the current view, a moderately elevated view with more marble
behind the lower body, and a short orbit. Preserve exposure across each matched
scene/environment pair. Label the legacy mirror accurately.

**Gate:** identify a specific self/base reflection region that a viewer can
recognize at normal presentation size, not only a difference heat map. For
glass, nominate a region where marble should be visible through the body. If
the current geometry cannot provide a convincing reflection cue, make glass
the principal demonstration rather than assuming extra reflectivity will help.
This stage does not reopen the base design or add props.

### 2. Bring sampled shared materials to the dragon

Create an opt-in dragon presentation route that uses the existing workshop
transport, recipes and history. Start with canonical bronze to exercise rough
reflection and the unchanged marble base. Retain clay and the old ideal mirror
as controls. Enable scene reflections in the new showcase preset; leave existing
defaults and historical presets reproducible.

This is more than setting `Scene.workshop`:

- Decouple transport/history capability from the specimen roster, selector UI
  and camera convention. The workshop and legacy dragon currently use opposite
  pitch conventions; the saved dragon view must not jump during migration.
- Disable the isolated-specimen `knownEmptyOutside` shortcut. A ray leaving the
  dragon can still reach its own geometry or the slab.
- Replace `workshopCurvature()`'s hardcoded sphere/block assumptions with an
  explicit general-mesh footprint policy. Use the existing cone machinery with
  a conservative, documented mesh bound or differential estimate; validate the
  base pattern through secondary paths. A sphere radius is not a dragon bound.
- Keep per-instance recipe selection and history invalidation explicit. Material
  edits should not rebuild geometry; any different optical AS routing must be
  prepared and reported rather than hidden in a material switch.

**Gate:** deterministic progressive AA; visibly stable rough reflection after
settling; matched environment/scene comparison using the same material and SPP;
shared recipe identities; no duplicate sun/environment energy; correct reset,
exposure, resize and saved-view behavior. The existing workshop and legacy
controls retain their scoped regressions. Record AS build counts and actual
target/history generations rather than inferring reuse from unchanged pixels.

### 3. Qualify the scan, then enable clear glass

First attempt preparation with **zero position movement**: identify exact
coincident positions for topology, preserve render attributes, exclude the 108
zero-area faces from the optical index stream, and audit winding, vertex links,
self-intersections, shell orientation and body separation. Preserve the original
GLB and opaque route. Record the optical index/asset fingerprint and every change.

`prepare_workshop_mesh()` is a starting point, not an admission certificate for
the scan: it currently snaps near positions, can flip triangles according to
vertex normals, and checks edge closure/volume. Its policy needs an explicit
imported-mesh mode and the additional offline geometry checks. A topology-only
preparation should not silently inherit snapping or per-face reorientation.
Do not convexify the dragon. If a valid volume requires geometric repair,
return a bounded repair/derivative proposal with deviation measurements before
substituting a new optical surface.

Reuse per-body medium tracking for a single nonconvex closed glass dragon and
an opaque slab. A valid path can enter and exit several separated parts of the
same body. The legacy all-face-plane camera test assumes a convex volume and
must not become the dragon's inside test. The workshop's conservative camera-AABB
exclusion is an acceptable initial restriction; verify the base/body admission
against their actual bounds. Nested/overlapping media remain outside this pass.
Set the correct glass classification and masks: the legacy opaque/glass TLAS
masks differ, so changing only a material uniform is insufficient there.

Start with canonical clear Tinted glass, IOR 1.5 and the existing metre-based
absorption. Its stored roughness does not turn it into frosted glass; preserve
the library's explicit clear/frosted distinction. Use geometric normals for
medium classification and offsets. The workshop also uses geometric normals
for the dielectric interface; inspect whether scan faceting is visible. Any
smoother optical-normal or surface policy needs a labeled comparison and
boundary-safety checks, rather than quietly changing the geometry/model.

**Gate:** show marble distortion, grazing Fresnel reflection, thickness tint,
and stable entry/exit/TIR behavior on the actual dragon. Include an IOR=1,
neutral-absorption control against a view with the dragon hidden: the base/sky
should remain unwarped, within sampling/numerical error and sufficient budgets.
Compare scene/environment transmission only after tracing the body's required
interfaces; environment mode must not skip its exit boundary. Include thin-fold
and grazing close-ups, finite HDR checks, zero invalid-medium events in the
review views, and explicit path-limit diagnostics.

### 4. Qualify still and native motion separately

Sweep 1, 8, 32, 128 and 512 primary SPP at fixed camera/light and deterministic
seeds, extending the reference if it has not stabilized. Compare default
8-event / 32-radiance / 128-total-call limits with the existing deeper
12 / 64 / 256 control. More SPP reduces variance but does not remove bounded
transport bias. Inspect feature regions and dark tails as well as aggregate HDR
error; diagnose truncation separately from rough dielectric energy loss.

Measure completed GPU time per iteration, capture/settle time and memory on the
actual GPU. Exercise orbit, stop, material edit and resize in the native app.
Report moving quality and time to settle explicitly. A movie converged at every
frozen frame is an offline output, not proof of clean live motion. The older
single-sample dragon timings do not predict this route's cost. Set any FPS or
settle-time target from that measured pilot rather than inventing a guarantee.

**Gate:** a reviewable still pair, a clearly labeled converged orbit, native
motion evidence, material/history lifecycle results, mesh identity and transport
limits. Reopen temporal reconstruction only if measured motion quality is the
remaining obstacle. Preserve an unfiltered high-SPP reference for that work.

## Deferred work and reopen triggers

- **Diffuse GI:** reopen for missing bounce illumination or color bleeding that
  materially weakens this composition. Shared SH diffuse remains the explicit
  approximation until then.
- **Caustics:** reopen if focused light on the marble is chosen as the next
  optical feature. Current straight Fresnel/Beer shadow transmission does not
  bend light or form caustics; an ordinary camera path tracer does not by itself
  guarantee efficient sun-through-glass caustics.
- **Frosted glass:** reuse the existing model after clear glass and the scan are
  qualified. Retain its bounded estimator/energy limits.
- **Denoising/reprojection:** reopen on measured low-SPP live motion needs, with
  reflection and transmission coverage evaluated separately.
- **Clouds, dispersion, new lights and broad scene changes:** independent scope.

## First implementation slice

After the camera/feature audition, implement the opt-in sampled **opaque** dragon
route with canonical bronze and marble, history lifecycle and mesh-appropriate
filtering. Stop at that review gate before attaching clear glass. In parallel
with that execution stage, the scan's offline admission can establish whether
glass needs only index preparation or a separate geometry decision. This keeps
the first change useful and reviewable even if optical asset preparation fails.

## Research evidence and preservation

Local ignored artifacts: `outputs/raytracing/dragon-optics-research-20261107/`:
`audit_mesh.py`, `mesh-audit.json`, paired `mirror-scene` / `mirror-environment`
PNG and binary diagnostics, `inspect_current.py`, `current-optics.json`, and
`research-identity.json`. The diagnostic runs used `--headless --frames 1
--width 128 --height 96 --require-validation` and the same development executable;
they are not a new source-build or timing qualification. The identity record
captures its hash and the inspected runtime/material files.

This pass changes documentation only. The cached dragon and existing runtime
candidate remain intact. The protected original worktree's 11-file checksum
manifest was rechecked without mismatches. Existing image/test/timing receipts
retain their original source scope. Nothing from this pass is committed, pushed
or promoted to the public site.

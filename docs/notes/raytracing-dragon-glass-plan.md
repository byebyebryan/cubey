# Dragon glass: research and implementation plan

Date: 2026-11-07. Follows reviewed local commit
`c02a938a625a8b418986c243ed71cddfd4289d04` (sampled opaque materials and the accepted
convex marble slab). The original proposal below records the research boundary.

**2026-11-08 acceptance:** the user selected the cleaned original main-shell
model for optical rendering. This supersedes the proposed .1 mm trial-fidelity
hold without waiving geometry validity. The production preparer reproduces that
geometry, rechecks its serialized float32 boundary and emits a separately named
GLB/certificate. Geometry payload SHA-256:
`4284cac599c2191e5a611fb76caeaaad2f31d4e8cd501c10d1570ef2e4886eb4`.
Accepted GLB with updated admission metadata:
`bfc08772bd797a356e9b61a400b5bda735270940b55cba3fbf4fd08ae8c01c47`.
The original opaque model remains unchanged. G1 and the essential G2 controls
are delivered in [the first glass pass](raytracing-dragon-glass-first-pass.md).
G3 presentation/convergence and isolated filtering acceptance remain separate.

## Direction

The committed slice is chiefly **opaque reflection**, with shared material
shading and traced shadows. The next slice should be **solid clear glass**:
Fresnel reflection plus refraction, internal reflection/TIR and distance-based
absorption. Those components are coupled at each dielectric interface, rather
than two unrelated effects. The existing workshop already implements the relevant
dielectric model. [PBRT dielectric BSDF](https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF).

Keep the dragon, its 30 mm convex marble base, camera convention and shared
clear atmosphere. Make marble veins visibly bend through the lower body while
reflection and tint convey the rest of the volume. Canonical Tinted glass is the
first material: IOR 1.5, attenuation (.65, .90, .76) over .5 m, sharp interfaces.
Its stored .05 roughness does not enable the separate frosted-glass model.

**Start with a small optical asset preparation task.** The new mesh audit found
actual self-intersections, so enabling glass by changing a material flag would
skip a necessary boundary qualification. Keep the original opaque asset intact.

## What the exact mesh check established

The original cached GLB remains SHA-256
`3f9dc36d34ce148e92ac4d096339a5cc48a9c4a86126b9963b052676a8a2260e`.
The audit identifies exact coincident positions and excludes 108 zero-area faces
in memory, without moving vertices, flipping faces or changing source normals.
It writes an analysis-only OFF file outside the model cache.

| Check on that analysis boundary | Result |
| --- | ---: |
| Active unique positions / triangles | 435,545 / 871,306 |
| Edges with other than two incident faces | 0 |
| Inconsistent edge winding | 0 |
| Vertices with multiple/disconnected link cycles | 0 |
| CGAL-reported intersection pairs | **201** |
| Nonadjacent pairs / pairs sharing a vertex | 135 / 66 |
| Degenerate self-pairs / shared-edge pairs | 0 / 0 |
| Distinct affected faces | **190**, about **0.0218%** |
| Regions connected by intersections or shared vertices | **7** |

The largest region spans approximately 24 × 38 × 2.51 mm and contains 137
affected faces. The other regions contain 5–13 affected faces each. Region span
is not penetration depth or the displacement needed to repair it. A small face
fraction suggests local repair is worth trying; it does not prove repair is easy.

CGAL 5.6 predicates ran through the pinned `cgal==5.6.post202402071826` Python
3.11 package. A closed tetrahedron passed the negative control; crossing closed
tetrahedra passed the positive control. Imported vertex/face counts match the
analysis input, and affected facet coordinates were checked against its OFF data.
The package's polyhedron interface uses the documented exact-predicate kernel.
CGAL explicitly distinguishes these intersections from an ordinary shared edge
or vertex. [CGAL intersection predicates](https://doc.cgal.org/5.6/Polygon_mesh_processing/group__PMP__intersection__grp.html),
[binding kernel](https://raw.githubusercontent.com/CGAL/cgal-swig-bindings/main/SWIG_CGAL/Polyhedron_3/typedefs.h).

Thus the mesh is combinatorially closed/manifold but **not an intersection-free
optical volume**. Positive algebraic volume and an orientation predicate do not
override that result. Deleting only the 108 zero-area triangles is insufficient.
The finding does not invalidate its existing opaque rendering.

Two further measurements matter for glass:

- Geometric face normals differ from averaged shading normals by median 1.62°,
  p99 12.51°. Adjacent face-normal changes have median 2.74° and p95 12.13°.
  Sharp glass using the actual triangular boundary may show faceting that a
  smoothly shaded opaque mirror conceals.
- 16,756 triangles have a minimum altitude below the renderer's current 20 µm
  origin offset. This is a triangle-shape measurement, **not body thickness**
  and not evidence that every such face leaks. It warrants targeted offset and
  grazing-ray checks, including the separate current 1 µm ray minimum distance.

## Reuse and integration boundaries

Reuse the workshop's exact dielectric Fresnel/Snell directions, radiance weights,
Beer absorption, first-two-depth Fresnel splitting, later stochastic continuation,
progressive history and shared postprocessing. Keep one atmosphere/light owner.
The initial clear-glass route needs no new diffuse GI, spectral dispersion,
cloud integration or denoiser.

Several parts need explicit adaptation:

| Area | Required contract |
| --- | --- |
| Material admission | A model certificate admits glass; a scene name or removal of `opaque_only` does not. Keep frosted glass outside the first pass. |
| Per-instance roles | Resolve the selected recipe for the dragon and the independent fixed recipe for the base identically on CPU and GPU. `validate_workshop_view()` currently treats non-gallery instances as the selected material, and the base's extra slot lies outside the editable catalog. Do not reuse that assumption for glass. |
| Bounds | Check the certified glass body's bounds and the actual opaque base separately. Conservative camera-AABB exclusion is acceptable initially. Avoid the legacy convex face-plane inside test for this nonconvex model. |
| Scene resources | Preserve the sampled background/sun resources, legacy dragon pitch convention and live history behavior. Make sampled capability explicit rather than relying on the current two scene-name checks in the atmosphere adapter. |
| AS lifetime | A prepared optical scene should use the same qualified boundary for its opaque controls and glass. Material switches retain its prepared BLAS/TLAS; existing original-mesh opaque presets remain reproducible. |
| Normals | Use geometric normals for medium classification and ray spawning. Start with geometric interface normals too. Smoother optical normals require a separately labeled policy and hemisphere/boundary checks. Shading and geometric hemispheres can disagree. [PBRT BSDF conventions](https://www.pbr-book.org/4ed/Reflection_Models/BSDF_Representation). |
| Footprints | The opaque route estimates variation from interpolated shading normals. Glass currently scatters about geometric normals and scales the cone by `1/eta`; this is not a general grazing-refraction footprint guarantee. Match filtering to the chosen interface normal model. |

For ray spawning, evaluate NVIDIA's available **Vulkan GLSL** error-bounded
implementation, including barycentric hit reconstruction and both front/back
spawn positions. Preserve its evaluation-order requirements and validate on the
actual GPU. Increasing the old epsilon is not the proposed fix. Limit any initial
policy change to the new optical route while retaining the old controls.
[NVIDIA GLSL implementation](https://github.com/NVIDIA/self-intersection-avoidance/blob/main/SelfIntersectionAvoidance.glsl).

For filtering, begin with the existing cone machinery and targeted higher-
resolution/downsampled controls on the marble. Add Snell-aware angular growth or
ray differentials if those controls show aliasing or excess blur; keep primary
sampling variance separate from footprint error. Directions can obey Snell's
law while their material footprint is still approximate.
[Igehy, Tracing Ray Differentials](https://graphics.stanford.edu/papers/trd/).

## A nonconvex transmission comparison needs a defined meaning

The current `environmentOnly && medium < 0` shortcut samples sky after the first
exit. That is appropriate for the isolated convex specimens it was designed
around. A ray exiting one part of the dragon can enter another part of the same
dragon. Consequently it is not a full same-body transmission control here.

**Recommend full scene transmission for the first glass slice.** Reject or hide
the unqualified environment-transmission choice in that new glass presentation.
Do not silently label a first-exit proxy as equivalent optical traversal. The
existing opaque and workshop comparisons remain available.

A later matched environment control should trace every required same-body
interval while omitting other scene objects, then sample the probe after a true
same-body miss. Prepared visibility masks by object role can support that without
rebuilding AS on a material switch. Internal reflection must continue tracing
the body's boundary regardless of the external reflection source choice.

## Proposed stages

### G0: a certified optical copy, with a visual difference gate

Work on the seven affected regions and a bounded neighboring ring, keeping
unaffected vertices fixed where possible. Try local repair before whole-model
remeshing. As a **proposed trial budget**, use maximum surface displacement of
.1 mm at the current one-metre model scale, plus matched 1080p silhouette/detail
and foot-contact comparisons. Report actual deviation and any topology change;
that number is not an already-approved artistic tolerance.

CGAL offers geometric repair and autorefinement, but splitting intersections
alone does not establish a valid solid. New intersection coordinates can also
acquire defects when rounded. The audited 5.6 Python package does not expose the
newer 6.2 repair pipeline; pick and pin a suitable offline tool explicitly if it
is needed. [CGAL mesh repair](https://doc.cgal.org/6.2/PMP_Mesh_repair/index.html).

Emit a separately named optical GLB and manifest. **Recheck the final serialized
float32 positions and indices**, not just a higher-precision intermediate:
nondegenerate faces, exact-position topology, vertex links, consistent winding,
no self-intersections, component/orientation/solid checks and separated base
bounds. Record original/derived hashes, tool/kernel version, operation log,
deviation and normal policy. CGAL is an offline preparation dependency, not an
ordinary renderer/build requirement.

**Gate:** valid derivative plus original/derived opaque comparisons using the
same recipe, camera and light. If local repair cannot meet the trial budget,
return the measured result and a bounded alternative before replacing the
surface with a global remesh. Keep the original GLB and opaque route intact.

### G1: clear reflection and refraction on that certified body

Add an opt-in optical presentation using the qualified mesh for both bronze and
glass. The implemented scene name is `dragon-optics`.
Validate the certificate/hash before allowing a transmitting recipe. Reuse the
shared dielectric transport and canonical marble base, with the instance/bounds,
spawning and source-mode contracts above.

The medium is the dragon instance, not a convex shell or a fixed pair of
surfaces. Support repeated entry/exit intervals and internal reflections of
that same body. No nested/overlapping media or camera-inside behavior is required
in this slice. Keep rough/frosted transmission out of its material selector.
Make native orbit/dolly controls respect the conservative glass exclusion before
render submission, retaining a valid view instead of exiting on an interactive
camera move. Saved/CLI views that violate the restriction should fail clearly.

**Gate:** the marble is visibly refracted through the body, with grazing
reflection and thickness tint. Material selection, recipe/view save, exposure,
resize and source changes preserve their existing history/lifetime contracts.

### G2: controls that separate bending, tint and path limits

Use the same qualified geometry and camera for:

1. IOR=1 with neutral absorption, compared with the dragon hidden. The marble and
   sky should be unwarped within sampling/numerical error, with enough path budget.
2. IOR=1.5 with neutral absorption, then canonical tinted absorption. These
   separate geometric refraction from Beer tint and keep recipe overrides explicit.
3. Grazing, thin-feature and lower-body views that exercise exit, re-entry and TIR.
   Require finite HDR and zero invalid-medium events in the accepted views.
4. Default versus deeper event/radiance/total-call limits at matched SPP. Record
   feature-region error and truncation status as well as full-image averages.

The current 8/32/128 defaults and 12/64/256 control were not qualified for this
nonconvex glass body. If the 12-event ceiling is insufficient, add a bounded
optics-specific reference mode before claiming convergence; extra SPP cannot
remove truncation. Preserve original route defaults. Include marble filtering
controls at higher image resolution and an explicitly labeled relief-off view.

### G3: presentation and measured live quality

Use the current three-quarter view and a slightly elevated alternate if needed
to expose recognizable veins behind the lower body. Keep the existing slab and
open atmosphere. Compare 32/128/512/2048 SPP with identical light/exposure/seed,
then record actual native orbit, stop and settle separately from frozen-frame
exports. Re-measure GPU cost and memory; opaque timing is not a glass estimate.

The first result can retain straight Fresnel/Beer sun-shadow transmission and
SH diffuse lighting, provided it is labeled accurately. Those paths do not form
focused caustics on the marble. Reopen caustics if that is chosen as the next
visible effect; reopen temporal reconstruction if live low-SPP motion is the
remaining obstacle. Neither is a prerequisite for the first glass demonstration.

## Original first execution boundary (completed; see acceptance above)

The next bounded task should be **G0 only: local optical repair and qualification,
with measured original/derived visual differences**. Review that result before
enabling glass in the renderer. This resolves the newly measured asset problem
without turning it into a simultaneous mesh, transport and presentation rewrite.

## Research evidence

[Compact audit data](../evidence/raytracing-dragon-glass-research-2026-11/README.md)
retains the measurements and intersection classifications. Full scripts and the
analysis-only OFF file remain under ignored
`outputs/raytracing/dragon-glass-research-20261107/`. Commands used:

```sh
rtk proxy uv run --no-project outputs/raytracing/dragon-glass-research-20261107/audit_boundary.py
rtk proxy uv run --no-project --python 3.11 --with cgal==5.6.post202402071826 --no-build python outputs/raytracing/dragon-glass-research-20261107/check_cgal.py
rtk proxy uv run --no-project --python 3.11 --with cgal==5.6.post202402071826 --no-build python outputs/raytracing/dragon-glass-research-20261107/classify_intersections.py
```

No repair was attempted in this research pass. The committed renderer, its
executable/shaders, source GLB, material catalog and original SDF work remain
unchanged. This follow-up plan and its compact research evidence are separate
from the reviewed opaque-material commit.

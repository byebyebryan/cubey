# Dragon optical preparation: G0 trial result

2026-11-08. Executes the bounded G0 study in
[the glass plan](raytracing-dragon-glass-plan.md), against opaque-renderer commit
`c02a938a625a8b418986c243ed71cddfd4289d04`.

**The geometric audit has a passing alternative, but the fidelity gate has not
passed. No optical asset is admitted and glass remains disabled.** The main-shell
alternative is separately identified under ignored outputs, not installed in the
model cache or selected by a preset.

## Measured result

Eighteen initial parameter/method variants covered bounded normal displacement,
bounded Laplacian smoothing, hole filling with two face-selection strategies,
MeshFix and MeshLib. None met both the intersection and 0.1 mm surface criteria.
This does not prove that every possible local repair must fail.

Removing the 190 intersecting faces plus one vertex ring and triangulating the
holes removes all 201 intersection pairs, without moving a vertex. However,
sampled original-to-candidate distance reaches **8.285 mm**. The original single
surface becomes three components; the two small components have inward winding.
Some new triangles oppose their inherited shading normals (p95 121.45 degrees).
Each of the seven neighborhoods exceeds 0.1 mm in at least one sampled direction.

A concrete alternative retains the largest repaired shell, discards the two
newly detached components (400 triangles), and assigns geometric normals to new
patch corners. It passes the final serialized float32 checks:

| Check | Main-shell alternative |
| --- | ---: |
| Active unique positions / triangles | 435,155 / 870,514 |
| Edges without exactly two consistently wound incident faces | 0 |
| Vertices without one disk link | 0 |
| Connected surface components | 1 |
| CGAL self-intersection pairs | 0 |
| Outward orientation / signed volume | outward / 0.0556057236581 m3 |
| Euler characteristic / genus | -102 / 52, originally -108 / 55 |
| Removed original nonzero triangles / added triangles | 855 / 63 |
| Source faces removed beyond the original one-ring selection | 387 |
| Largest sampled original-to-candidate distance | **13.537 mm** |
| Largest sampled candidate-to-original distance | **0.371 mm** |

The candidate SHA-256 is
`2d00f040a936c3195b6050a03b46144d7d9e696d02c070d24fd12901249a923f`.
Every original render position and normal remains bit-identical in the vertex
attribute prefix; 189 new patch corners duplicate existing positions and use
flat geometric normals. Retained source triangles preserve their original order
and corner attributes. The 108 original zero-area triangles are excluded.

The distance probes test changed vertices and triangle centroids against CGAL
AABB trees. A sample over budget is sufficient to reject a candidate. These are
**lower bounds on maximum surface difference**, not Hausdorff upper bounds:
13.537 mm must not be described as a certified global maximum. Unmoved vertices
do not bound surface change when triangles are removed or reconnected.

The single connected embedded boundary is a geometric result. It is separate
from artistic acceptance, renderer optical admission, medium traversal and
refraction quality. Both comparison GLBs explicitly carry `optical_admission:
false` and `REJECTED` in their filename.

## Native appearance and integration evidence

Original, initial-patch and main-shell copies were captured using the committed
RTX renderer at 1920x1080, 256 SPP for beauty, seed 7, with matched light,
exposure, material and cameras. Instance-mask and geometric-normal views use
one sample. Additional deterministic primary-ray records use 640x360.

For the main-shell alternative:

- Hero and lower-body-detail instance masks: **0 differing pixels** in each view.
- Hero geometric-normal PNG: **0 differing pixels**.
- Beauty PNGs: 40 differing pixels for bronze, 2 for plain, 54 for the closer
  plain detail, out of 2,073,600 pixels each. Corresponding linear HDR RMSE is
  0.000188833, 0.0000000906 and 0.000018962.
- At 640x360, 31,839 corresponding primary hits have the same instance; none
  change depth by more than 1 micrometre or geometric normal by more than 1e-5
  in vector length.
- Native scene dumps prove the slab mesh, transforms, instance material slots
  and marble recipe are identical. All changed faces lie above the source's
  lowest 15 mm contact band; that band's faces remain unchanged.
- HDR records are finite; native capture logs are free of Vulkan validation
  errors. These are frozen captures, not a live-motion performance qualification.

The close appearance suggests most removed structures are occluded in these
opaque views. It does not establish all-view silhouette equivalence or matching
glass appearance. Transmission can expose geometry hidden from opaque primary
rays, so image similarity alone is insufficient to accept the mesh.

## Proposed next boundary

**Review this main-shell alternative before G1.** It is a viable geometric
starting point if the separately named optical copy may discard folded scan
structures and change topology. That would revise the 0.1 mm surface-fidelity
contract; it has not been adopted by this study. The measured alternative is
available for a concrete visual and geometric review.

If retaining every part of the original surface within 0.1 mm remains required,
use a separate local reconstruction study. A pinned newer CGAL native tool could
refine intersection curves before classifying and rebuilding the local solid.
Refinement alone is insufficient: it can leave non-manifold contacts, and final
float32 encoding still needs the full audit. Neither success nor a tighter
deviation bound is established for that untried approach.
[CGAL geometric repair and autorefinement](https://doc.cgal.org/6.2/PMP_Mesh_repair/index.html).

Keep the existing G1/G2 transport contracts: explicit asset admission, independent
base material, geometric medium normals, robust ray spawning, repeated nonconvex
entry/exit and TIR, filtering controls and path-limit evidence. No transport
implementation was mixed into this G0 study.

## Preservation and evidence

The original GLB retains SHA-256
`3f9dc36d34ce148e92ac4d096339a5cc48a9c4a86126b9963b052676a8a2260e`.
All 99 starting renderer/material/research files and all 11 protected files in
the original SDF worktree retain their recorded hashes. Executable and shader
bytes are unchanged. No model-cache output, preset change, renderer edit,
commit, push or public-site publication occurred.

[Compact measurements and reproduction](../evidence/raytracing-dragon-optical-preparation-2026-11/README.md)
include the final audits and eight independent topology/predicate controls.
Full ignored trials, GLBs, native frames, binary observations and logs remain
under `outputs/raytracing/dragon-optical-preparation-20261108/`.

Subsequent 2026-11-08 decision: the user accepted the cleaned main-shell model,
superseding the proposed trial-fidelity hold. The results above remain the G0
measurements. [The first glass pass](raytracing-dragon-glass-first-pass.md)
records the accepted production certificate and later renderer changes.

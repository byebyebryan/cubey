# Certified dragon: first solid-glass pass

Review follow-up: [memory accounting and qualification acceptance fixes](raytracing-dragon-glass-review-fixes.md).
The original capture receipts below remain historical evidence.

2026-11-08, local `raytracing-first-pass-20261104` worktree. Builds on opaque
baseline `c02a938a625a8b418986c243ed71cddfd4289d04`. G1 and the essential G2
controls are complete; G3 motion/convergence and isolated filtering acceptance
remain separate work. No commit, push or public-site promotion.

[Native comparison and receipts](http://starship:8787/reports/471822d5ea971b044267052227c6c2802111dcd88ecfa93b0ed9f41d558a69c3/)
includes bronze, neutral glass, canonical tinted glass, an elevated view, an
interactive wipe and the numerical limits. This is a shared internal report.

## Model clearance

The user accepted the cleaned original main-shell derivative after the G0 trials
and alternative-model investigation. That replaces the proposed .1 mm trial hold;
it does not change the geometry validity requirements or turn its measured
deviation into a tolerance pass. Original-to-derived samples reached 13.54 mm;
the sampled distances are not Hausdorff upper bounds. The two detached inward
pieces were removed, and topology changed from genus 55 to 52. The foot-contact
band is unchanged. See the retained [G0 results](raytracing-dragon-glass-g0-results.md).

The production preparer reproduces the selected geometry payload exactly:
`4284cac599c2191e5a611fb76caeaaad2f31d4e8cd501c10d1570ef2e4886eb4`.
The new GLB hash is
`bfc08772bd797a356e9b61a400b5bda735270940b55cba3fbf4fd08ae8c01c47`;
only its admission/generator metadata differs from the previously labeled
rejected trial. A second preparation reproduced the GLB and full certificate
exactly. The original GLB remains
`3f9dc36d34ce148e92ac4d096339a5cc48a9c4a86126b9963b052676a8a2260e`.

Final serialized float32 audit: 870,514 nonzero triangles, 435,155 active exact
unique positions, one connected closed consistently oriented manifold, disk
vertex links, no CGAL self-intersection pairs, outward orientation and positive
volume. New patch corner normals are geometric; original corner attributes are
retained. Pinned Python 3.11 / NumPy 2.4.6 / CGAL 5.6 run offline, never in the
ordinary build/render path. The renderer checks the reviewed certificate,
model hash and loaded counts before allowing glass. Original, metadata-tampered
and position-tampered GLBs are rejected.

## Integrated scene

`dragon-optics` reuses shared recipes, dielectric transport, atmosphere/sun,
progressive history and postprocessing. Its certified mesh serves both opaque
controls and glass. The 30 mm convex slab retains a fixed independent canonical
marble recipe. There is no ground plane or added prop.

Full-scene reflection and refraction handle repeated intervals of the nonconvex
dragon, Fresnel reflection, TIR and Beer absorption. Glass starts at canonical
IOR 1.5 and attenuation (.65, .90, .76) over .5 m. Geometric interfaces are sharp;
frosted glass and environment-only transmission are excluded in this route.
Nested/overlapping media and camera-inside operation are unsupported.

CPU/GPU material roles agree. Native orbit/dolly retains a valid camera at the
conservative body bounds; CLI invalid views reject. Material switches keep the
same BLAS/TLAS. Saved effective recipe/view roundtrips are exact. Original
`dragon-materials`, primitive workshops and legacy scenes retain their modes.

## Numerical behavior and evidence

The new route reconstructs hits using NVIDIA's error-offset implementation and
uses adjacent geometric face directions near creases. Its `tmin` is zero, and
its certified closed solid queries entries outside and exits inside using
medium-aware facing culling. Other routes keep their fixed spawning policy.
No any-hit filtering, medium-state toggling or geometry mutation is used.

Earlier full-HD runs exposed rare wrong-facing hits. Double-precision rays found
entry/exit distances separated below a float32 distance step, plus a grazing
silhouette ambiguity. Facing selection resolves those finite-precision cases on
this certified boundary. Inside misses and cross-instance errors remain checked.
This is a measured float32 policy, not a proof for arbitrary models or views.
[Khronos traversal/culling](https://docs.vulkan.org/spec/latest/chapters/raytraversal.html)
defines the hardware behavior; the unmodified
[NVIDIA implementation](https://github.com/NVIDIA/self-intersection-avoidance/blob/main/SelfIntersectionAvoidance.glsl)
and its BSD-3-Clause license are retained in the project.

At 160 × 120 / 256 SPP, the default 128-event result equals the 256-event
reference, with maximum observed depth 91. The 24-event control has subject HDR
RMSE .0006126 and 2,822 pixels with truncation status; 64 events has RMSE
.000003114 and three such pixels. Defaults are optics-only 128 events / 1024
radiance rays / 4096 total queries, with a 256 / 2048 / 8192 deeper ceiling.
More SPP alone cannot remove truncation.

IOR 1 with neutral absorption matches the dragon-hidden background with HDR
RMSE .000398. Neutral IOR 1.5, canonical tint, grazing and thin-feature controls
are finite with zero invalid-medium events. Final 1920 × 1080 / 256 SPP tinted,
neutral, bronze and elevated captures have zero invalid-medium, event, query,
shadow-limit or stack flags. Flags OR every completed sample. A few zero-weight
branches report throughput termination with the raw cutoff fixed at zero.

The independent base recipe is unchanged at 649 sampled primary base pixels
after editing a marble subject. The native exercise verifies IOR edit, rejected
import retention, valid reload, bronze/glass selection, exposure history,
orbit/dolly and resize: three AS builds retained, three target generations,
32 settled samples, and 21 camera-update attempts held at the exclusion guard.

Validation: all 18 raytracing CTests, eight independent offline topology/predicate
controls against the production preparer, the new opt-in glass qualification,
the existing original-mesh opaque qualification and the native exercise pass.
Vulkan synchronization validation is clean. The original GLB, 32 existing
recipe/preset files and 11 protected original SDF-worktree files retain hashes.
The original executable/SPIR-V backup remains under the ignored output root.

## Remaining limits

The faceted/ice-like appearance is the actual triangular optical boundary.
Smooth optical normals need a separately checked policy. The Snell-aware cone
estimates angular growth at each flat interface; it does not bound edge jumps.
The 960 × 540 / 256 SPP elevated control versus the 2 × 2 average of a full-HD
256-SPP reference has HDR RMSE .682 whole-image and 1.34 in the lower-body/base
rectangle. Sampling variance, silhouettes and interface jumps are mixed into
those values, so they do not certify footprint accuracy. Relief-off is
bit-identical in this setup and supplies no additional relief evidence.
Isolating filtering error and higher-SPP/live settling quality belongs to G3.

Straight Fresnel/Beer sunlight transmittance and SH diffuse lighting remain
approximations. Focused caustics, diffuse GI, dispersion, rough glass and clouds
are deferred. No claim of full path-traced illumination or motion convergence.

## Reproduce

Run from this worktree after preparing the pinned original GLB:

```sh
rtk proxy uv run --no-project --no-build projects/raytracing/tools/prepare_optical_dragon.py --output cache/raytracing/stanford-dragon/optical-v1
rtk proxy cmake --build build/dev --target cubey_project_raytracing cubey_project_raytracing_tests -j8
rtk proxy build/dev/projects/raytracing/raytracing --config projects/raytracing/presets/optics/dragon-glass.json
rtk proxy uv run --no-project --no-build projects/raytracing/tools/qualify_dragon_glass.py --output outputs/raytracing/dragon-glass-check
rtk proxy ctest --test-dir build/dev -R '^raytracing_' --output-on-failure -j1
```

The preparation output must be new; use another output path for reproduction.
The checked certificate is `projects/raytracing/assets/stanford-dragon-optical.json`.
[Compact evidence](../evidence/raytracing-dragon-glass-first-pass-2026-11/README.md)
retains receipts; full models, HDR observations, failed trials and native images
remain under `outputs/raytracing/dragon-glass-first-pass-20261108/`.

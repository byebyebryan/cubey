# Sampled opaque materials on the dragon

Date: 2026-11-07. Local first implementation slice of the
[dragon optics plan](raytracing-dragon-optics-research-plan.md), based on published
`3c1cda12` plus the existing uncommitted convex-slab candidate.

## Run and scope

```sh
rtk proxy build/dev/projects/raytracing/raytracing --config projects/raytracing/presets/optics/dragon-sampled.json
```

The opt-in `dragon-materials` scene reuses the material workshop's sampled GGX
transport and progressive FP32 HDR history. It starts with canonical **Satin
bronze** on the dragon and canonical **Balanced marble** on the accepted convex,
30 mm slab. Its selector admits all ten opaque library materials. Glass and the
specimen gallery are rejected for this scene; the scan has not been admitted as
a dielectric volume. Existing clay/mirror dragon presets and the material
workshop keep their defaults.

[Interactive comparison and native motion](http://starship:8787/reports/52b247a6c27b1b2d7b02aa0300ea1552612065f00d04e9611d5ef109f8e490ed/)
contains the 1080p material controls and the actual native recording. All 19
published files match their authored bytes; browser source/material controls,
sample selection and eight-second video playback were verified. The existing
shared report server was reused and left running.

The camera audition selected yaw .45, pitch .35, distance 1.75 m and target y=.32.
Positive pitch still raises the dragon camera. This exposes more of the base
while keeping the two-object composition. The scene/environment comparison uses
the same camera, light, material and sample target. The optional polished bronze
capture changes effective roughness from .32 to .10 and is labeled as an override;
it does not edit the canonical recipe.

## Shared rendering and material ownership

The project adds a scene layout, not another integrator. The dragon gets one
prepared TLAS over its two unchanged meshes; material edits upload recipes and
invalidate history without rebuilding acceleration structures. The selected
recipe applies only to the dragon. The base has a separate immutable GPU recipe
slot, so even editing a marble dragon leaves the base's canonical surface alone.

One existing atmosphere runtime supplies the clear sky, sun and SH lighting.
The sampled background and sun-component resources are now available to this
scene. The dragon retains `SkyOnlyNoGroundOcclusion` for its distant sky probe;
the workshop's existing ground policy is unchanged. There is no new ground
geometry. Sun/BSDF MIS and the shared environment route remain responsible for
their respective specular contribution, with shared HDR postprocessing once.

The specimen-only empty-outside shortcut is disabled by this scene layout, so
secondary rays can hit the dragon and base. Cone growth uses a local derivative
estimate from the actual hit triangle's world-space vertex positions and
transformed interpolated normals, rather than a sphere radius. Barycentric
gradient magnitudes and normal normalization contribute to that estimate.
This is a first-order estimate: it is not a global footprint guarantee across
triangle edges or procedural relief. The existing material evaluator still
filters patterns in their authored object domain.

Native rendering adds one primary iteration per frame until the target (256 by
default). Camera, recipe, light, source and other shading edits reset history;
exposure keeps it. Headless captured frames converge independently. Save view
also writes the effective subject recipe beside the view and records its absolute
input path; source recipes remain untouched. Use the sibling material JSON when
moving that saved view to another machine and update its path accordingly.

Diffuse lighting still uses shared SH and direct sun, not diffuse GI. Repeated
specular paths remain bounded; this is not an unbounded reference path tracer.

## Verification

The current evidence is under `outputs/raytracing/dragon-sampled-20261107/`:
The [compact delivery packet](../evidence/raytracing-dragon-sampled-2026-11/README.md)
retains source and acceptance identities in the repository.

- All **18 focused RT tests pass**, including the existing workshop, glass,
  gallery, atmosphere refresh and failure/retry checks. The normal project-wide
  suite is not represented by this receipt.
- `qualification-final/receipt.json` records finite HDR for all ten opaque
  recipes, sample convergence, actual local hits, effective recipe/view roundtrip,
  rejected glass/configuration routes, and material-source preservation.
- The actual new scene dump matches the previous convex-slab scene's positions,
  normals, indices and instance world matrices exactly: two objects and 871,458
  triangles. The cached GLB is unchanged. No optical preparation or geometry
  repair occurs in this slice.
- A marble-subject roughness edit leaves all four evaluated primary base recipe
  channels byte-identical across **422** matching diagnostic base pixels, while
  changing the subject roughness. The effective edited view reloads to identical
  linear HDR output.
- The owned native exercise covers material/source switching, exposure, recipe
  reset, saved effective recipe, orbit, resize and restore. It finishes at 32
  primary samples, retains **three AS builds**, and records three target
  generations for initial creation plus two resizes. Exposure changes within
  the original history epoch. Each settled epoch reaches 32 recorded primary
  iterations in 32 frames; this is not a measured wall-clock presentation latency.
- `native-exercise-2/native-motion.mp4` records eight seconds of the actual
  1000×760 native app at 30 capture fps with a 32-SPP target. Movement resets
  accumulation. It is a native diagnostic recording with visible sampling noise.
- The previous legacy mirror view remains **PNG-byte-identical** to its capture
  before this implementation.

The fixed 320×240, seed-7 foreground HDR RMSE against the same estimator at 2,048
SPP is .22725 at 8 SPP, .11011 at 32, .05490 at 128 and .02383 at 512. Its
14,157-pixel primary-hit mask includes edges. These values demonstrate sampling
convergence, not physical ground-truth accuracy. Fine reflected detail remains
noisy at low sample counts.

At 128×96 and 512 SPP, default budgets report an event limit in 241 pixels at
least once; the deeper 12/64/256 control reports 37. Neither reports invalid-medium,
stack, radiance-call or total-call failures in that audit. Mean absolute default/
deeper linear RGB difference is 1.08e-7 over the full image, maximum .003285.
That aggregate result does not remove localized path truncation.

Three serial **Debug-build**, sync-validated 1080p pilots each retain 150 completed
GPU samples after 30 warmup frames, with one fresh primary iteration per frame.
Trace p50 is 1.645–1.646 ms; total-frame p50 is 2.231–2.233 ms and p95 is
2.254–2.257 ms on the RTX 5070 Ti. These GPU measurements do not include native
presentation latency or imply a fully converged frame at that rate.

Reproduce the asset-dependent checks explicitly:

```sh
rtk proxy uv run projects/raytracing/tools/qualify_dragon_materials.py --output outputs/raytracing/dragon-check
rtk proxy env CUBEY_RT_PRIVATE_XVFB=1 xvfb-run -a -s '-screen 0 1600x1200x24' python projects/raytracing/tools/check_material_window.py --scene dragon-materials --exercise --output outputs/raytracing/dragon-native-check
```

## Preservation and next gate

The original SDF/horse worktree is protected by its existing checksum manifest.
The implementation's starting candidate is retained under this evidence root's
`before/` tree and `before.json`, including the prior uncommitted scene work.
Canonical material files and the cached dragon remain unchanged. No commit,
push or public-site promotion is part of this slice.

The subsequent [commit review](../evidence/raytracing-dragon-sampled-2026-11/review.md)
records source acceptance separately from the precommit qualification above.

Next is the optical mesh gate: vertex links, robust self-intersection checks and
volume validity on the exact pinned mesh, followed by a separately identified
optical index stream if zero-area removal is sufficient. Clear glass then reuses
the same transport. GI, caustics, clouds and temporal reconstruction retain the
research plan's separate reopen triggers.

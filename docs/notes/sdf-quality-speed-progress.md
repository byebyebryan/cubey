# SDF quality/speed loop: execution record

Started 2026-10-29 from planning commit `42ae900d`. The
[research plan](sdf-quality-speed-research-plan.md) owns the experiment scope.

## Initial checkpoint

- Worktree clean on `sdf-tracing-comparison` before the loop.
- Existing native binary matches the previous measured SHA-256
  `865d909326f73267f79d7ebdd4f68cb6409d61ffb8f2be66f5639d608028a68d`.
- RTX 5070 Ti / driver 610.57.04 available. Shared encoder activity is present;
  do not claim idle-GPU evidence or stop external work.
- Raw evidence root: `outputs/sdf/quality-speed-loop-20261029/`.
- No default, authoring/history, optical, builder or window-admission change.

## Checkpoints

1. Matched endpoint timing and capture: initial batch complete; final-binary rerun pending.
2. Native diagnostic qualification and quality-gap attribution: bounded native-validated domain complete for ten paired views.
3. Candidate selection and bounded implementation: C/N and one combination implemented.
4. Reconstruction/history/resource qualification complete; final matched timing pending.
5. Decision receipt, preservation checks and local commits: pending.

## Measured endpoint checkpoint

`initial-timing-v2/timing.json` has 54 complete native runs, three repetitions,
180/30/150 frames and verified raw frame indices. Guardian sampled GPU p95 is
2.049–2.087 / 2.360–2.370 / 2.269–2.276 ms for preset/side/rear. Adaptive
analytical A+B is 17.734–17.930 / 19.403–19.482 / 15.374–15.507 ms. Shared
encoder activity remains; these are not idle-GPU or interactive results.

`endpoints/` retains uninstrumented native images and separate diagnostic
captures for both subjects at preset/side/rear/close/distant. A full-HD payload
uses 16,588,816 bytes plus 16,588,800 raster-UV bytes, below 32 MiB logical
storage. Geometry replay is compared inside a fragment wrapper to native
status/depth/normal functions using unchanged 4-epsilon/one-degree limits.

Qualification is explicitly limited to the scored pixel domain: isolated
cross-stage depth differences and one image-rounding difference are retained
as invalid pixels, with coordinates, validation colours and a validity mask.
Every scored pixel retains exact native-image parity. No depth/normal tolerance
is widened and no excluded pixel counts as a quality improvement. Silhouette
statistics additionally exclude a two-pixel neighbourhood of invalid samples.
The diagnostic is not a claim of universal or every-pixel native equivalence.

Two initial failed diagnostic attempts are retained and excluded: mutable
fragment storage required an unavailable device feature, and optional region
dependencies were incorrectly assumed on the sampled route. The final path
uses readonly fragment storage and declares the sampled field dependencies.
The first timing harness attempt used the wrong CSV column name; its single
completed run is excluded from the matched batch.

## Selected families and pilot

`candidate-selection.json` records ten endpoint comparisons. Normal tails and
early cached hits justify C (cached-hit refinement) and N (source normals).
Neither family can recover an unflagged cached miss. P and a new uncertainty
hierarchy are not added to this loop.

C keeps the original cached traversal as its anchor and uses local trilinear
gradient bounds to refine primary hits, with targets 0.05 h / 0.03125 h and a
64-iteration optional budget. Unresolved finite refinement retains the cached
approximation; a covered ray end can reject an earlier approximate hit.
Nonfinite refinement is diagnosed. Secondary tracers and their offsets stay
on the original sampled rule. The actual helper is exercised independently
by `cell-oracle/receipt.json`: 22 controlled observations and 2,560 independent
gradient checks pass, including first-zero brackets, inside exit, multiple
roots, endpoint/occupancy cases, nonfinite diagnosis and exact-budget fallback.
These are controlled quantized-cell checks, not global source equivalence.

N adds the existing root-far A64 region cache solely for six analytical normal
queries at cached hits; its zero-selection control uses the same fragment SPV
and cache with the normal branch disabled. Geometry/history formats and the
canonical field/region builders are unchanged. Native route metrics identify
every selected mode. All 166 preexisting SPVs still match their baseline bytes.

Pilot-only timings (12 frames, two warmup; not final acceptance) put Guardian
preset sampled/refine-0.03125/N at roughly 2.03/2.32/4.50 ms. The same view's
depth-error p95 drops from 0.300 to 0.015 pixel-footprint units with refinement;
Robot normal-error p99 drops from 9.52 to 2.30 degrees with N. Final full-view,
history, thin-feature, temporal and resource gates remain pending.

## Implementation freeze

Native binary `1c71ec2622022f2f56cd85a9e19b80b7290bc35c5b9408ac193fd1cd55e1f587`
contains the private candidates and diagnostic. The 166 original SPVs remain
byte-identical. Shared engine, canonical field/region builders, authored
geometry/history and material sources are unchanged. Twelve focused CTest
groups and ten actual negative CLI checks pass.

The combination uses source normals after cached hit refinement. It does not
change cached primary visibility or cached shadows into source geometry.
Source fixture contracts pass 155 independent checks. All cached variants
still miss the subgrid sphere, and cavity/front-surface errors remain.

Five routes pass nine history/publication-recovery actions, including undo,
redo, appearance reuse, failed publication and failed camera rollback. The
full-scene publication batch has three serial repetitions per subject/route.
Guardian cached rebuilds still take roughly 1.1–1.3 seconds; source-only edits
take roughly 160 ms. This loop does not solve edit responsiveness.

Temporal evidence now uses 384 native-validated per-pose geometry snapshots,
paired with the original uninstrumented images. Every pose retains field and
region generations. A preceding RGB foreground inference failed calibration
at 32 pixels and is excluded. Two failed descriptor/layout attempts are also
retained and excluded. The final diagnostic retains one UV resource slot and
drains the GPU per pose, below its 32 MiB logical payload cap. This capture
mode cannot be used for multiframe profiling.

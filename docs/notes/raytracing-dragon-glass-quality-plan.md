# Dragon glass: quality research and proposed G3 plan

Execution checkpoint, 2026-11-14: [Q0 measurements](raytracing-dragon-glass-q0-results.md)
are complete. The subsequent [Q0.1 correction](raytracing-dragon-glass-q01-results.md)
clears the recorded full-HD correctness hold with eleven passing captures,
including six independent 4096-SPP references. The
[bounded Q1 optical-normal experiment](raytracing-dragon-glass-q1-plan.md)
has now [rejected its first whole-interface fallback candidate](raytracing-dragon-glass-q1-results.md)
at the numerical-control stage. An analytical follow-up would study a
continuous interface/weighting model. Q2 filtering and Q3 moving quality remain
separate.

The subsequent [private visual comparison](raytracing-dragon-glass-normal-looks.md)
answers how noticeable the normal-policy change is on the actual dragon. Demo
appearance and practical value determine priority; the numerical experiment is
not a requirement to chase optical perfection before another rendering study.

Research checkpoint: 2026-11-08, `raytracing-first-pass-20261104`, reviewed local
commit `10328ef2dff12bd26b6ff20a589c47dde5e8eabc`. This is a proposal, with
read-only mesh analysis and reanalysis of historical native metrics. No new GPU
captures or quality/performance qualification were run in this research pass.
Renderer, model, materials and presets are unchanged.

Recommendation: make G3 a glass-quality pass before adding another lighting
system. First measure settled and moving quality separately; then test smooth
optical normals and procedural filtering as controlled experiments. Make
temporal reconstruction a separate decision once the measured motion gap is
known. Caustics remain a compelling later lighting study.

## Starting point and boundaries

The [first glass pass](raytracing-dragon-glass-first-pass.md) implements
full-scene reflection/refraction, repeated nonconvex entry/exit, Fresnel, TIR and
Beer absorption on the accepted certified dragon. The
[review fixes](raytracing-dragon-glass-review-fixes.md) close memory accounting
and preset-acceptance gaps. Those correctness gates stay in force.

Keep the accepted optical GLB, independent 30 mm convex marble slab, shared
clear atmosphere/sun and canonical glass recipe. Keep the original opaque route
and matched forward/SDF/RT material workshop. No ground plane, added props,
model search, repair, remeshing or material retuning is proposed here.

Three different mechanisms need separate evidence:

| Observation | Current mechanism | What would address it |
| --- | --- | --- |
| Faceted / ice-like refraction | Sharp geometric face normals define each optical interface | A qualified smooth-interface approximation, or different geometry in a later study |
| Noise while orbiting / after edits | Camera changes clear history; native rendering adds at most one fresh sample per frame | Better sampling per frame, a resolution tradeoff, or path-aware reconstruction |
| Unstable or blurred marble detail through glass | Scalar cone footprint, discontinuous triangle interfaces, plus sampling noise | Isolated footprint tests and a consistent normal-derivative model |

More accumulated samples can reduce sampling noise. They do not change the
triangle interface, certify the filter or add missing focused illumination.

## What this pass established

### Normals need an explicit policy

Fresh float64 analysis of the accepted serialized float32 GLB finds a median
adjacent-face angle of 2.74 degrees, p99 24.55 degrees and p99.9 77.66 degrees.
The interpolated centroid normal differs from the face normal by a median
1.62 degrees and p99 12.44 degrees. Forty-four stored corner-normal occurrences
across 43 faces are tangent to or opposed to their face normal. This describes
the attributes; the current glass renderer uses geometric normals instead.

An unoccluded triangle-centroid probe at the default camera, entering IOR 1.5,
finds 5,228 incident shading/geometric hemisphere disagreements among 408,501
front-facing centroids. Even after excluding those disagreements, naive smooth
reflection puts 4,951 directions on the wrong geometric side. No wrong-side
transmissions occur in this entering-only probe. Occluded faces are included;
these are neither visible-pixel error counts nor an exit/TIR qualification.

[PBRT's BSDF treatment](https://www.pbr-book.org/3ed-2018/Materials/BSDFs)
explains why shading and geometric hemispheres must remain distinct. Its
reflection/transmission classification is useful guidance, not a complete
smooth-dielectric implementation for this renderer. A normal substitution alone
would need additional grazing, exit, weight and continuity checks.

### Existing timing evidence is not a moving-quality benchmark

Code inspection confirms one native sample per frame and history invalidation
on camera changes. The historical native exercise targeted 32 SPP and mixed
camera, IOR, exposure and window-size changes. Reanalysis separates its glass
frames as follows; these are historical GPU trace times, not present FPS or
input latency:

| Phase | Frames | Median trace ms | p95 trace ms |
| --- | ---: | ---: | ---: |
| Reset, including non-motion edits | 129 | 4.48 | 17.85 |
| Adding samples to existing history | 259 | 4.93 | 10.30 |
| Settled, adding no samples | 906 | .0295 | .0411 |

Settled dispatches return cheaply after the cap. Averaging them with active
sampling would misrepresent cost. The exercise has no per-frame CPU/input
timestamps, uses multiple extents and predates the memory-accounting fix.
Raising the sample target helps a stopped camera; it leaves a continuously
changing camera with one fresh sample per frame under the current policy.

### Filtering and denoising are distinct investments

The current scalar cone has a Snell-aware angular update for a fixed flat
normal, with zero glass curvature. It is not a bound across triangle edges.
The earlier half-resolution/full-resolution comparison mixed filtering,
silhouettes and Monte Carlo variance and remains inconclusive.

[NVIDIA's refraction-cone publication](https://research.nvidia.com/publication/2021-08_refraction-ray-cones-texture-level-detail)
supports a compact refraction footprint model; its erratum explicitly warns
that the chapter and other cone implementations use different half/full-angle
conventions. [Igehy's ray differentials](https://graphics.stanford.edu/papers/trd/)
provide a first-order alternative that propagates reflection/refraction and
normal variation, with more state. Neither local method makes discontinuous
visibility or neighboring paths automatically smooth.

[NRD 4.18.0](https://github.com/NVIDIA-RTX/NRD/blob/abb00a7fb3535d969435f1b68c0c37e480ca88b6/README.md)
expects guides such as normals, roughness, view depth and motion; it explicitly
does not natively target transparency. The pinned
[RTXPT design](https://github.com/NVIDIA-RTX/RTXPT/blob/303b408ab81790a5051abd7573830b7a4112eb8e/Docs/Design.md)
decomposes delta reflection/transmission into up to three layers, with separate
NRD guides/instances. Its DLSS path combines guides and selects a dominant
layer for depth and motion, with acknowledged transparency limitations.
[Path-space motion research](https://cs.dartmouth.edu/~wjarosz/publications/zimmer15path.html)
explains the underlying ambiguity of an image seen through curved glass.

Cubey currently has raw progressive color/count history, without reprojection,
variance moments, motion vectors or layered specular guides. Integrating a
denoiser therefore needs a guide/history design, not just a final color pass.

## Proposed execution sequence

### Q0 — Establish the quality and cost baseline

This is the recommended **first bounded implementation loop**. Its deliverable
is a comparison and a decision, not a new renderer default.

1. Freeze the commit/model/recipe/view/environment/budget identities. Retain
   geometric-normal raw HDR as the reference mode. Use the canonical view plus
   an elevated view and a grazing/thin-feature view; keep exposure fixed.
2. Start with a 480p/540p pilot. Capture 32/128/512/2048 SPP and two independent
   reference seeds at 4096 SPP; extend to the existing 8192 ceiling only if the
   reference-to-reference difference still matters. Confirm the selected
   comparisons at 1920 × 1080. Do not blindly run the full matrix at full HD.
3. Report raw, unclamped HDR error, fixed-exposure display error and outlier
   percentiles. Separate body, head/thin features, refracted slab, directly
   visible slab and silhouette crops; exclude empty sky from foreground scores.
   Label rectangle crops honestly. A single jittered primary ID is not
   accumulated coverage; coverage masks need their own defined construction.
4. Use existing sky-only, sun-only and combined lighting controls to locate
   variance from the small bright solar source. Compare against independent
   reference seeds to estimate the reference noise floor. Same-seed sample
   prefixes are correlated and cannot alone establish convergence.
5. Record a repeatable native orbit–stop–settle sequence with camera/input
   timestamps, frame CPU/GPU/present timings, sample counts, history epochs,
   extent and current memory accounting. Separate setup, reset, accumulation
   and settled intervals. Report time to reach measured quality after stopping,
   moving quality and p50/p95 active cost separately.

Q0 acceptance: reproducible identities; finite output; zero invalid-medium,
stack and production-budget failures; preset/deep-reference agreement retained;
reference noise characterized or explicitly unresolved. Deliver side-by-side
captures, an orbit/settle recording and a ranked diagnosis. If even the raw
reference remains uncertain, stop before using it to approve another policy.

### Q1 — Test guarded smooth optical normals

If Q0 confirms faceting is a priority, add an opt-in project-owned optical-normal
policy alongside the geometric baseline. Keep it separate from shared material
recipes. Use a separate explicit configuration/uniform/history identity:
`environment.glass.w` already distinguishes legacy, optical spawning and the
dragon-hidden reference; treating it as a new policy ordinal could hide geometry.

Keep geometric normals authoritative for medium orientation, facing culling,
origin spawning and actual segment lengths/Beer absorption. Use existing smooth
attributes only for the candidate optical interface. Before choosing a Fresnel
branch, select one admissible interface normal and validate incidence and both
outgoing geometric hemispheres; a TIR case has only the reflected direction.
An inadmissible candidate needs a whole-interface fallback or checked adjustment
toward the geometric normal. Recompute Fresnel/TIR and weights consistently.
Do not discard or flip an already selected bad branch as an invisible repair.

This is a smooth-interface appearance approximation. It does not make the
underlying triangle geometry a curved physical surface. Report fallback rates,
critical-angle changes, reciprocity/energy limits, repaired-patch seams and
grazing continuity; hemisphere admissibility alone is insufficient acceptance.
An adjustment may introduce new directional discontinuities and must earn its
place against the retained baseline.

Qualify first on a planar slab, tessellated spheres at multiple densities,
entering/exiting critical angles and neutral IOR 1, then the dragon's head,
thin features and repaired neighborhoods. A flat equal-normal control should
retain its result; a sphere should approach the analytic control with refinement.
Use each policy's own high-SPP reference when measuring variance: changing
normals changes the mean image.

Normal variation also changes filtering. Q1 may use a controlled untextured
background to isolate direction, but it is not ready for textured-scene
promotion until Q2 supplies a matching derivative/footprint model. Leaving the
current zero-curvature assumption under varying optical normals is inconsistent.

### Q2 — Qualify procedural filtering through glass

Use a planar slab and sphere over a controlled pattern before the dragon/slab
crop. Define full/half angle and width/radius conventions explicitly. Separate
surface-interior footprints from silhouette and triangle-edge discontinuities.
Use deterministic/analytic footprint checks where possible and paired-seed image
comparisons with independent reference noise checks. A higher-resolution image
alone cannot isolate footprint bias from different sampling noise.

First evaluate the smallest cone correction consistent with the selected
normal derivative and local Snell conditioning. Include any view-dependent
normal adjustment in the derivative model, and label fallback discontinuities.
If controlled tests still show
meaningful anisotropy or curvature errors, prototype specular ray differentials
as a diagnostic reference, then measure whether they merit adoption. Four
additional vec3 derivatives represent 48 logical bytes per path before packing;
with the existing pending-path array, actual register/stack cost needs profiling.
That is a state-size estimate, not a measured GPU allocation or performance claim.

Acceptance: lower isolated detail error without unjustified blur, a reported
reference noise floor, finite grazing/TIR behavior and clean transport flags.
Retain unresolved edge exceptions instead of claiming a global conservative
bound. Recheck active GPU cost and memory after every retained candidate.

### Q3 — Choose the live-quality budget

After Q0 identifies the moving-image gap, compare 1/2/4 fresh samples per frame
and a render-resolution tradeoff at matched camera trajectories. Sample count
per frame is separate from the total settle target. Any extra Fresnel splitting
must retain estimator weights and record the additional query cost; solar
outliers must not be silently clamped to improve a score.

Choose a product target from measured quality/cost options. Full-HD 60 Hz
(16.7 ms) and 30 Hz (33.3 ms) are useful comparison budgets, not adopted promises.
If affordable sampling is sufficient, keep the simpler pipeline.

If temporal reconstruction is still needed, return a separate costed design:
primary versus reflected/transmitted guides, apparent motion through curved
glass, disocclusion/history rejection, resize/material/environment invalidation,
demodulation, buffer/VRAM costs and raw-mode access. Test reflection and
transmission together during orbit and sudden view changes. Missing guides,
ghosting or energy drift block promotion. NRD/DLSS selection is not made here.

## Promotion, stop conditions and later studies

Each stage is independently reviewable. Keep geometric raw rendering available,
retain the selected model/certificate and existing budget acceptance, and reject
any candidate that hides invalid status, uses a throughput clamp, truncates
accepted paths or invalidates memory reporting. New optical options must reset
history correctly and leave other scene modes unselected by default. Native
camera exclusion, edit/reload, resize and static AS reuse remain required.

Quality thresholds beyond those existing hard gates are proposed, not approved
numerical tolerances. Q0 should provide the evidence needed to choose them.
Tests should target hemisphere/critical-angle/footprint/history contracts rather
than encode one preferred screenshot. Visual review still owns appearance.

After G3, choose the next rendering study explicitly:

- **Focused glass caustics on the existing slab:** the most direct additional
  glass-lighting effect to investigate. It needs a separate transport/sampling
  plan; current straight Fresnel/Beer sun shadows do not focus light.
- **Diffuse GI:** broader lighting value, especially for enclosed scenes, but a
  larger estimator/cache/validation commitment for this open-atmosphere scene.
- **Shared clouds:** weather/reflection composition, with distinct volumetric
  visibility and temporal costs.

Dispersion, frosted interfaces, nested media, a new scene enclosure and geometry
subdivision remain outside this pass. Reopen geometry only if a controlled
normal experiment cannot meet the required appearance while retaining transport
validity; reopen lighting when the glass-quality comparison is ready to review.

[Research evidence and reproduction](../evidence/raytracing-dragon-glass-quality-research-2026-11/README.md)
retain fresh normal analysis, historical timing separation, source pins and
unchanged-runtime checks. The original research recommended Q0 first and
implemented none of Q0–Q3. The execution checkpoint above and the separate Q1
results now identify the current experiment boundary.

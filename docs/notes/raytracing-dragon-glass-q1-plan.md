# Dragon glass Q1 optical normal experiment

Execution checkpoint: [Q1 results](raytracing-dragon-glass-q1-results.md) reject
the first whole-interface fallback candidate at the analytical/GPU control
stage. Later dragon/native promotion stages were not entered. The contract
below records the experiment that produced that decision.

Proposed execution contract, 2026-11-14. The next pass asks whether guarded
smooth optical normals can reduce the dragon's faceted refraction while
preserving the qualified geometric boundary. Its deliverable is a measured
keep, revise or reject decision on one opt-in policy. A successful experiment
may advance to Q2 filtering; it does not change the default dragon preset.

## Starting point

The [Q0.1 correction](raytracing-dragon-glass-q01-results.md) clears the recorded
full-HD correctness hold: all eleven captures, including six independent
4096-SPP references, pass. Q0 separately established geometric faceting, solar
variance and one-sample moving history as different problems. This pass targets
the first of those. It does not promise to remove solar noise or improve moving
sampling through a normal change.

Work belongs in `/home/bryan/code/cubey-raytracing-first-pass`, on
`raytracing-first-pass-20261104`. Current HEAD is `10328ef2`, but the qualified
Q0/Q0.1 additions are uncommitted. HEAD alone is therefore **not** the baseline.
Before implementation, preserve a source/build/shader snapshot of the complete
qualified working tree and its artifact hashes. A commit can be reviewed
separately; execution must not depend on silently committing existing work.

Keep the certified optical GLB, its certificate, physical camera admission,
fixed tracing frame, projected crease offsets, marble support, shared recipes,
clear atmosphere/sun, exposure and raw sampling policy. The canonical transport
budget remains 128 events / 1024 radiance rays / 4096 total queries, cutoff zero;
the existing doubled-budget control remains a reference, not a new default.

The original opaque route and matched forward/SDF/RT workshop retain their
behavior. Generated slab/sphere controls are diagnostic assets, not additions
to the dragon composition. No remesh, subdivision, new props, material retuning,
GI, caustics, clouds, rough glass or reconstruction is included.

## Policy and integration contract

Add one project-owned `optical-normal` setting with `geometric` and
`guarded-smooth` values. `geometric` stays the default and preserves the existing
optical arithmetic path. Admit the nondefault value only in `dragon-optics`
and the explicit test harness. It affects sharp glass only; within the optical
scene, the choice can remain inactive while an opaque recipe is selected.
Metrics distinguish requested and effective policy. Unsupported scenes or
rough interfaces fail clearly. This is not a shared material recipe property.

Expose the same choice in the native glass controls, saved views and metrics.
It has an explicit versioned uniform field and history identity; switching it
resets accumulation without rebuilding BLAS/TLAS. Do not overload
`environment.glass.w`: it already distinguishes optical spawning and the hidden
dragon control. Account for any uniform/storage-size change in memory metrics.

For each actual dielectric hit, the first candidate works as follows:

1. Keep the outward geometric normal `Ng` authoritative for entry/exit, facing
   culling, medium identity, spawning and physical segment lengths/absorption.
   Its incident-side orientation is `G`. The current medium checks stay active.
2. Reconstruct the raw interpolated vertex normal using the actual hit
   barycentrics and inverse-transpose transform. Check finite/nonzero length
   before normalization and require outward agreement with `Ng` before
   orienting it to the incident side as `S`. Do not silently flip opposed asset
   attributes into acceptable candidates. The existing `s.n` has already been
   reoriented for shading, so it cannot by itself supply this audit.
3. Require `dot(-incoming, S)` above the candidate incidence guard. Evaluate
   Fresnel, TIR and the possible reflected/transmitted directions about `S`,
   using the current radiance-mode eta convention.
   Validate the geometric side of **every positive-weight possible branch**
   before drawing the branch sample or queuing a split reflection. With rays
   pointing away from the hit, reflection must have positive dot with `G`, and
   transmission negative dot. A TIR interface has only reflection to validate.
4. If any candidate condition fails, select the entire geometric interface and
   recompute the complete direction/Fresnel/TIR tuple about `G`. Both early
   split branches and later stochastic branches use that single choice.
   IOR 1 remains exactly straight in the local interface calculation.
5. Sample with the selected interface's own Fresnel masses, eta scaling and
   existing random dimensions. Never repair a selected branch by flipping its
   direction, dropping its positive weight, changing its medium or retrying a
   different normal after selection.

Use one named, recorded numerical guard for candidate hemisphere tests, fixed
after the synthetic tests and before dragon comparisons. It is an implementation
tolerance, not an artist control. The geometric fallback keeps its established
numerical policy. Guard sensitivity is part of the evidence, not a way to tune
the screenshot after qualification.

Whole-interface fallback is the only first candidate. Do not add an iterative
normal-adjustment solver, per-face smoothing controls or a menu of heuristics
in the same loop. If fallback seams defeat the purpose, return that result and
a bounded proposal for a second candidate.

The existing straight Fresnel/Beer sun-shadow approximation continues to use
geometric normals. This experiment changes camera/radiance-path interfaces;
it does not establish a reciprocal light-path or caustic transport model.

## Physical interpretation and filtering boundary

Smooth normals approximate the appearance of a curved interface while the hit
surface stays triangulated. [PBRT's BSDF treatment][bsdf] explains why geometric
and shading hemispheres must remain distinct. A geometric-side guard follows
that distinction, but is a Cubey candidate rather than a complete algorithm
provided by PBRT.

Normal substitution changes scattering symmetry and can violate conservation;
direction admissibility alone is insufficient. [Veach's transport research][veach]
and [PBRT's shading-normal analysis][adjoint] require keeping radiance and
importance conventions distinct. Retain Cubey's camera-path radiance convention
and transmitted eta-squared factor. Do not apply an importance-path correction
as an unexplained brightness fix. Measure reverse-path discrepancies and
energy behavior, including cases where the reverse ray selects a different
fallback. This policy is not claimed to be a physically reciprocal curved solid.

The current cone update uses a fixed flat interface and zero optical curvature.
Changing only its cosine to the smooth normal would still omit normal
variation. [PBRT's specular differentials][differentials] explicitly carry those
derivatives; [NVIDIA's refraction-cone erratum][cones] also requires consistent
angle conventions.

Accordingly, Q1 appearance decisions use controlled unfiltered analytic signals
and dragon comparisons with the procedural marble pattern and bump disabled
for **both** policies. The canonical textured marble scene is also captured,
but labeled a filtering-unqualified integration preview. Its detail cannot
approve the new normal policy. Existing cone results remain diagnostic only
on the smooth route. A retained policy needs Q2's matching normal-derivative
and footprint work before textured-scene/default promotion.

## Execution and evidence

### 1. Establish interface controls

Build a small project-owned CPU float64 oracle and a bounded GPU interface
probe. Exercise both branches regardless of the stochastic choice. Pin the
same eta convention, normal orientation and policy version in both outputs.

| Control | Required observation |
| --- | --- |
| Planar slab, equal normals | Existing directions, Fresnel, radiance weights and IOR-1 behavior are retained within declared float32 tolerances |
| Tilted, opposed, near-zero and invalid normal inputs | Candidate rejection occurs before sampling, with the complete geometric fallback and a reason code |
| Entering/exiting angle sweeps, IOR 1 / 1.1 / 1.5 / 2 | Snell/Fresnel agreement away from the critical band; explicit adjacent float32 cases around grazing and TIR |
| Fixed-interface reversed direction | Direction reversal and eta-scaled transport follow the analytic convention; reselected-fallback differences are reported separately |
| Two-triangle seam and repaired-patch fixtures | Direction/Fresnel changes across the seam and fallback boundary are measured; no silently discarded branch |
| Tessellated spheres at multiple densities | Compare central ray directions against an analytic sphere; report direction error separately from polygon silhouettes and finite intersection differences |
| Lossless slab/sphere under constant radiance | Report complete-path radiance and truncation; do not mistake an individual eta-scaled radiance weight above one for energy creation |

Declare angular, Fresnel and weight tolerances using the geometric/equal-normal
float32 controls before comparing smooth dragon images. Record critical-band
classification separately instead of using one permissive tolerance everywhere.
Measure the selected field's flux/Jacobian behavior with incident-direction
integration; `F + (1-F) = 1` alone is not an energy-conservation proof. Numerical
offline integration is sufficient for that diagnostic and does not select a
runtime derivative implementation. If a reliable smooth-field energy
characterization cannot be established, stop at a diagnostic result rather than
calling the policy qualified.

### 2. Compare on the dragon

Start at 960 x 540 with the canonical view, then extend a surviving candidate
to the elevated and grazing views from Q0. Compare both policies at 128/512 SPP
and their **own** independent seed-19/43 4096-SPP references. Start with sky-only
lighting and pattern/bump disabled. Restore combined sky/sun in the canonical
view to measure solar variance separately; expand that image comparison only
when it changes the decision. Reuse captures only when the full
source/build/model/config identity and needed outputs match.

Assess head/horns/thin features, body, silhouette, repaired neighborhoods and
the slab seen through glass at fixed exposure. Keep raw unclamped HDR and
display crops. Whole-image error between the policies measures a changed image,
not correctness: their limiting images differ. Sampling error is measured
against each policy's own independent references. Retain reference-to-reference
noise; extend to 8192 only when uncertainty affects the decision, otherwise
report it as unresolved.

Collect all-boundary smooth-use/fallback counts by reason, entry/exit, TIR and
path depth, plus primary-hit maps and repaired-face group statistics. State the
denominator: an interface-event rate is not a visible-pixel percentage. Keep
diagnostic storage within the existing readback cap using bounded pilot maps
or chunks and host-side wide reductions. No unbounded all-ray journal or new
ordinary-frame global atomic counter is required.

If the synthetic and pilot gates pass, confirm the isolated comparison at full
HD and run the existing eleven-capture Q0.1 transport matrix for the new policy
at the unchanged budget, including both independent reference seeds. Recheck
the geometric route with the normal build. New normal directions are new paths:
old clean captures cannot establish their transport validity. An exact Q0.1
pixel may no longer follow the same path under the candidate; retain the
geometric replay and whole-image cumulative checks as separate evidence.

### 3. Verify integration and return a decision

Run the existing optical/opaque qualifiers and non-optical controls. Test native
policy switching, reset/history identity, save/reload, recipe changes, camera
exclusion, orbit/dolly and resize. Geometry/AS counts must not change when only
the policy changes. Record a slow orbit with matched extent, exposure and fresh
SPP per frame to inspect fallback popping; it is not a moving-noise solution.

Measure active GPU trace cost, query counts, pending-path peak and requested
memory against the corrected geometric baseline, separately from setup,
settled dispatches and Xvfb presentation. Use repeated matched active runs;
report distributions and diagnostic overhead. No desktop FPS target or allowed
percentage cost increase is adopted by this plan.

Deliver a report with an A/B wipe, identical crops, slow-orbit recordings,
fallback overlays, analytic controls, raw reference uncertainty and cost. Keep
the current default and review one of three outcomes:

- **Retain for Q2:** visible faceting improves in the isolated controls and
  dragon crops, hard transport/integration gates pass, and the remaining seam,
  energy, reverse-path and cost limits are explicit and acceptable for an
  opt-in appearance experiment. Textured/default promotion remains held.
- **Revise the candidate:** guards are safe but fallback seams/popping or cost
  defeat the appearance gain. Preserve the evidence; propose one next policy
  without expanding this loop into an adjustment-algorithm search.
- **Reject or leave inconclusive:** positive branches are lost, transport or
  analytic gates fail, a physical-behavior claim cannot be supported, or noise
  prevents a defensible comparison. Retain the geometric renderer and state
  the smallest unresolved question.

Any new medium miss, budget/stack exhaustion, nonfinite output, geometry or
shared-consumer regression blocks retention. Record the first reproducible
failure with the existing journal tools. Do not make the matrix pass by raising
the default budget, increasing spawn offsets, hiding flags or clamping samples.
Completing the experiment can legitimately mean rejecting the candidate.

## Implementation boundaries and follow-up

The likely implementation touches the RT config/native controls, an explicit
uniform/history identity, a project-owned interface helper, its workshop call
site, bounded diagnostics and targeted numerical/qualification tools. The
geometric spawning helper and certified asset preparer are outside that scope.
Default-off and non-optical checks must be meaningful; new tests cover the
interface contract rather than locking a preferred screenshot.

Q2 owns consistent varying-normal filtering, including derivatives through
any fallback and its discontinuities. Q3 owns fresh samples per moving frame,
render resolution and any temporal guide/history design. Later lighting work
can revisit focused slab caustics, GI and clouds after those decisions.

This planning pass adds no runtime implementation, GPU qualification, commit,
push or public-site publication. The [research record][research] pins the
inspected working-tree inputs and primary-source scope.

[bsdf]: https://www.pbr-book.org/3ed-2018/Materials/BSDFs
[veach]: https://graphics.stanford.edu/papers/non-symmetric/gamma-fixed/
[adjoint]: https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/The_Path-Space_Measurement_Equation
[differentials]: https://www.pbr-book.org/4ed/Textures_and_Materials/Texture_Sampling_and_Antialiasing
[cones]: https://research.nvidia.com/publication/2021-08_refraction-ray-cones-texture-level-detail
[research]: ../evidence/raytracing-dragon-glass-q1-research-2026-11/README.md

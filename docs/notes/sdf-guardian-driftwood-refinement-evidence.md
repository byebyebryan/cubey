# Guardian and driftwood refinement evidence

Date: 2026-10-22

Status: implementation in progress under the
[refinement plan](sdf-guardian-driftwood-refinement-plan.md). The first-pass
scenes, presets and published four-subject gallery remain intact.

## Curve comparison and shared correctness

`outputs/sdf/refinement-curves-final/` compares the same horn and trunk with
linear versus monotone Hermite radius interpolation, small segment blends
(0.0012 and 0.006), and a denser hard-union chain. Native views retain fixed
field, camera and lighting settings; direct-expression strips are recorded
separately. More blending produces visible corrugation, especially near narrow
tips, and is rejected. Smooth width interpolation and a modestly denser hard
chain are selected. They improve the approximation but do not make an exact
curve sweep; rear/grazing views still expose some segment transitions.

The small-blend fixture exposed a CPU/GPU part-owner mismatch at a near tie:
CPU part 7 versus GPU part 5, with no reported distance or material-color error.
Smooth operations previously switched owner at `h < 0.5`, while hard operations
already retain the first owner within a 1e-6 world-distance band. Smooth union
and subtraction now use that same ownership rule on CPU and GPU. Distance and
continuous material interpolation are unchanged. Validation still requires
exact part agreement and its original distance/material tolerances; error
messages now identify the disagreeing parts and numerical errors.

Focused CPU tests cover both sides of the smooth-union and smooth-subtraction
tie band, plus a point clearly outside it. The previously failing native horn
fixture passes after the fix. All six selected authored/recipe/build/layer
checks pass. `outputs/sdf/refinement-foundation/` retains the complete native
authored regression run, including primitives, tapered shapes, smooth/cut
materials, lifecycle, maximum capacity, legacy scenes and export/reload.

## Progress

- [x] Preserve original inputs and compare curve treatments.
- [x] Fix and qualify the exposed smooth-operation ownership instability.
- [x] Finish guardian native refinement and intent checks.
- [x] Finish driftwood native refinement and intent checks.
- [ ] Qualify both construction histories and publish matched comparisons.

Artistic assessment and user acceptance will be recorded separately from the
native correctness results.

## Modeling checkpoints

The opt-in `author_refinements.py` emits separate `sculpture-*-refined` scenes
and static/build presets. All original blockouts and finished scenes, plus
the two refinements, reproduce byte-identically; the receipt is
`outputs/sdf/refinement-reproduction.json`. No original scene/preset changed.

`outputs/sdf/refinement-audition-final/` retains two clay form variants per
subject, with front, hero, profile and rear views and exact source inputs.
The restrained guardian cheek treatment (A) retains more quiet face area and
is selected. The compact wood fork (A) separates its rear limb more clearly
than the leaning treatment (B). Earlier native revisions remain under
`outputs/sdf/refinement-audition/` and `refinement-detail-{1,2}/`.

The guardian now has resolved socket rims, cheekbones, muzzle/lip and nose
wings, raised brow folds, inner ears, longitudinal horn flutes, neck/shoulder
structure and a few chips/cracks. The correction pass restores readable
nostrils after a first placement was too deeply buried. It uses 108 leaves /
215 instructions. Seven saved-volume connection/socket/chip checks pass.

Driftwood uses fewer dominant limbs, a rearward turn, oblique broken ends,
broader worn planes, an elongated knot, two hollows, a real longitudinal split
and grain extending to the side/rear. The correction pass trims the rear limb
and shortens a rear scoop that cut too close to that junction. Its retained
rear-limb probe now has over 0.021 workspace units of interior clearance.
It uses 100 leaves / 199 instructions. Nine design checks pass, including
solid-before/empty-after samples at the hollow and split. The split's front
wall sample changes from -0.002384 to +0.009548 signed distance.

`outputs/sdf/refinement-preflight/` records successful final native ray and
CPU/GPU distance/material/part checks. The per-subject full history matrices,
performance and films are the next qualification checkpoint; these preflight
captures alone do not establish construction acceptance.

## Construction visibility correction

The first full guardian capture passes all native prefix checks but stops at
the preview-visibility gate. Its retained attempt is
`outputs/sdf/refinement-guardian-preview-attempt/`. Native on/off comparisons
identify the far ear and eight rear-neck segments as fully occluded from the
front build camera. These have individual reasons in
`sculpture_refinements.json`; opposite/profile/rear inspection remains required.
The analogous driftwood preflight finds all four side-grain previews visible,
while six rear-grain segments are occluded and explicitly documented.

The same check reveals that the original jaw-chip cutter was buried. It is
moved onto the exposed jaw edge rather than exempted. An independent sample
changes from -0.005944 before that cut to +0.009720 afterward, with a continuous
empty path to the exterior. The revised native cutter preview is visible.
These corrections retain the visibility threshold and original ray/material
tolerances. The full guardian capture is rerun against the corrected scene.

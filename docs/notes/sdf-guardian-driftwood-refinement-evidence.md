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
- [ ] Finish guardian native refinement and intent checks.
- [ ] Finish driftwood native refinement and intent checks.
- [ ] Qualify both construction histories and publish matched comparisons.

Artistic assessment and user acceptance will be recorded separately from the
native correctness results.

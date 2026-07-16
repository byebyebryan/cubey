# Mountains-Guided Terrain Source Correction

Date: 2026-07-15

## Decision

Use the external ShaderToy Mountains height field as the next mountain
morphology target, not as production source code and not as evidence that it is
categorically better than every other reference. The shared GUI comparison did
not establish a decisive visual winner, but Mountains remains the most useful
global baseline: it is coherent, unmasked, compact, and already connected to an
exact-source diagnostic harness.

Keep source v2.1 frozen as the production control. Build one corrected
clean-room candidate inside `projects/terrain/source_study`, review it through
the accepted cached-backdrop renderer, and discuss production promotion only
after the fixed pack passes. This batch does not add a production source
version.

## Why The Existing Study Is Insufficient

The current `mountains-signed` recipe preserves the general ideas of broad
modulation and alternating signed octaves, but changes their relative scale too
aggressively. Its primary period is 14 km and its remote uplift period is about
168 km. Across the 32.768 km backdrop domain, that remote term barely develops
the sparse high masses visible in the direct reference.

The reference couples three operations much more tightly:

1. a broad coherent field controls the amplitude of the structure chain;
2. one transformed coordinate chain accumulates alternating positive and
   negative octaves;
3. a sparse low-frequency uplift term adds occasional major summits at a scale
   still represented inside the viewed terrain domain.

The correction must preserve those relationships in world units while using
Cubey's shared noise, seed derivation, footprint filtering, and independently
selected constants. It must not reproduce the reference hash, matrix,
coefficients, source text, rendering, or camera path.

## Candidate Contract

Add `mountains-hierarchy-v2` as a study-only recipe with:

- one deterministic world-space coordinate chain;
- a broad amplitude envelope that directly modulates the signed structure;
- alternating signed octave accumulation with scale plus rotation per octave;
- a sparse uplift field with a period represented several times in the fixed
  review domain;
- footprint filtering for every octave and neutral fallback for unresolved
  detail;
- one calibration over seeds `0`, `9012`, and `12345`, identical to the other
  study recipes.

The candidate may expose envelope, signed-structure, uplift, and final-height
components in reports. Those are observations of one coherent composition, not
independently placed terrain features. No radial focus, centered mask, ridge
line, valley curve, erosion, hydrology, or candidate-specific camera/material
is allowed.

## External Reference Diagnostics

Extend `terrain_shadertoy_ref` with audit-only Mountains scale-decomposition
views derived from the baked exact height:

- `envelope`: a broad low-pass height proxy;
- `structure`: signed height residual relative to that envelope;
- `uplift`: positive broad residual relative to a wider low pass;
- `height`: final terrain height.

These are spatial diagnostics, not reconstructions of private terms inside the
reference function. The adapter must not duplicate the reference coefficients
or formulas. Their purpose is to measure where broad mass, intermediate form,
and positive summit buildup live in the final field.

Capture the same component set at reference times `0`, `20`, and `40`. Time is
used only to select translated camera-centered windows from the stationary
global field. The component pack records external source hashes and remains
under ignored `outputs/`; no reference source or generated SPIR-V is vendored.

## Acceptance Gate

Review top height and slope first, then common clay views at three azimuths for
all three fixed seeds. The candidate is eligible for a later production-v4
batch only when at least two seeds show:

- broad connected mountain mass and useful valley contrast;
- ridges with terrain-scale body rather than narrow fins;
- visible buildup from range to ridge to summit;
- multiple major masses across the 32.768 km domain;
- no dominant grid, diagonal, contour, focal-mask, or repeated-template
  artifact.

Record calibration, relief, slope, scale response, and source throughput, but
do not reject the candidate on source evaluation cost alone because the target
backdrop product bakes at setup. Rendering topology, materials, vegetation,
erosion, and the runtime `<1 ms` gate remain separate follow-on work.

## Stop Condition

This batch ends with a documented visual verdict. A weak candidate stays in the
study. Do not tune it indefinitely and do not add `TerrainSourceVersion::V4`
merely because the implementation is complete.

## Completed Review

The fixed pack is under
`outputs/terrain/mountains-source-decision-v2/`. It compares production v2.1,
the old `mountains-signed` abstraction, and `mountains-hierarchy-v2` across
seeds `0`, `9012`, and `12345` at six yaw directions. It also records exact
Mountains height decompositions and oblique views at times `0`, `20`, and `40`.

The corrected candidate is a meaningful improvement over `mountains-signed`:

- mean slope rises from the old candidate's `0.10-0.28` to `0.54-0.73`;
- p95 one-kilometer relief rises from `252-602 m` to `1,324-1,662 m`;
- all three seeds contain several complete masses and remain stable through the
  unrestricted six-angle silhouette review;
- source throughput remains about `6.2-6.4 million` samples per second in the
  report, roughly twice the v2.1 control.

It still fails the promotion gate. Height views are dominated by rounded broad
masses, slope views form rings around those masses, and clay silhouettes read
as hills or smooth ranges with occasional isolated summits. They do not build a
reliable range-to-ridge-to-summit hierarchy in two seeds. Production v2.1 is
too slope-heavy (`1.39-1.43` mean slope and roughly `2.4-2.6 km` p95 local
relief), but remains notably stronger in summit and ridge readability.

The exact-reference pack also prevents a false target. Its broad envelope and
positive uplift are persistent, but the oblique result changes substantially
between translated windows: time `0` is subdued, time `20` is jagged, and time
`40` is dominated by one broad wall. Mountains is useful evidence for coupled
scales, not a uniformly successful global field that Cubey should match
literally.

**Decision:** keep `mountains-hierarchy-v2` as a rejected study result, retain
v2.1 as the production control, and do not add source v4. Further mountain
source work needs a different structural operator, not another constants pass
over this signed-octave composition.

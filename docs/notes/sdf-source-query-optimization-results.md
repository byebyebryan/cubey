# SDF source-query optimization results

Date: 2026-10-29. Contract: [plan](sdf-source-query-optimization-plan.md).
Measured implementation: `b942d6bb`, local `sdf-tracing-comparison`.
Final binary SHA-256: `aace33001393abfa94a20c073e51ca2a3cb7577cfdd038cee031eaaef914c5f2`.

## Decision

**Adopt A, zero-contribution shadow culling, on the eligible automatic source
route. Retain B, prepared rounded-cone values, and A+B as explicit studies.**
Both independent trials and the measured combination pass the proposed
gain/regression gates. The combined rear view meets the 16.7 ms GPU p95 target;
preset and side remain above it. The loop is complete without expanding into a
new stepping algorithm, finer pruning, GPU construction or lower resolution.

Sampled/full remains the default tracer/evaluator. Root-far remains a static
headless study. These measurements do not establish full interactive FPS or
windowed root-far support.

## Final matched measurements

Ranges of three whole-scene GPU p95 results, in milliseconds, at 1920×1080,
supersample 1, analytic/pruned A64 root-far, on RTX 5070 Ti / driver 610.57.04.
All four controls use the final binary, unchanged scenes/cameras and quality.

| Guardian view | Current reference | A: shadow | B: prepared cones | A+B |
| --- | ---: | ---: | ---: | ---: |
| Preset | 21.56–21.66 | 19.32–19.58 | 20.05–20.36 | **18.32–18.37** |
| Side | 23.09–23.26 | 21.80–21.91 | 21.92–22.03 | **20.74–20.86** |
| Rear | 21.05–21.14 | 17.23–17.47 | 19.63–19.66 | **16.14–16.41** |

The final batch contains 72 interleaved serial runs / 10,800 completed GPU
timestamps. Earlier independent A and B batches contribute another 72 runs /
10,800 timestamps. Each run has 180 frames, 30 warmup and 150 measured samples;
the archive verifies every measured frame index, including the submission tail,
and reproduces each p95 from raw samples. No owned build/test/capture overlaps
timing. GPU/encoder observations are shared-host boundary snapshots.

The independent A and B batches record 0% encoder use at all boundaries. The
final four-way batch records **8–50% shared encoder activity**, with similar
mode distributions (means about 28–30%). Its gains remain consistent and larger
than observed reference variation, but the final ranges are shared-load results.
Combined performance on an idle GPU is not qualified by this pack. No external
encoding process was stopped or reconfigured.

The multi-material Robot stays on its original lit/general-material pipeline.
Its three views show no meaningful regression. The proposed gate requires a
repeatable gain of at least 0.5 ms and 5% in a Guardian view, larger than observed
reference variation, and rejects a view regressing by both over 0.5 ms and 3%.
All three candidates pass. Gains are measured directly, not added from separate
trials or compared only against the older approximately 51 ms shader.

## What changed and what the controls mean

- `--source-optimization automatic`: A on opaque atmosphere, debug 0, exactly
  one published registered material and enabled material shortcut; reference
  otherwise. This applies to already opt-in analytic/hybrid source tracing.
- `reference`: the previous automatic shader, including its existing lit and
  eligible material specialization. This is the rollback/performance control.
- `shadow`: explicitly requests the same A candidate and conservative fallback.
- `prepared-cones`: B-only, static headless analytic/pruned A64 root-far atmosphere.
- `combined`: A+B within that same explicit study boundary.
- `--source-shaders general`: the original complete, unculled and unprepared
  source shader. It remains a strict diagnostic control. Explicit incompatible
  optimization requests are rejected.

Optimization mode is fixed for a renderer lifetime. Each source renderer keeps
three prebuilt compatible pipelines, selecting by published material state and
current debug/shortcut settings. Edits do not compile pipelines. Sampled and
retained diagnostic routes keep their original behavior and allocation boundary.

A computes the final bumped normal before deciding whether direct lighting
needs visibility. Exactly zero finite light cosine skips the shadow query;
nonfinite inputs retain it. Shadow origins and AO retain the geometric normal.
Ambient, emission and existing sampling/tolerances are preserved.

The exceptional diagnostic change is explicit: a skipped unused shadow cannot
exhaust. Required-query exhaustion still propagates. The original general
shader retains the old behavior, including magenta if an otherwise unused
shadow exhausts. Ordinary admitted captures remain byte-identical for A.

B stores slope, radial factor and cap/body threshold in a disposable companion
buffer, indexed by original source instruction identity. Its per-entry format
tag identifies version 1; unsupported entries retain canonical evaluation.
Geometry identity, source order/count and the fixed renderer format determine
reuse. Material-only changes reuse the cache; geometry/history changes prepare
a matching generation. Authored bytes, serialization, CSG and the independent
CPU evaluator are unchanged. The [operation/guard audit](sdf-source-query-optimization-progress.md#b-numerical-and-certificate-audit)
explains why no arithmetic guard or tolerance is widened.

## Correctness and lifecycle evidence

The final automatic binary passes 129 native jobs spanning all ten existing
source configurations, Guardian/Robot full-HD views, multiple materials,
optical/debug fallbacks, and analytic/hybrid action replays. Earlier appearance
and edge suites cover procedural/bump/domain edits, clay, AO/ground, soft shadows,
grazing light and near-surface/inside views; their shadow shader bytes are
unchanged in the final build. A controlled GPU fixture verifies 14 required/
skipped exhaustion, nonfinite and geometric-origin observations.

The frozen 155-check independent query suite passes across all ten retained
routes and separately with B and combined active. Prepared query receipts
identify and hash the actual prepared compute kernel. Another 21 analytic
checks per mode cover equal-radius capsule equivalence, tangent brackets,
domain clipping, inside exits, rotation/scale, zero endpoint radii and adjacent
float values at containing-sphere boundaries. Existing tolerances are retained.

The separate primitive probe covers 82,388 point/stencil samples, including
51,792 Guardian samples. Prepared GPU values stay within 2.39e-7 of the unchanged
CPU reference, and the maximum Guardian normal difference against the original
GPU evaluator is 0.184 degrees, below the frozen 1-degree contract.

B changes 96 / 185 / 79 pixels in the three Guardian 1080p captures, each by at
most one 8-bit output level. Combined is byte-identical to B in all six
Guardian/Robot views and six additional cone lighting configurations. The
original images and differences are retained; no goldens or geometry/normal
tolerances were relaxed. These observations are bounded qualification, not
universal bit identity across scenes or GPU implementations.

For B and combined, 15-action publication replays match six freshly created
native states. Five injected failures cover preparation, upload, appearance
upload, descriptors and activation. Failed updates retain old pixels exactly.
Source reordering, geometry, undo/redo, registration and debug transitions keep
cache identity and shader selection coherent, with three pipelines throughout.
Actual authored-history prefix replays add empty/first/second, undo, redo,
branching, appearance reuse and failed-prefix retry in all four modes.

A real Xvfb window on the admitted ordinary analytic/pruned A16 route survives
resize/recreation, camera input, debug switching, rejected reload, retry and
material editing. Restored/retained model pixels match exactly, with zero field
builds and three source pipelines per creation. This is native virtual-window
evidence, separate from physical/human acceptance and from root-far timings.

## Setup, memory and editing costs

Guardian's 2,021-instruction source uses a 32,336-byte cone table. Full-Guardian
replays measure 64,672 bytes for overlapping device generations, including a
discarded candidate, and 32,336 bytes of staging payload. CPU table copies,
program/appearance staging, descriptor and driver allocations are outside those
cache-specific figures; they are not total VRAM measurements. Reference, sampled
and shadow-only paths allocate no cone buffer, verified from native metrics.

One scripted full-Guardian replay per mode places successful geometry
publication at roughly 139–144 ms; appearance-only updates are roughly 10–12 ms.
This is descriptive edit-latency evidence, separate from repeated GPU timings.
The derived cache does not eliminate region rebuilding. Candidates stage their
complete program/cache/appearance/descriptor generation before queue retirement
and activation, preserving the old generation on failure.

The three source pipelines take about 36–43 ms to prepare in the final warm
timing runs. Opaque driver reuse is uncontrolled; this is not a cold-start or
persistent-cache result. Startup caching remains deferred.

## Preservation, review and next boundary

All 116 pre-existing SPIR-V files and 155 protected content/fixture files match
the frozen baseline. Canonical source/history/field/region/bounds CPU files and
shared engine APIs are unchanged. Twelve focused SDF/shader/documentation groups
pass. Source, compact receipts, native images and raw GPU samples stay local;
there is no push, default-tracer switch or public media/site publication.

Rejected harness assumptions are recorded in the
[progress receipt](sdf-source-query-optimization-progress.md): invalid camera
distances were replaced by admitted enclosing geometry; native pointer/keyboard
effects were corrected; copied fixture history was fixed; and prepared-state
publication parity was separated from expected original-GPU float differences.
A's exact-parity requirement and all frozen numerical contracts remain intact.

The remaining worst view is side, about 20.8 ms with the combination. A separate
guarded-step investigation is the next decision if primary-query cost remains
the focus. Finer pruning requires its own memory/build budget. Neither starts
automatically, and windowed root-far admission needs a separate contract.

[Archived receipts, samples and reproduction](../evidence/sdf-source-query-optimization-2026-10/README.md).
Raw host pack: `outputs/sdf/source-query-optimization-20261029/`.

# SDF tracing, region programs and history: comparison result

Date: 2026-10-27. Local source candidate: `cea1f1289173312e779783e4b51b6d38ed2a856e`.
The [bounded comparison plan](sdf-tracing-comparison-plan.md) has been exercised
through its decision gate. The default remains sampled/full. No push, public
media refresh, content revision or default adoption is part of this result.

## Decision supported by this pass

Retain the complete authored source and history as authoritative. Spatially
reduced CSG programs are useful disposable acceleration data. Source evaluation
does recover geometry that sampled reconstruction loses, and undo does not
require retaining a complete brick field for every historical state.

Do not select this prototype hybrid path as the new default. It recovers the
quality benefit remembered from projectR, but its retained full field adds
memory and rebuild cost without beating the analytic candidate in these runs.
The analytic candidate is a useful opt-in quality/memory alternative on Robot.
Guardian exposes a remaining rendering/build tradeoff that needs diagnosis.

The recommended next bounded investigation is Guardian query work: separate
primary, normal, material and shadow costs, and measure the program lengths
actually encountered near its surface. Then choose between GPU region
construction, using those programs during brick construction, or a cheaper
hybrid cache. Faster CPU/GPU construction alone will not fix the measured
Guardian frame cost. More authored content would not resolve that uncertainty.
This is a recommendation, not an adopted next implementation goal.

## What was implemented

| Route | Surface/query data | Geometry resources |
| --- | --- | --- |
| S0, default | Existing sampled brick tracer and full authored builder | Existing paired field, unchanged |
| A0 | Full authored source, analytic finite-difference normals | Program only; small-scene reference |
| H0 | Sampled steps when their error bound permits; source hit/normal evaluation | Existing field plus full program |
| A1 | Exact regional branch pruning of the source program | Region headers/references; no field or atlas |
| H1 | Same qualified region programs with the hybrid stepping rule | Regions plus existing field |

Native and compute-query routes share the tracing includes. The source hit
residual is `h0/32`, with `h0=1/(256*7)`, rather than S0's sampled hit threshold.
Full/pruned geometric normal stencils use `h0/16`. Appearance continues to use
the complete authored material evaluator, including inherited cut ownership.
Distance pruning is not assumed to preserve a reduced material expression.

Hybrid uses the trilinear 1-Lipschitz interpolation bound, an R16 rounding
allowance and an authored arithmetic guard. It evaluates the source when that
bound does not support a positive step. It does not force a minimum sampled
step through uncertainty, or treat a uniform-cell label as an exact distance.
This is deliberately more conservative than projectR's empirical switch.

The region builder is a synchronous **CPU study implementation** with GPU
evaluation. Hierarchical 4/16/64 grids inherit reduced postfix references,
preserve operator grouping and propagate subtraction complements. Dominance
requires separation beyond `2R + smooth radius + arithmetic guard`. It does
not replace far regions with approximate constants. Root clipping stays in
the evaluator. Capacity exhaustion explicitly selects the full source for
the affected cells and is counted.

The pinned MIT donor's original pruning core was run in isolation with 32-bit
descriptor storage, Cubey primitive evaluation and workgroup scheduling adapters.
It passed 53,784 exact-value point checks and 1,512 conservative far-bound
checks at its first refinement. This does not establish the performance of
the donor application or qualify its entire allocator/hierarchy.

## Geometry and history evidence

The [committed evidence pack](../evidence/sdf-tracing-comparison-2026-10/README.md)
retains commands, hashes, raw-result summaries and selected images.

- The final 32-fixture manifest has 155 independent known-answer checks per
  candidate. A0/H0 and A1/H1 at region axes 4, 16 and 64 pass. Revision 2 added
  thirteen near-parallel/inside-clip rays explicitly; it changed no tolerances.
- Sphere/box rays use independent closed-form intersections. Selected smooth
  fixtures have closed-form radial reductions, including distinct nested
  bracketings. These are bounded controls, not a general smooth-CSG root oracle.
- Point error is below `3e-8` in those independent fixtures; maximum normal
  error is 0.23 degrees. Regular-ray depth and tangent brackets are scored
  separately. The larger tangent depth error is not a regular-ray failure.
- Each query run also checks a fixed point grid, region seams, normal-stencil
  offsets and leaf centers against the full CPU source. Robot/Guardian content
  has a separate differential full-GPU-source check: 150 rays, 106 regular hits,
  no exhaustion/classification failures, and maximum full/hybrid depth
  difference below `1.6e-5` workspace units in the selected views.
- In the 128×128 native query ROI, the sampled path hits zero pixels of the
  half-grid sphere; analytic/hybrid paths each hit 824. Geometry and viewport
  world scale are identical. The thin-stick control also shows a narrower,
  sharper source-defined boundary. These are query-harness normal images,
  separate from native PBR captures.
- A separate 20-action source-snapshot replay covers hidden geometry, undo,
  redo, branch replacement, an atomic mirrored action, earlier-group transforms,
  smooth-radius undo, appearance/domain/cut-owner changes, pre-publication
  failure and deterministic retry. The three tested native routes pass.

The action driver owns complete canonical snapshots; it does not change the
saved authored reveal-history format. Requested source compilation and cache
creation happen inside the action timer. Expected-state compilation happens
before that timer. Frames are serialized and queue-idle confirms completion
after submission. This is an observed upper bound for the first completed GPU
frame, not physical display latency or continuous-brush responsiveness.

The two study failure injections occur before publication. Actual existing
field frontier/reference overflow and recovery are separately covered by the
legacy GPU batch validator. Do not interpret injected boundary failures as
fault injection at every Vulkan allocation, or as asynchronous publication proof.

## Frame, edit and memory tradeoff

RTX 5070 Ti, driver 610.57.04; Release, 1×, fixed hard shadows, accepted Fieldkit
Robot and limestone Guardian. One owned GPU job at a time. Full-HD rows have
three interleaved runs of 180 frames, 30 warmup, **150 completed GPU samples per
run**. The table shows the range of per-run p95 values.

| Scene / route | Whole-scene GPU p95, 1920×1080 | Selected-edit completion p95 at 320×180 | Published geometry cache |
| --- | --- | --- | --- |
| Robot S0 | 4.23–4.25 ms | 94.3–94.9 ms | 913.0 MB field |
| Robot A1, regions 16 | 20.26–20.53 ms | 10.1–10.6 ms | 0.56 MB regions; zero field |
| Robot H1, regions 16 | 24.62–24.84 ms | 102.7–103.5 ms | 913.0 MB field + 0.56 MB regions |
| Robot A1, regions 64 | 7.83–7.88 ms | 79.5–84.1 ms | 9.25 MB regions; zero field |
| Robot H1, regions 64 | 9.88–9.95 ms | 170.4–179.6 ms | 913.0 MB field + 9.25 MB regions |
| Guardian S0 | 1.91–1.92 ms | 938.9–939.3 ms | 913.0 MB field |
| Guardian A1, regions 16 | Not advanced: 53.0 ms p95 at 320×180 | 112.2–113.4 ms | 3.00 MB regions; zero field |
| Guardian H1, regions 16 | Not advanced: 71.9 ms p95 at 320×180 | 1046.1–1046.6 ms | 913.0 MB field + 3.00 MB regions |
| Guardian A1, regions 64 | Not advanced: 14.7 ms p95 at 320×180 | 467.2–481.3 ms | 54.68 MB regions; zero field |
| Guardian H1, regions 64 | Not advanced: 17.6 ms p95 at 320×180 | 1396.0–1401.5 ms | 913.0 MB field + 54.68 MB regions |

MB means decimal requested bytes. The 320p scaling guard was 12 ms; Guardian
candidate full-HD runs were skipped before collecting them. That guard is
unchanged after observing the results. The edit runs select early/middle/late
construction states, final/full-count transitions, backward seeks and redo,
three times per route. Their raw per-action rows prevent the full-count copy
shortcut or early actions from hiding late rebuild costs. Edit and frame
measurements use different resolutions and must not be merged into a claim of
full-HD editing responsiveness.

The final reference cap is 32M 32-bit references, at most 128 MiB. All measured
final content has zero fallback. The first 8M cap caused Guardian fallback;
those exploratory timings are retained locally but excluded from this table.
The final all-region mean is approximately 6.8 instructions for Robot/64 and
50.1 for Guardian/64; a near-surface work histogram has not been measured.

Region construction/upload is roughly 5 ms for Robot/16, 80 ms for Robot/64,
35 ms for Guardian/16 and 420–461 ms for Guardian/64 in the retained runs.
CPU scratch high-water payloads are about 1.1/14.1 MB and 4.8/69.0 MB,
respectively. A conservative measured Guardian H1 working/completed-region
overlap plus scratch and the paired field is about 1.10 GB, below the 2 GiB
geometry budget. This is requested payload accounting, not a complete Vulkan
heap/allocator or process-memory profile. Canonical JSON/program/history and
shared renderer/environment allocations are separate; A1 does not claim zero
total renderer memory.

No candidate meets both practical targets across both subjects. Robot A1/64
meets the measured 16.7 ms frame and 100 ms discrete-edit targets in their
respective tests. Guardian misses them. H1 still builds the full authored
field; this pass does not implement incremental or pruned brick construction.
The second/coarser hybrid cache and optional S1 builder ablation stop at this
failed practicality gate rather than expanding the field backend now.

## Design implications and acceptance limits

The earlier motivation for spatial filtering and hybrid accuracy was sound.
Filtering can support tracing without erasing undo history. It does not force
the canonical model to be a flat list or force every rendered point to follow
the brick grid. The key separation is complete source/history versus disposable
spatial programs and sampled data.

Current rigid/uniform groups already flatten without changing that contract.
Compact smooth operators can be pruned conservatively when their blend support
is included. Avoiding all smoothing/groups is therefore unnecessary. General
nonuniform/domain warps need explicit distance/Lipschitz and invalidation rules;
they are not automatically qualified by this result. Brush/field relaxation is
another modeling operation with a different source/history contract.

All 29 original shader binaries, accepted scene/preset bytes and source identity
contracts are preserved. The existing field/material/history tests and GPU
batch/optical validators pass. Candidate geometry resources are selected only
for their explicit routes. The local source adds no new device features to the
default route. Profiling now includes a whole-scene timer and collects final
frame slots so timing receipts do not drop their last samples.

Native Robot/Guardian PBR and bounded sphere-glass captures were inspected.
Review found and fixed a missing optics define in candidate entrypoints before
glass acceptance; final transport diagnostics enter the glass path and have no
exhaustion pixels. Automated native X11 rendering under Xvfb completed 154
frames in a one-second window. This is virtual-window smoke, not physical
desktop input, resize/seek interaction or user visual acceptance.

The numerical guard is a conservative engineering envelope qualified on these
fixtures/content, not a proof for every extreme valid primitive parameter or
floating-point compiler transformation. General mixed-smooth root isolation,
all-parameter certification, shader-side revision receipts, exhaustive optical
transport and whole-renderer memory accounting remain adoption gaps. Keep the
candidate opt-in. The completed deliverable is this bounded comparison and its
failed/successful gates, not a production replacement renderer.

# Rounded brush segments and paced commit service

Date: 2026-10-31. Runtime source: `f9c3daa4362b58204297a4d8d6079f67509c8652`.
The [followup plan](sdf-interactive-edit-followup-plan.md) is complete through its
bounded service, geometry and sampling gates. The combined timed-stroke trial
stopped at Guardian's service miss. Evidence and retention are indexed in the
[native evidence packet](../evidence/sdf-interactive-edit-2026-10/README.md).

## Outcome

The private Cached+8 experiment now admits **rounded tapered segments with
independent endpoint radii**, through `--edit-rounded-segments`. It reuses the
existing authored `round-cone` solid. Spheres and ordinary capsules remain
special cases. The previous sphere-only admission stays the default for this
private executable; public modes/defaults and authored formats are unchanged.

This improves brush representation and can reduce incoming construction work.
It does not simplify Guardian's existing 996 tapered segments. Guardian still
misses the chosen four-edits/four-frames service target with the tested bounded
scheduler. No continuous editing, grouped-stroke undo, multiple in-flight reader
retirement, stereo or physical input/display acceptance is claimed.

## Geometry and cache correctness

Admission, interval entry/exit, cap/side normals, add/cut ownership, workspace
clipping and local construction were qualified separately. The independent CPU
oracle numerically minimizes the union of interpolated endpoint spheres; it
does not copy the shader's conical intersection quadratic. Cases cover equal
and unequal radii, reversal, containment and its boundary, pointed ends, tiny
lengths, transforms/scale, inside starts, parallel and grazing rays, overlaps
and 0/1/2/4/8 tools. Stationary/contained inputs become spheres before axis
normalization. A sub-minimum-length approximation requires the declared error
allowance; both-zero radii create no volumetric contribution.

| Gate | Result |
| --- | --- |
| Admission/canonicalization | 23 positive/negative checks passed; legacy admission still rejects tapered tools and varying sphere radii. |
| Regular geometry | 271 rays passed; maximum depth error 0.000255523 against 0.000279018, maximum normal error 1.881 degrees against 2 degrees. |
| Pending/base owner | 251 checks passed. |
| Exact construction | 247 cache comparisons, covering **13,789,351,936 active raw R16 words**, passed with identical reachable topology and logical leaves. |
| Original Cached consumer | 12 zero-edit/settled native images were byte identical. |
| Camera/shadow replay | 115,200 camera rays and 21,602 shadow rays; no exhaustion, with visible edits in every view. |
| Serialized prefix/failure controls | 28 sequences / 136 frames passed four/four accounting, failure retention, intermediate and settled image comparisons. Four retained-source replays matched original Cached. |
| Protected controls | The original public executable and all 211 protected SPVs retain their baseline bytes. The unchanged sphere-only fixture control passes 82 regular rays. |

The 247 comparisons comprise 28 initial fixtures, nine disjoint/mixed/buried
coverage cases, 20 dominance-builder subject cases, 20 simpler-local-builder
controls, 20 sliced cases and 150 sampling-chain comparisons. Every comparison uses that representation's own fresh canonical
source. No identity between differently sampled authored strokes is assumed.
Service publication checks and retained representative dump pairs are additional
checks, reported separately in the evidence packet.

The geometry payload screen remains about 1.71 GiB for two Fields and builder
storage. The existing largest diagnostic staging allocation is about 73.14 MiB;
together these remain below the 2 GiB geometry/diagnostic screen. This is not a
whole-process VRAM measurement. Source limits remain 1024 leaves, 2048
instructions, depth 24 and stack 32. Guardian still has at most 13 extra leaves.

### Retained workspace diagnostic

A ray parallel to the workspace face at only 0.0001 units from it can hit the
Cached near-surface tolerance without crossing the true surface. The unchanged
zero-tool control reproduces the false near-plane hit. The added-tool variant
can resume into the same ambiguity after leaving the tool. These are retained
as four explicit sub-grid/workspace diagnostics, including a passing cut case;
they are outside the regular geometry gate. The tool-only control passes. This
pass does not fix or conceal the existing Cached boundary behavior. Singular
pointed tips also have no regular normal-error assertion.

## Paced sphere service

The control runs a bounded preparation worker, one GPU owner, completed drawing
at 60 Hz and maintenance between draws. Source copying and both admission
passes remain in job age. Initial source parsing and pipeline preparation are
startup, outside the retained-parsed-source service control. There is one
completed draw at a time; descriptors change only after its completion.

The allowance limits complete maintenance unit wall time. Bounds are frozen
before timing at maximum calibrated unit wall time times 1.25 plus 0.05 ms,
per work kind. Known oversized units are deferred, rather than called budgeted
work. This conservative policy leaves some capacity unused. The measured
latencies are outcomes of this policy, not lower bounds for the architecture.

Three counterbalanced repetitions use four accepted radius-0.025 sphere edits.
The deadline is the **completed replacement draw within 66.667 ms**, including
CPU work and publication, rather than just a frame index or GPU build time.

| Subject | 4 ms allowance, addition | 4 ms allowance, cut | Four-frame service |
| --- | ---: | ---: | --- |
| Guardian | 119.9 ms median | 202.9 ms median | Failed all repetitions. |
| Robot | 38.2 ms median | 38.2 ms median | Passed all repetitions. |

At 2 ms, the frozen bounds block Guardian classification; those jobs retain
their pending edits and cancel at the finite limit. Robot additions finish in
about 54.8 ms and pass; cuts take about 71.9 ms and miss. All **1,440 measured
complete service frames** meet 16.67 ms; the maximum is 8.512 ms. Frame cadence
passing does not establish adequate edit throughput.

Guardian spends about 19–20 ms preparing the worker snapshot and another 16 ms
admitting it inside the builder. The 512-group cut job uses about 16 ms GPU and
20 ms of measured submission/unit wall time, spread across ten maintenance
frames. Addition uses about 7 ms GPU. These costs explain why moving work off
the drawing thread preserves frames while allowing edit age to grow.

A separate 1024-group calibration probe at 4 ms still misses: completed
replacement is about 169.7 ms for cut and 136.1 ms for add. It confirms that
changing the chunk size alone is insufficient in this scheduler. This is one
probe per operation, not a new median timing cohort.

## Preview rendering

Matched 1080p SS1 opaque runs with shadows use three repetitions and 100 measured
frames after 20 warmup frames per run. Eight tapered pending tools cost:

| Subject/view | Add median GPU | Cut median GPU |
| --- | ---: | ---: |
| Guardian preset | 1.99 ms | 1.99 ms |
| Guardian close | 2.71 ms | 2.73 ms |
| Robot preset | 4.01 ms | 4.03 ms |
| Robot close | 8.07 ms | 8.00 ms |

Across 4,800 measured frames, including matched original/zero-tool controls,
none exceed 11.11 or 16.67 ms GPU. These are preview-only GPU timings, with no
maintenance. Different tool coverage changes the Cached walk; a lower value in
one edited view does not establish that the tapered primitive is intrinsically
cheaper. Two close preview/committed image pairs were also inspected visually.

## Sampling both path and width

Ten recorded-input controls cover straight paths, corners, curves, width bulges
and stationary width changes at 120/240 poses per second over 0.05 seconds.
Release/restart paths are segmented separately and leave the intervening gap
empty. Greedy provisional-tip extension freezes previous accepted spans; five
focused CPU tests include an unchanged-frozen-prefix check.

The combined centerline-plus-radius deviation allowance is one quarter of the
existing lattice spacing, **0.000139509 workspace units**, against the recorded
position/radius polyline. Dense independent interpolation checks pass. Maximum
observed recorded error is 0.000136297; the scripted continuous path/width oracle
stays within the separately frozen half-spacing allowance, with maximum error
0.000139995. Corners, width extrema and endpoints remain represented. This is a
study tolerance, not a new default or a bound on unobserved physical motion.

Both raw and simplified representations already use rounded segments. This
comparison therefore measures **admission/merging**, not a sphere-to-capsule
conversion. Static native trials commit consecutive batches of up to eight,
carry the candidate forward, and check each prefix against fresh canonical
source. Initial parsing, pipeline setup, canonical reference construction and
rendering are outside the following builder rows; admission/copying are inside.
They are not timed-input service results.

| Guardian path | Raw to admitted segments | Complete builder wall, raw to admitted | GPU work, raw to admitted |
| --- | --- | --- | --- |
| Straight, 120 Hz | 6 to 1 | 31.46 to 29.21 ms | 10.46 to 10.43 ms |
| Straight, 240 Hz | 12 to 1 | 60.27 to 29.73 ms | 19.53 to 10.39 ms |
| Curve, 240 Hz | 12 to 5 | 59.61 to 31.30 ms | 19.60 to 10.55 ms |
| Width bulge, 240 Hz | 12 to 6 | 59.35 to 31.48 ms | 19.68 to 10.60 ms |
| Stationary width, 240 Hz | 12 to 1 | 58.32 to 28.00 ms | 20.18 to 10.07 ms |

Values are medians of three counterbalanced repetitions. At 120 Hz, the curve
and width-bulge controls retain all six segments at this tolerance. At 240 Hz,
reducing twelve segments below eight avoids a second batch, repeated dirty-brick
work, admission and copying. That is the main saving, rather than a twelvefold
reduction in per-sample cost. Robot's 240 Hz straight case similarly drops from
9.93 to 4.58 ms wall and 4.02 to 1.95 ms GPU.

## Next decision

The subsequent [edit-service results](sdf-edit-service-results.md) retain these
historical misses and measure preparation/submission changes, an explicit 8 ms
allowance and a finite simple-scene online-input prototype. They do not qualify
unbounded Guardian editing.

The later [solution research pass](sdf-interactive-edit-solution-research.md)
validates and narrows these proposals. In particular, recorded cut-unit wall
work already exceeds four 4 ms windows; preparation and packing alone are not a
demonstrated sufficient fix. It also identifies exact-center cache invalidation
and multi-frame publication requirements before implementation.

Keep rounded tapered segments as the general brush representation, with the
sphere/equal-radius controls. The bounded geometry route is accepted; a public
brush tool and complete interactive service remain unqualified.

The next performance investigation should first isolate **repeated immutable
source preparation** and **maintenance scheduling utilization**. This service
path copies/validates the same snapshot twice before its first GPU unit. A
validated immutable-prefix token needs source/palette/version ownership and
stale-token tests before it can remove the second pass. More specific unit
bounds and publication ordering should be measured without hiding overruns or
worker time. The current whole-kind bound and draw-before-maintenance ordering
are deliberately conservative.

After those measurements, decide whether exact center-value reuse or a narrower
source evaluator is still needed. The long canonical source remains a real GPU
cost, but the observed 120–203 ms latency is not all GPU evaluation. Approximate
construction remains a separate quality-contract decision. No additional
evaluator, token, adaptive budget or approximate route is implemented here.

The combined timestamped-stroke/maintenance trial remains stopped at the
Guardian service gate. Longer sessions also need a source-capacity strategy;
segmentation slows growth but does not remove the cap or establish undo.

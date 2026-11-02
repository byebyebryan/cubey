# Bounded SDF edit-service results

Date: 2026-11-01. Runtime: `9df0fa1f`. Execution followed the
[bounded plan](sdf-edit-service-execution-plan.md) and
[solution research](sdf-interactive-edit-solution-research.md).
The [evidence packet](../evidence/sdf-edit-service-2026-11/README.md) contains
reproducible arithmetic, native receipts, source identities and retention policy.

## Outcome

The preparation changes and coarser dispatches materially reduce the work.
An explicit 8 ms maintenance allowance makes the Guardian four-edit control
much more practical, but a repeated deadline miss prevents claiming a hard
66.667 ms guarantee. The finite simple-scene prototype now consumes actual
timestamped poses while previewing, building and publishing exact local caches.
Public Cached defaults and all other render options remain unchanged.

These are different evidence boundaries: the Guardian batch control starts
from retained parsed scenes, while the finite input route constructs new source
after receipt. This pass does not qualify sustained Guardian editing or a public
brush editor.

## Preparation and submission

The matched old-native baseline found Guardian additions at about 120 ms with
validation and 86 ms without it; cuts were about 203/169 ms. Robot passed the
four-frame target in both modes. Validation affects overhead but did not explain
the original Guardian miss.

On Guardian, measured preparation factors were:

| Control | Worker preparation | Builder admission |
| --- | ---: | ---: |
| Whole-scene palette round trip and repeated validation | 18.47 ms | 16.34 ms |
| Palette-only serialization | 6.79 ms | 5.85 ms |
| Palette-only plus immutable prepared result | 6.77 ms | 0.09 ms |

The palette path still parses each retained document once. It shares the exact
material serialization code with persistence, preserving V2/V3 recipe metadata,
names/order, native workshop changes, patterns, optics, domains and libraries.
The prepared result owns its source and binds to session, base-cache, palette
and accepted-prefix identity. Stale epochs, altered prefixes and caller-side
mutations are covered by focused tests.

For Guardian cuts, coarser 2048-group units reduced median GPU work from
16.14 to 11.60 ms, and completed-unit wall work from 18.49 to 13.21 ms in the
matched validation-off submission study. Command-pool/fence reuse alone did not
show a unit-wall improvement. Moving publication before drawing reduces a
possible extra frame, but does not remove source or GPU work.

No shader or numerical-quality change produced these savings. The pooled route
preserves dependencies, completes its submission ticket before recycling and
retires its resources on the GPU owner. One completed draw at a time remains the
reader-lifetime boundary.

## Fixed budgets and deadline limits

The 4 ms control remains too slow for Guardian. The 6 ms cohort also missed on
all three Guardian cuts and one addition. The initial 8 ms overview/close-view
cohorts passed, including a validation-on control.

The stricter repeat records actual unit availability against the maintenance
deadline. **27 of 28 trials passed.** It caught a Guardian addition at 71.23 ms,
with a 0.03 ms unit-window overrun. That run's classifier GPU units were slower;
the cause of that variation
was not isolated. Preparation was 6.49 ms and builder admission 0.09 ms, so the
miss cannot be assigned to the old duplicated preparation. Keep it as a measured
miss rather than replacing it with a later run.

An 8 ms allowance is an explicit private option, not a public default or a
worst-case scheduling guarantee. Frame cadence, maintenance deadlines and
accepted-edit completion are separate gates. The machine is paced at 60 Hz;
these controls do not establish 90 Hz, multiple in-flight readers or XR behavior.

## Finite online input

The integration uses a bounded recorded producer independent of drawing,
online position/radius segmentation, a provisional tip, immutable accepted
prefixes and a worker that constructs/validates new authored source after
acceptance. It commits at most four accepted primitives per job and preserves
every accepted ID. A 64-pose input queue applies backpressure; pending geometry
including the tip never exceeds eight slots.

The source is a simple sphere with named history and a fixed two-material
palette. The finite 0.35-second input includes a straight/paused span, a corner,
changing radius, curvature and explicit release/restart gaps. At 120 poses/s,
35 poses produce 15 accepted primitives; at 240 poses/s, 67 produce 14. Add and
cut are separate same-operation trials. The finite stream, source depth, stack,
leaf count and resulting shapes are preflighted before admission.

All new source construction and every queued input delay remain in the measured
ages. The input-to-preview gate is 33.333 ms; accepted-input-to-completed-cache is
66.667 ms; completed frames are limited to 16.67 ms. Independent dense
reconstruction checks the combined centerline/radius tolerance of `1/(256*7*4)`.
The settled native cache is compared with an independently built canonical
cache, and faithful source/history serialization must match the independent
full replay. Startup calibration and initial GPU capacity allocation are separate.

The final strict-window cohort passed all 16 trials (three repetitions with
validation off and one with it, at both rates and polarities):

| Maximum observed | Result | Gate |
| --- | ---: | ---: |
| Completed frame | 6.45 ms | 16.67 ms |
| Accepted input to completed committed draw | 48.39 ms | 66.667 ms |
| Input pose to preview | 23.06 ms | 33.333 ms |
| Pending primitives including tip | 5 | 8 |
| Queued poses | 5 | 64 |

All 16 caches and retained sources match their independent final rebuilds.
The reconstruction maximum is 0.000137303, within the 0.000139509 allowance.

The observed queue occupancy does not saturate all eight pending slots or all
64 input slots. This is a finite desktop prototype, not an unbounded-rate or
long-session guarantee. The stream currently rejects zero-width tips and
subminimum non-contained segments rather than silently changing their geometry.
Final review added provisional-tip shape checks to startup preflight at
`f49f8070`. Two additional validation-on 240 Hz add/cut trials passed (maximum
accepted-to-cache age 53.02 ms, frame 6.58 ms), and a tiny moving-tip stream was
refused before host/GPU setup. This changes admission, not the measured update
algorithm; the repeated service/timing cohort remains pinned to `9df0fa1f`.

## Verification and next decision

All drained service controls retain exact active raw R16/topology and identical
source-work counts across the preparation/submission factors. Current-runtime
geometry/owner checks pass, and four retained-scene replays are byte-identical
to original Cached. The original public executable and all 220 SPVs, including
the 211 protected public shaders, remain unchanged. Focused authored,
preparation/versioning and sampling tests pass.

Keep the immutable preparation and coarser private service options. Before
promoting the Guardian timing target, investigate the observed GPU-unit
variation and empirical-bound misses under a fixed workload. A next bounded
integration should use a moderate authored scene with source headroom, then
test Guardian separately with preflighted input. Include live source construction,
queue saturation, longer sustained input and accepted-edit age, not just static
batch replacement time.

The 1024-leaf/depth/stack limits still apply. Guardian starts at 1011 leaves;
finite successful commits do not solve source growth or undo product semantics.
Spatial CSG programs, classification-center state and field-authoritative editing
remain separate research decisions; none was implemented in this pass.

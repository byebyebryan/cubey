# SDF interactive editing followup plan

Status: bounded pass executed; see the
[interactive editing results](sdf-interactive-edit-results.md). Service and
rounded-segment geometry/sampling were evaluated independently. Guardian missed
the service gate, so the combined timed-stroke phase remains stopped. The
[previous plan](sdf-local-edit-research-plan.md) records the completed local-cache
study. The proposed experiments below are preserved as their execution contract;
remaining performance alternatives are not automatically selected.
Revised 2026-10-31 after checking Guardian's segment composition and the existing
[rounded tapered segment contract](sdf-rounded-stroke-contract.md).

## Starting point and priorities

Local cache updates, the eight-sphere interval preview and serialized four/four
prefix publication have passed their bounded correctness gates. Paced rendering,
concurrent input and in-flight resource retirement remain to be qualified.

Four small Guardian cuts take about 10.6 ms GPU whole: roughly 6.1 ms
classification, 3.25 ms canonical fallback and 1.28 ms copying. Splitting into
128-group units raises the total to 37.7 ms. The previous 2 ms/frame allowance
cannot supply enough work across four frames. That allowance was an experiment
target, not the entire desktop frame budget. Guardian render-only measurements
around 2–3 ms motivate a larger-budget probe; they do not establish complete-frame
headroom. Repeated helper admission also costs about 15 ms for Guardian, apart
from initial parsing.

Guardian is a saved, script-authored scene, not a recorded live input stream.
Its current limestone source has 1,011 leaves: 996 `round-cone` segments, nine
capsules, five spheres and one box, grouped into 65 history actions. Its curves
already use connected tapered segments. Adding capsule support to new input
does not simplify that existing source. Replacing curved, varying-width paths
with fewer straight segments would be a separate geometry approximation study.

Keep two questions separate:

- **Commit service:** can the existing exact builder keep up with a declared
  accepted edit rate while drawing Guardian? Start with the unchanged sphere
  controls, bounded worker preparation and fixed 2/4 ms maintenance allowances.
- **Brush representation:** can the eight-edit path preview and commit connected
  segments with different endpoint radii, and can spatial sampling reduce
  incoming operations while preserving the observed path and width?

The first question determines Guardian's performance priorities. The second
provides useful stroke functionality; a speedup must be measured independently.
Combine them only after their individual gates pass. If the service probe
fails, scope the source-evaluation experiment before expanding the integrated
editing trial; do not treat tapered segments as its performance remedy.

## Reuse rounded tapered segments for brush input

Capsule strokes are an existing projectR mechanism. At source `e2b80796`, the
archived MarchingCubes controller passes the current and previous brush poses
into modification jobs. Its capsule preparation computes their midpoint,
orientation and separation to represent the connecting segment. Motion admission
already checks position, rotation and strength changes. These observations refer
to the archived editing path; they do not establish continuous-stroke behavior
or performance in the later brick controller.

Source anchors:

- projectR `Archive/MarchingCubes/Scripts/Modifier/Controller.cs`, activation and
  motion handling around lines 161–203.
- projectR `Archive/MarchingCubes/Scripts/Primitive/Capsule.cs`,
  `PrepareParameters` and `CalculateDistance`.
- Cubey `projects/sdf/sdf_authored.cpp` already parses/evaluates capsules, with
  a corresponding primitive in `projects/sdf/shaders/authored.glsl`.
- The same evaluators support `round-cone`, the convex hull of two endpoint
  spheres. `projects/sdf/tools/refinement_author.py` already turns curved,
  varying-width paths into these segments. The existing geometry contract and
  its CPU/native qualification remain the starting point.
- `projects/sdf/sdf_cached_stroke.cpp` deliberately admits spheres only in the
  qualified eight-edit experiment and enforces a fixed radius. Its preview in
  `projects/sdf/shaders/cached_stroke_trace.glsl` also assumes spherical ray
  intervals and normals. Extending only admission is insufficient.

A rounded segment supplies continuity between endpoints. One emitted each frame
still creates one edit each frame. Any reduction in edit count must come from
spatial admission or merging nearly straight path portions within an explicit
path-and-width error bound. A longer segment may also touch more bricks; reduced
leaf count is not automatically reduced construction cost.

Represent each proposed brush segment as `(p0, r0, p1, r1)` in workspace
coordinates, plus an immutable ID, operation and material. The surface is the
existing `round-cone` solid, with the following cases:

| Input | Canonical geometry |
| --- | --- |
| Distinct endpoints, equal positive radii | Ordinary capsule geometry; use as the control before tapering. |
| Distinct endpoints, unequal radii | Rounded tapered segment using the existing `round-cone` encoding. |
| Coincident endpoints, or one endpoint sphere contains the other | The larger endpoint sphere, including the containment boundary. Handle coincidence before normalizing the axis or creating a positive half-length. |
| One zero radius and one positive radius, without containment | Existing pointed-end geometry. Keep the singular tip out of regular normal-error statistics and retain it as a diagnostic. |
| Both radii zero | No volumetric brush contribution. Preserve the input event without allocating a geometry slot; a lift/restart must not create an unintended bridge. |

Validate finite positions and nonnegative radii. Preserve endpoint/radius pairing
when reversing a segment. Apply coincidence/containment reduction before the
general tapered case. Consecutive segments share position and radius at the
joint. A stationary radius change can therefore still create a sphere update;
position-only admission must not discard it. Do not substitute a small positive
length for a stationary edit or clamp an unsupported taper into a cylinder.

The current parser requires `half_length >= 0.000001`. For a shorter,
noncontained segment, a sphere approximation is admissible only if it passes
the frozen path-and-width tolerance; its accepted canonical source is that
sphere. Otherwise defer admission or report the unsupported input, including
at release. Never silently lengthen the segment to satisfy the parser.

Start with opaque, fixed-material geometry and one hard operation per stroke,
all add or all cut. Radius may vary; smoothing of new tools, general domain
operations and mirror grouping remain outside this pass. Existing smooth
operations in the committed base remain unchanged. The segment is a straight
sweep with linearly varying sphere radius, not an analytic spline. Do not claim
smooth curvature or normal continuity across a bent multi-segment joint.

Use an explicit input transform and checked conversion to Cubey's local Y axis,
midpoint, half-length and endpoint-radius convention. Do not copy projectR's old
motion thresholds as new defaults. Reuse the existing geometry oracle, then
qualify the new interval preview and local construction independently.

Each convex tool has at most one occupied ray interval. Derive and qualify its
entry/exit distances and cap/side normals, including nearly equal radii, near
containment, reversed endpoints, tiny lengths, axis-parallel rays, starts inside,
grazing/tangent rays and workspace clipping. Preserve merged-interval ownership
for overlapping tools, inverted cut normals, materials and shadows. A bounding
capsule/box may reject rays conservatively; its boundary cannot stand in for the
actual tapered surface. Recheck numerical locality/dominance guards for this
primitive rather than transferring the sphere-only qualification by assumption.

## Proposed experiments and acceptance

| Order | Experiment | Required evidence and stop condition |
| --- | --- | --- |
| 1 | Existing sphere controls with worker preparation and fixed 2/4 ms maintenance budgets. | Preserve the 2 ms miss as the control. Replay the existing four-accepted-edits/four-frames demand. Measure actual complete-frame deadlines, unit costs, preparation/job age and publication at 4 ms. A larger allowance passes only when rendering and maintenance together fit the target; spare render-only GPU time is insufficient. Separate CPU admission, classification, canonical sampling and copy costs. |
| 1b, conditional planning | Reuse exact center evaluations or improve the exact source evaluator. | If phase 1 still misses service, choose and scope one candidate from its measured dominant cost before another implementation loop. Include setup, compilation, new-cell fallback, memory and invalidation costs. Retain strict cache identity. Brush qualification may proceed independently, but does not resolve this gate. |
| 2 | Extend preview and local construction to 0/1/2/4/8 rounded segments: spheres, equal-radius controls, then variable-radius tools. | Compare each candidate cache with its own fresh canonical source, including reachable topology and every raw R16 word. Qualify depth, normals, shadows, ownership, degeneracies and workspace boundaries for add and cut. Preserve zero-edit Cached and existing sphere controls. |
| 3 | Segment recorded position-and-radius paths before timing. | Freeze path-and-width tolerance. Compare operations, source leaves, touched bricks and geometry error across unmerged and simplified rounded-segment paths. Retain corners, radius extrema, endpoints and accepted geometry. Fewer raw poses alone is not success. |
| 4 | If service and geometry gates pass, combine finite timed segment streams with prefix commits; separately one large edit. | Measure raw poses, provisional geometry, accepted segments and completed prefixes separately. Preserve all accepted IDs and the eight-slot cap. Report pauses, first-visible latency, drain time and complete-frame misses; test the large edit against 4/8/16-frame completion allowances. Compare total work and latency, not only accepted segment count. |
| Separate future fork | Construct an explicitly approximate cache from the previous sampled field and new tools. | Requires a distinct representation/quality contract and an agreed visual/drift gate before implementation. It cannot be substituted for the exact route or counted as passing its R16 identity gate. |

The proposed next implementation loop covers phase 1 and the independent
geometry/sampling phases 2–3, with phase 4 admitted only after their gates pass.
Phase 1b and the approximate fork require a scoped decision after a service miss;
neither is automatically selected by that miss. Reauthoring/decimating Guardian's
existing 996 tapered segments is a separate study and is not included.

## Scheduling and CPU preparation

Use one GPU owner and the existing queue. Prepare immutable source snapshots on
a bounded worker, or qualify an immutable validated-prefix token so the same
admission is not repeated unnecessarily. Keep source parsing/validation in total
job age and received-to-visible latency. Precompiled input is a separate control.
Moving preparation off the frame thread does not reduce its service cost or the
GPU work; measure both independently.

Poll bounded work completion. Do not wait for a complete construction job on the
frame thread. Bind cache, program/material data and pending controls consistently
per frame/version. The current all-descriptor `Renderer::update_field` path is
not an in-flight publication mechanism. Publish only completed candidates, and
retire prefix IDs and old resources after actual replacement-frame/reader
completion. Keep two complete Fields and the existing 2 GiB geometry-resource
screen, including any new auxiliary storage.

Calibrate work-unit sizes for the proposed 4 ms allowance; do not assume the
smallest unit is fastest. Report GPU work, complete submission/wait wall cost,
unit maxima and every overrun. A maintenance allowance does not preempt an
already submitted dispatch. Never admit a known oversized unit and call it
budgeted work.

Only after the fixed-budget probe passes should an adaptive allowance be tested.
Its upper limit, frame-time estimator and safety margin must be frozen before
measurement. Include drawing, preparation/publication overhead, queued work and
GPU completion in that estimate; CPU enqueue time alone is insufficient. Pause
maintenance when no safe allowance remains, and expose the resulting queue age
and admission delay.

Desktop 60 Hz is the first target. Retain full-frame p50/p95/p99/max and every
16.67/11.11 ms miss. No result here establishes stereo, physical input/display
latency or headset acceptance.

Measure a large discrete edit separately from the small-stroke service test.
Its complete construction may span frames; test 4/8/16-frame completion
allowances and record misses. Keep its pending preview visible and retire it
only after a completed replacement frame. A single large primitive can require
substantial brick work despite occupying only one pending/source slot.

## Stroke sampling and queue accounting

Replay the same timestamped paths at 120 and 240 raw poses per second,
independently of draw count. Begin with constant-radius controls, then add width
ramps, interior radius extrema, stationary width changes and pointed endings.
Compare straight segments, corners, curves, pauses, release, fast motion and
workspace crossings. Keep material and operation fixed within a stroke. Record
path speed, spatial extent and width variation with poses/s and accepted
segments/s; these rates are not interchangeable.

Before native timing, freeze a geometric tolerance and verify segmentation
against the recorded position/radius polyline and, for scripted curves, a dense
independent curve/width oracle. A proposed first control bounds centerline
deviation plus absolute radius error by half the existing lattice spacing at
corresponding path parameters. Report both errors separately and check between
samples under the declared interpolation. A straight path with an interior width
bulge must not be collapsed to a constant-width capsule. Retain corners, width
extrema, stroke endpoints and lift/restart boundaries.

This tolerance is a study setting to validate, not an adopted product default or
a claim about unobserved motion between input samples. Dense sphere stamps and
rounded sweeps have different authored surfaces: compare path coverage/error,
and use each source's own canonical cache for byte-identity checks. Equal-radius
capsules and round cones have the same mathematical geometry; that does not
establish identical F32 evaluation or raw R16 words between their encodings.

A provisional tip may extend or replace unaccepted geometry while the complete
observed path remains within tolerance. Once a segment is accepted or frozen
into a build prefix, its ID and geometry are immutable. Extending that prefix
starts a new segment from its endpoint. Accepted geometry may not be merged away
or discarded to conceal an overloaded builder.

Keep eight total uncommitted primitives, including the frozen prefix and any
visible provisional tip; there is no ninth preview slot. Begin with four frozen
operations and up to four remaining slots. One nondegenerate rounded segment is
one source leaf/slot; its endpoint caps are part of that leaf. A separately
visible sphere consumes its own slot. Account for the provisional tip's actual
representation; do not add uncounted endpoint spheres. On overflow pause
geometric admission, preserve accepted geometry and record the delay. Bound
raw-input buffering and report its limit; unbounded buffering is not a service
pass. A change of
operation waits for the current stroke to drain.

Use finite source-admitted Guardian/Robot streams within the unchanged
1024-leaf, 2048-instruction, depth-24 and stack-32 limits. The prior sphere
streams' admission does not qualify the new rounded-segment inputs automatically.
Spatial sampling can slow source growth; it does not remove source/slot limits
or establish a long-session result. Guardian begins at 1,011 leaves and has at
most 13 additional leaves under the leaf cap; preflight the other limits too.
Use a simpler admitted base for longer stream controls, without counting those
results as long-session Guardian evidence or silently resetting Guardian.

Call segmentation a throughput improvement only when the same observed path
and width remain within tolerance and measured total maintenance work, queue age
or sustainable accepted rate improve. Report neutral/regressive cases, including
long segments whose larger dirty regions offset their lower operation count.

## Exact evaluator alternatives

Retaining full-precision canonical center values may avoid repeating the old
scene evaluation for previously visited centers. Define whether stored values
precede workspace clipping, how hard operations update them, and how geometry
epochs invalidate them. Newly exposed centers still require evaluation. Do not
substitute R16 interpolation or uniform empty/filled labels for exact scalar
values. Measure hit rate and saved work against auxiliary memory/copy cost.

A compact evaluation program or a shader specialized for the stable base is
another candidate for classification and uncertain canonical sampling. Keep
the new-tool suffix bounded and include shader compilation/cache invalidation
in setup and editing latency. Preserve operation order and qualify native F32/R16
behavior; mathematical equivalence alone is insufficient. The previously
rejected general regional sampler remains excluded.

## Approximate construction alternative

A separate interactive mode could investigate forming the next field from
cached data plus brushes, while retaining canonical source for exact rebuilds.
The current cache stores sampled values in active bricks and sign/classification
in uniform cells; it does not supply an arbitrary full distance everywhere.
Define the distance band, error/stepping bounds, newly exposed child values and
material ownership before using it as a general scalar operand.

Its evaluation would need repeated add/cut strokes, drift and thin-feature
coverage, seams/normals/shadows, and visible changes during any exact settling
pass. Keep it separately labelled with explicit quality thresholds; no such
threshold or default change is adopted by this plan.

## Completion boundary

A service pass requires coherent rendering during actual maintenance and timed
input, completed-prefix accounting, bounded queues and resources, measured
admission delay, and retained-source replay. Reuse the existing cancellation,
stale/discard, capacity and failure controls, then add delayed old readers and
input during every build stage. At drain, exact candidates must match original
Cached rendering of their retained canonical source.

Report a miss at the chosen budget/rate without enlarging the queue, resetting
source capacity, silently switching to approximation or hiding worker time.
Keep public modes/defaults, original evidence and source/history/material
contracts unchanged. No public brush/undo UI, allocator, XR integration, push or
site/report publication is included in this planning update.

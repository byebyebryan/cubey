# SDF rendering quality and speed: research and proposed comparison

Date: 2026-10-29. Source baseline: `59273c2c` on `sdf-tracing-comparison`.
Status: planning and source inspection only; no new native measurements or
renderer changes. The [previous optimization loop](sdf-query-acceleration-results.md)
is complete. This proposes a separate investigation.

## Question and recommendation

**Is cached rendering already fast enough, which visible defects remain, and
where does additional analytical evaluation buy enough quality to justify its
cost?** Build a measured quality/cost frontier beginning with the sampled
renderer. Retain analytical rendering as a reference and a measured endpoint.

First separate limitations of the stored field from limitations of its hit
solver and normals. Then investigate at most two targeted candidate families.
Do not assume that every surface hit, normal and shadow needs source accuracy,
that the current hybrid supplies a useful intermediate, or that the costs of
different combinations interpolate between the two endpoints.

The user's direction authorizes planning a quality/speed tradeoff. The working
budgets and profiles below are proposals for the experiment, not an adopted
visual policy or permission to change the default.

## Evidence we have, and the missing matched comparison

The [baseline audit](../evidence/sdf-quality-speed-research-2026-10/baseline-audit.json)
pins inspected source, receipts, actual preset cameras and the existing binary.
It makes no new runtime claim.

| Earlier result | Whole-scene GPU p95 | Scope |
| --- | ---: | --- |
| Guardian sampled/full | 1.908–1.921 ms | Earlier 1080p preset view, three runs |
| Robot sampled/full | 4.225–4.249 ms | Earlier 1080p preset view, three runs |
| Guardian analytical, ordinary, A+B | 18.31–18.63 / 20.66–20.81 / 16.15–16.30 ms | Latest preset / side / rear |
| Guardian analytical, adaptive, A+B | 17.80–17.91 / 19.22–19.31 / 15.25–15.34 ms | Latest preset / side / rear; explicit study |

The sampled and analytical rows use different executable identities and runs.
They establish a promising starting point, not a current paired speedup.
The analytical batch had shared encoder activity. The earlier sampled cache
requested about 913 MB and Guardian selected-edit completion was about 939 ms
at 320×180. Fast steady-state rendering does not establish responsive editing.

Current source reveals three distinct geometry questions:

1. **Stored representation:** quantized, trilinearly reconstructed brick samples
   and uniform occupancy labels can lose information. Labels are not distances.
2. **Tracing the representation:** sampled termination uses `0.2 h`, whereas
   analytical termination uses `h / 32`, for `h = 1 / (256 * 7)`. The sampled
   acceptance band is 6.4 times wider. Simply tightening it is not a qualified
   solver: minimum steps, sign changes, first roots and inside exits also matter.
3. **Shading it:** sampled normals use brick-clamped differences at half-spacing;
   analytical normals use six exact field queries at `h / 16`. Normal quality,
   intersection quality and representation quality need separate attribution.

The current hybrid uses cached bounds to accelerate traversal while retaining
source-based intersections, normals and shadow tracing. It protects source
detail, including features the cache may miss. It does not intentionally accept
a cheaper surface approximation to fit a visual error budget.

## Research grounding and limits

The [primary-source ledger](../evidence/sdf-quality-speed-research-2026-10/sources.json)
records what was inspected and what remains inference.

| Primary source | Relevant result | Proposed use here |
| --- | --- | --- |
| [Frisken et al., ADFs, 2000](https://www.merl.com/publications/docs/TR2000-15.pdf), §§3.1–3.3, 4.1 | Detail-driven sampling, trilinear reconstruction, gradient estimation and reconstructed-surface intersection are separate choices. | Distinguish cache information loss from reconstruction/tracing loss; defer a new adaptive cache builder. |
| [Hansson-Söderlund et al., 2022](https://research.nvidia.com/publication/2022-09_ray-tracing-signed-distance-function-grids) | Evaluates voxel traversal/intersection methods and describes continuous normals. | A more accurate intersection of the cached trilinear field is a separate option from exact authored CSG. Author abstract inspected; full PDF access failed. No algorithm port is qualified. |
| [Epic, Mesh Distance Fields](https://dev.epicgames.com/documentation/unreal-engine/mesh-distance-fields-in-unreal-engine?lang=en-US), Quality | Resolution affects rounded corners and thin-feature representation. | Include feature disappearance and memory in the comparison; do not assume normal correction repairs missing geometry. |
| [Epic, Distance Field Soft Shadows](https://dev.epicgames.com/documentation/en-us/unreal-engine/distance-field-soft-shadows-in-unreal-engine), Quality | Uses reduced-resolution shadow evaluation with reconstruction and temporal support. | Treat shadows as an independent possible approximation. Cubey has not qualified the corresponding reconstruction/temporal machinery. |

These are mechanisms and precedents, not speed forecasts for Cubey. A solver
that intersects a reconstructed field accurately still does not recover the
original authored surface. Sampled error predicates are empirical unless a
conservative bound is independently established.

## Fixed boundaries and proposed working targets

- Keep canonical CSG, groups/smoothing semantics, material ownership, history,
  undo/redo and source serialization unchanged. Approximation affects rendering,
  not the authoritative document or the recovery of hidden geometry.
- Keep the sampled/full default and existing automatic shader choices. New
  candidates are explicit studies with honest route and resource identities.
- Start with opaque Guardian and Robot, existing materials, frozen atmosphere,
  hard shadows enabled, no AO/clouds/ground/occluder, 1920×1080 and SS1. Clay,
  no-bump and no-shadow views are attribution diagnostics, not quality wins.
- Use 16.7 ms GPU p95 in the declared test views as the first practical frame target.
  Report CPU/present/startup separately; this is not a full interactive 60 fps
  claim. A 120 fps target is a later choice.
- Permit deliberately approximate geometry, normals and shadows in candidate
  profiles. Do not describe their output as exact. Nonfinite values, unreported
  exhaustion, corrupt caches, stale generations and broken undo remain failures.
- Numeric image budgets below are exploratory reporting bands. Final visual
  acceptance and automatic adoption remain separate from technical screening.

## Phase 0: refresh the endpoints and qualify the measuring tools

Produce a baseline receipt before adding a quality candidate.

1. Freeze executable/SPV/tool hashes, authored scenes, materials, uniforms,
   output extent, actual camera matrices, GPU/driver and load. Use each preset's
   real yaw (Guardian 0.4, Robot 0.48), pitch, target and distance. Side/rear add
   π/2 and π to that yaw. Preserve source-selection metrics for every run.
2. Measure sampled/full and analytical/pruned A64 root-far A+B ordinary in all
   six scene/view cases. Add adaptive as the explicit faster analytical control;
   Robot's identical original-pipeline fallback can be deduplicated only after
   shader, uniforms and geometry identity are checked. Ordinary remains the
   geometry/image reference. Record adaptive's existing image differences.
3. Capture the currently admitted hybrid/unclamped A64 route as context. Begin
   with a Guardian pilot and apply the existing timing cost guard. It is not a
   new quality policy; a skipped full-HD run is not a measurement.
4. Retain three serial, interleaved runs per core case: 180 frames, 30 warmup,
   150 complete samples, exact frame indices 30–179 and raw GPU pass arrays.
   Core timing is at most 54 runs; deduplicating Robot adaptive gives 45. No
   captures/builds/encoding overlap the timed runs. Record shared GPU activity;
   do not stop unowned work. Repeat only if variation obscures the decision.
5. Separately measure warm/cold setup, complete-geometry publication and
   material-only reuse. Retain per-action rows for the existing prefix/undo/redo/
   branch/failure/retry sequence. Report full rebuild latency and requested
   memory independently of frame cost. Do not merge 320p edit receipts with a
   full-HD interaction claim.
6. Qualify hit/miss/status, depth and geometric-normal output against the actual
   selected native functions before using geometry diagnostics. Existing query
   replay admits only static analytical studies and asserts six exact normal
   queries: it cannot be reused unchanged as sampled/mixed-path evidence.
   Bound any additional diagnostic payload to 32 MiB and exclude it from timing.

**Decision:** if cached rendering itself misses the frame target in a view,
identify that cost before assuming there is headroom for source refinement.
If Phase 1 establishes adequate cached quality in the scoped views, retaining
it is a valid outcome; construction latency becomes a separate investigation.

## Phase 1: identify the quality gap, including motion

Use native captures of both endpoints at the same poses and lighting. Add
depth/mask/normal data and no-shadow, neutral-material and no-bump ablations
only as needed to separate geometry, normal, material and visibility effects.
Whole-program ablation timings are not additive GPU cost shares.

Retain the six core views. Add one close and one distant view per subject,
with positions frozen before candidates are tuned. Select at most twelve
synthetic cases from existing thin/oblique/inside/seam fixtures and bounded
extensions: rotated flat faces, corners and CSG seams; positive and negative
thin sticks/slots; a subgrid feature with no cached hit; a front thin surface
with a tempting later surface; grazing rays, inside exits and brick boundaries.
Vary grid phase and projected feature size so one fortunate alignment cannot
stand for all cases. These are private fixtures, not new authored showcase work.

Separate two reference questions:

- Against authored geometry: independent analytic/CPU fixtures and the retained
  source tracer establish first-hit identity, feature coverage and source error.
- Against the cached representation: a small CPU reconstruction/first-root
  oracle, including the actual quantized values and occupancy semantics, checks
  whether a cached solver is faithfully tracing its own represented surface.
  An interpolation error or a source residual alone does not answer both.

Measure the following per view and inside named defect regions, not only as
whole-image averages:

| Quality dimension | Required evidence |
| --- | --- |
| Silhouette | Bidirectional boundary distance in pixels; false foreground/background area; p50/p95/p99/max and maps. |
| Surface position | First-hit depth difference in world units and local pixel-footprint units; earlier/later surface swaps recorded separately. Points on the same ray project to the same pixel, so normalized depth is not lateral screen displacement. |
| Thin features | Visible coverage and survival of named sticks, slots and holes at their actual projected widths; a missing front surface can still produce a later hit. |
| Normals/materials | Angular differences at compatible hits, plus native shading/cut-material comparisons; exclude incompatible surfaces from the angular score and count them separately. |
| Shadows | Visibility differences, contact leaks and boundary displacement; report separately from primary geometry. |
| Temporal behavior | Paired error sequences at identical deterministic poses, edge/feature toggles relative to the analytical sequence and reviewable clips. |

For temporal screening, freeze two 24-pose sequences per subject: a small slow
arc and a close dolly across a troublesome boundary. Use explicit static poses
with identical matrices for every route; current root-far/adaptive admission
rejects runtime orbit. Pose sequences test spatial/temporal appearance, not
interactive timing or a history-based resolve. Raw adjacent-frame image change
is not flicker evidence because the reference also changes. Mark disocclusions
and ambiguous correspondence rather than inventing a temporal score for them.

For ambiguous subpixel cases, add a small supersampled analytical reference
crop with a frozen sample pattern and resolve filter. This separates ordinary
SS1 aliasing from representation loss. Keep it labelled as an aliasing
diagnostic; it is not the SS1 performance endpoint or a basis for silently
changing image goldens.

Report silhouette bands 0.25 / 0.5 / 1 pixel and feature widths below 1, 1–2 and
at least 2 pixels. These are proposed measurement bins. Select and freeze the
named defects and provisional profile criteria from the baseline before tuning.
Human review is required to turn such criteria into an adopted quality policy.

**Decision:** rank the actual defects and their likely causes. Skip fixes for
defects the baseline does not exhibit. Do not spend the next loop merely
improving a whole-image score while visible sticks disappear or edges flicker.

## Phase 2: screen at most two candidate families

Choose from this list using Phase 1. Keep the cached endpoint in every plot.

| Family | Change under test | Main limit |
| --- | --- | --- |
| C: better reconstruction of the same cache | Improve local hit termination/bracketing first; compare two tighter settings against current `0.2 h`, without changing stored samples. | A narrower epsilon alone can interact badly with minimum steps and sign changes. Require reconstructed-field first-root/inside tests. This cannot restore discarded features. |
| N: source normals on cached hits | Keep cached intersections and shadows; evaluate source normals at those hits as a one-purpose quality/cost probe. | Does not repair geometry. Count all six source queries; mixed gradients/material ownership must remain explicit. |
| P: bounded analytical surface refinement | Use cached traversal, then limited source refinement in selected uncertain/visible regions; initially keep cached normals and shadows to isolate surface benefit. | Cached misses require an independent uncertainty path. More exact endpoint evaluations do not establish the first source intersection. |

Prefer C first if its measured defects are significant. Select N when normals
dominate the visible gap, or P when position/feature loss dominates. Do not
implement all combinations or assume N and P gains add. Use at most two
settings per selected family in initial screening, then retain only survivors
for repeated timing and full pose-sequence review.

Before adding a mixed route, freeze its stage and hit-data contract. A refined
point must obtain valid cache/region metadata at its new location; reusing an
old brick pointer across a boundary is invalid. Declare a fallback for a normal
request in a cell without samples. Depth, material ownership, material footprint
and the shadow origin must all use the selected hit consistently. Give camera,
normal and shadow stages explicit policies and tolerances; a shared `trace` or
`EPSILON` change must not accidentally change secondary rays or self-shadow bias.
New combinations need private admission and complete-generation publication
checks; existing CLI routes do not already provide these combinations.

For source-enabled families, capture a zero-selection counterpart with the new
path present but no analytical requests. A seldom-used branch can still alter
compiled shader cost for every pixel. Measure that overhead, code size and
register counts; counts alone do not establish live occupancy or stalls.

For P, proposed initial source-query budgets are 4 and 8 per selected ray,
including search, rejected trials and refinement. Count normal and shadow work
separately and include all work in frame timing. Test a fixed, predeclared
selector before adaptive tuning. Record selected-ray fraction, false negatives,
false positives, exhausted budgets and fallback costs.

The selector and budget need an explicit contract before implementation:

- A post-hit polish cannot rescue an unflagged cached miss. Include uncertain
  intervals on rays that have no cached hit, using metadata tied to the same
  canonical geometry generation. Same-sign corner samples or occupancy labels
  alone cannot certify that a cell lacks a thin feature.
- Distinguish certified bounds from sampled error estimates and view heuristics.
  Converting field error to hit displacement becomes ill-conditioned at grazing
  intersections; composite CSG fields are not universally true distance-to-zero
  functions. A clamped local gradient or a pixel-scaled epsilon is not proof.
- Local polishing is an explicitly approximate candidate unless the earlier
  ray prefix is covered. Test the tempting-later-hit case and report source
  first-hit swaps even when endpoint residual is small.
- On budget exhaustion, either retain a labelled cached approximation or fail
  that candidate's declared profile. Do not silently convert unresolved work
  into a trustworthy source miss. Any full-source fallback is a separately
  charged policy, not hidden outside the advertised query budget.
- Evaluate every source point using its own valid region program; retain
  numerical progress/inside/endpoint checks. Keep bounds and signed values
  distinct. Keep the previous complete compatible generation on build failure.

If P cannot flag missed features cheaply enough, record that limit and stop
its trial. A new adaptive field hierarchy or builder is not an automatic next
step within this loop.

## Phase 3: compare complete policies and make a bounded decision

Plot measured GPU cost against silhouette, feature, shading and temporal error.
Show the Pareto alternatives and per-view tails; do not collapse everything to
one weighted score. Every claimed improvement names the visible defect it fixes
and the extra cost above sampled rendering. Repeat all shortlisted native
policies under the Phase 0 timing protocol using the final measured binary.
Include the frozen close/distant views for final survivors; if they fail the
budget, qualify only the narrower measured scope or reject the general policy.

Proposed technical screening gates:

- A practical intermediate meets 16.7 ms GPU p95 in the six core and four
  close/distant views, fixing a named visible cached defect without violating
  its declared profile.
- A candidate short of that target may remain research-worthy if it reduces
  worst-view Guardian time by at least 20% and 2 ms versus the faster qualified
  analytical endpoint, beyond control variation, with a documented quality gain
  over cached rendering. This is a proposed substantial-gain threshold.
- For a provisional detail-preserving profile, reject new holes, wrong front
  surfaces and loss of the named at-least-two-pixel features. Below that scale,
  show loss/flicker explicitly. A more aggressive preview may expose those
  losses for review; it does not inherit that profile's pass.
- Report Robot cost increases as the price of added quality. Do not require
  every improved-quality policy to be as fast as the cached endpoint, or treat
  the tested views as a guarantee over all possible cameras.
- Keep new diagnostic logical payload below 32 MiB. Keep candidate metadata
  below 64 MiB per generation and combined overlapping geometry generations
  below the previous 2 GiB requested-payload ceiling. Report staging, CPU copies,
  scratch and allocator/device memory separately; reused caches are not free.
- Stop a candidate if repeated geometry publication p95 increases by both 25%
  and 10 ms over its matched cached control, or resource caps are exceeded.
  Passing this stop rule does not make the existing roughly one-second rebuild
  acceptable for continuous editing.

Combine independently qualified choices only if a surviving quality gap
requires it, at most once, and remeasure the combination. A cached-shadow
variant on a surviving source-refined policy is a possible follow-up at that
checkpoint. Reduced-resolution shadows need a separate reconstruction plan;
Epic's temporal support does not exist merely because we cite it.

Potential outcomes include: keep cached rendering; keep a measured intermediate
as opt-in; expose cached preview and analytical settled views for later UX work;
or reject the candidates with a useful defect/cost map. Automatic mode switching
requires hysteresis, transition and edit-state tests beyond these static studies.

## Execution scope, artifacts and completion

Execution should first land a reproducible endpoint/quality receipt. Only then
select up to two candidate families from the measured gaps. Existing tools are
building blocks: `benchmark_tracing.py` lacks the current multi-view endpoint
matrix, and `query_acceleration_loop.py` hardcodes analytical diagnostics and
normal invariants. Reusing them requires explicit extensions and qualification,
not relabelling old receipts. No new flags are implemented by this plan.

Retain exact commands, frame indices, route/SPV/source hashes, raw GPU samples,
memory/publication rows, diagnostic parity, named quality regions, first-hit
fixtures, clips and failures. The final decision report should show a quality/
speed frontier, native split views and motion evidence with clear measured,
forecast, skipped and human-review labels.

Do not add a GPU builder, incremental updates, new model modifiers, optical
transport changes, temporal upscaling, automatic quality switching or a windowed
root-far port to this loop. Their prerequisites and acceptance remain separate.
Local implementation, visual/default adoption, GitHub push and site publication
are separate boundaries. The investigation can complete with a supported
rejection; it need not force a new default or promise 60 fps.

[Research receipt and inspected sources](../evidence/sdf-quality-speed-research-2026-10/README.md).

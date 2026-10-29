# SDF source-query optimization plan

Date: 2026-10-29. Planning baseline: `8b7a5e71`, local
`sdf-tracing-comparison`. Status: proposed execution contract; no runtime change
or new optimization loop has started with this document.

## Objective and boundary

Run two bounded trials against the qualified automatic source shader:
**A, zero-contribution shadow culling**, then **B, prepared rounded-cone
invariants**. Qualify A for the eligible opaque source-lit route. Keep B opt-in
until its numerical, cache and publication contracts are proven. Measure each
independently before testing their combination.

The desired full-quality endpoint is GPU p95 at or below 16.7 ms in all three
Guardian 1920×1080 views. Reaching it is not a promised outcome or permission to
expand the loop. A completed loop records accepted/rejected trials, measured
costs and the remaining gap even if neither candidate earns promotion.

Preserve sampled/full as the default, the analytic and hybrid source semantics,
exact near-surface CSG evaluation, authored content/history/serialization,
existing hit/normal tolerances and optical transport. No smoothing, group/domain
geometry modifiers, new content, GPU field/region builder, startup cache,
resolution reduction, public media, site work, push or deployment belongs here.

## What the evidence supports

The [investigation](sdf-optimization-opportunities.md) measured these ranges of
three whole-scene GPU p95 results in ms, on RTX 5070 Ti / driver 610.57.04,
analytic/pruned A64 root-far, 1920×1080, supersample 1:

| Guardian view | Current automatic | Private shadow-culling probe |
| --- | ---: | ---: |
| Preset | 21.39–21.56 | 19.27–19.61 |
| Side | 22.93–23.04 | 21.61–21.77 |
| Rear | 20.80–20.97 | 17.09–17.17 |

All three static captures match the reference exactly. The probe only covers
one material, fast path enabled, opaque atmosphere, debug 0, no AO, ground or
clouds. Appearance changes, failures/retry, Robot and native-window use remain
unqualified for this candidate. The side camera-only diagnostic already costs
about 20.2–20.6 ms; shadow work alone cannot close that view's gap.

Guardian has 996 rounded-cone instructions. Their shape-only division, slope
and radial square root are currently recomputed during field queries. Preparing
these values is a plausible query-cost trial, with **no measured gain yet**.
Extra buffer loads and different arithmetic could erase its benefit.

These are fixed-view headless GPU measurements. Root-far remains admitted only
for static headless authored source studies; it is not an enabled interactive
window route. Check window behavior on the already admitted ordinary source
route. Do not describe a 21 ms GPU sample as full interactive FPS, or silently
enable windowed root-far to make that claim.

## Controls and pipeline contract

Use the current automatic shader as the performance reference, rather than
claiming new gains against the older approximately 51 ms general shader.
Preserve `--source-shaders general` as the original, unprepared, unculled
evaluator and diagnostic control.

Proposed startup control, not an existing CLI option:
`--source-optimization automatic|reference|shadow|prepared-cones|combined`.
The four explicit trial modes form the reference / A / B / A+B comparison.
During development, automatic resolves to reference. After A passes all its
promotion gates, automatic may select A on eligible frames and reference
elsewhere. B and A+B remain explicit studies in this loop.

General shader selection always retains the original evaluator. Reject an
explicit incompatible trial request with general selection; automatic with
general selection remains valid and resolves to reference. Log requested mode,
effective mode, route, eligibility and published cache identity in receipts.

Fix optimization mode for the renderer lifetime. Retain the existing three
prebuilt source pipelines: general/debug, lit/general-material and
lit/one-material. Build only the appropriate candidate for that renderer's
eligible lit slot, rather than every optimization combination at once. Keep
descriptor layouts compatible with fallback pipelines and bind valid fallback
resources where necessary. Material count, debug and fast-path changes select
prebuilt pipelines using published state; they must not compile pipelines.
Sampled and retained diagnostic routes retain their existing behavior and do
not allocate the prepared cache. Explicit incompatible study modes fail early.

## Trial A: shadow queries with zero direct contribution

Initial eligibility is automatic source shading, exactly one registered
material, the material shortcut enabled, debug 0 and opaque atmosphere lighting.
An unused second registered material still makes the scene ineligible. Other
lighting families, glass, multiple materials and debug paths keep the reference
behavior. Do not generalize the private macro to every lighting function.

Compute the material and final bump-adjusted shading normal before deciding
whether direct lighting needs visibility. Skip only when the finite final
normal and its finite light dot product yield an exactly zero clamped cosine.
Keep the original query for unknown or nonfinite inputs, including a nonfinite
dot product before clamping. No new grazing-angle threshold is allowed.
Retain the geometric normal for shadow origins and AO, the existing AO order,
hard/soft shadow sampling, emission and ambient terms.

Proposed exhaustion policy: lit output reports exhaustion of queries actually
required and executed. A skipped zero-contribution shadow cannot exhaust.
The strict general shader retains the original exhaustive behavior, including
magenta if that unused shadow would exhaust. This is an explicit diagnostic
change in that exceptional case, not universal byte parity. Add a controlled
exhaustion fixture proving that required-query exhaustion still propagates and
that only the unnecessary query is removed. Debug captures alone do not prove
this, because many debug views never execute opaque shadow queries.

Qualify bump on/off, patterns/domains, clay, AO, ground, no/hard/soft shadows,
light directions through the grazing boundary and inside/near-surface views.
Keep unsupported lighting/option combinations rejected or on their original
route. Verify multiple-material and optical scenes remain on reference paths.
Outside the documented exhaustion case, require exact reference image parity
for A. Investigate every difference before considering promotion.

## Trial B: prepared rounded-cone values

Start with a disposable geometry-derived companion buffer. Keep canonical
`SceneInstruction` bytes, saved source, history, geometry/appearance hashes,
CSG ordering and the independent unprepared CPU/GPU evaluator unchanged.
Do not repurpose unused serialized instruction fields in this trial.

Proposed entry: 16 bytes containing slope, radial factor, height-times-radial
and an eligibility tag. Index it by the original source instruction index,
including when a region program evaluates a compacted reference list. Other
primitive types and unsupported/degenerate entries use the original formula.
At 2,048 instructions the payload is at most 32 KiB; two live generations are
64 KiB, before any header, staging, allocator overhead or fallback storage.
Measure actual allocation and peak live bytes rather than quoting payload only.

Prepare only shape invariants initially. Preserve transforms, uniform-scale
handling, the containing-sphere branch and cap/body comparisons. Do not combine
this with inverse-scale, quaternion, world-axis or normal-stencil rewrites.

CPU preparation and GPU evaluation can round differently. Compare against the
unprepared evaluator at equal/zero radii, cap/body transitions, nearly containing
spheres on both sides of the branch boundary, minimum legal height, rotations,
scales, interior points, tangent rays and clipped endpoints. Include points used
by the existing normal stencil. Keep the frozen distance, hit, normal and
inside-exit contracts; do not loosen tolerances or silently refresh goldens.

Audit the existing arithmetic guard used by pruning, root bounds and far
certificates against the new evaluator. Numerical sampling alone does not prove
that a conservative certificate remains valid after changing arithmetic. If its
existing bound cannot be justified, fall back for the affected entry or reject
B; widening guards or changing the certificate contract requires a separate
decision. Retain and explain native pixel differences even when geometry checks
pass. B is not promoted merely because an image looks similar.

First test B on the measured analytic/pruned root-far study with A disabled.
If correctness fails or saved arithmetic does not outweigh extra loads, stop B
and retain the negative result. Only a successful B proceeds to combination and
the applicable history/publication qualification. This loop does not make B an
automatic editor or optical feature.

## Cache and publication requirements

Key the prepared cache by original geometry identity, instruction count/order
and derived-format version; verify compatibility rather than trusting a stale
allocation. Material-only changes reuse it. Geometry edits, undo/redo and
branching prepare a matching candidate before publication. Material registration
may change shader eligibility without changing geometry. Never select using a
pending material count or pair a new program with an old prepared buffer.

Treat displayed source, region programs, prepared values, appearance buffers,
descriptors and published selection metadata as one coherent generation. Keep
the old generation alive until in-flight use has completed. Failed preparation,
upload or admission leaves the previous generation visible; retry can publish
the complete candidate. Report cache reuse/build counters and identities.

The current `prepare_time` prepares regions and the source program before its
queue-idle boundary, but `update_authored` still performs appearance uploads
after displayed state has begun changing. Audit all fallible work on the
candidate path before adding the companion buffer. Add fault coverage at the
new preparation/upload boundaries and immediately before activation, rather
than assuming the existing early region/field injections cover late failures.
If safe publication needs a broad renderer transaction rewrite, stop B at the
isolated study and report that prerequisite; do not hide it inside this loop.

## Qualification and measurements

Reuse the source-selection harness and frozen independent geometry checks.
Extend boundary and publication tests where the new behavior adds risk; do not
create tests that merely duplicate its condition. Capture completed action
frames, including the final submitted frame, rather than inferring pixel parity
from action metadata.

| Gate | Required evidence |
| --- | --- |
| A route and lifecycle | One/two materials, shortcut/debug toggles, appearance-only cache reuse, undo/redo/branching, rejected publication and retry; eligible analytic and hybrid routes plus reference fallbacks |
| A render correctness | Guardian three views, multi-material Robot controls, opaque lighting edge cases, targeted exhaustion fixture; existing glass/material-study references unchanged |
| B evaluator and cache | Independent primitive/geometry/normal checks, guard audit, original-index lookup, cache key/reuse, peak memory and failed-publication coherence |
| B native study | Guardian, Robot and focused round-cone fixtures on admitted static headless routes; original/general and B-only captures retained with every difference reported |
| Combined | Only if A and B independently pass; repeat applicable numerical/image checks and four-way timings, with no addition of predicted gains |
| Native window | Real Xvfb creation, material/debug changes, failure/retry and recreation for A on admitted ordinary source modes; report virtual-window evidence separately from physical acceptance |

Do not weaken root-far admission to run a test. If a B lifecycle cannot be
exercised through an admitted native route, retain it as an explicit unqualified
boundary; component tests cannot substitute for editor acceptance.

For timing, use the final candidate binary, fixed scenes/cameras, 1920×1080,
supersample 1 and unchanged quality settings. Run reference, A, B and A+B in
interleaved serial order where admitted, three repetitions each, 180 frames /
30 warmup / 150 completed GPU timestamp samples. Retain raw samples and per-run
p95s. Record GPU/encoder boundary snapshots and competing load. Run no owned
build, test or capture concurrently. Record startup, preparation/upload,
publication latency and memory separately from steady GPU time and host FPS.

Proposed promotion threshold: correctness gates pass; a trial saves at least
0.5 ms and 5% in one Guardian view in each interleaved repetition, with the gain
larger than observed reference variation; no Guardian or Robot view regresses
by more than both 0.5 ms and 3%. Investigate smaller consistent regressions too.
If noise prevents a decision, permit one additional controlled timing batch;
otherwise mark the outcome inconclusive. These are proposed acceptance rules,
not historical agreements or a guarantee of 60 FPS. A new result cannot inherit
the private probe's performance or correctness pass.

## Execution checkpoints and ownership

All implementation ownership stays within `projects/sdf`, its shader/build
registration and focused tests/tools, plus evidence notes. No engine-wide
pipeline/cache API change is planned. Preserve unrelated concurrent work.
Expected touch points are `sdf_config.h`, `sdf_shader_selection.*`,
`sdf_renderer.*`, `sdf_app.cpp`, `shaders/lighting.glsl`, `shaders/authored.glsl`
and the source-selection qualification tools. A small project-local helper may
own the derived cone data. Record the expected lit-shader changes; preserve
strict general/reference shader bytes, unrelated shaders and all content.

1. **Freeze controls.** Commit this plan; save binary, existing shader/content
   hashes, commands and environment. Add startup mode admission and receipts.
2. **A only.** Port the private shadow behavior to the eligible lit slot with
   conservative fallback. Run its image, exhaustion, action and window gates;
   measure against current automatic. Checkpoint A separately. Promote its
   automatic selection only if those gates pass.
3. **B only.** Prepare the companion buffer and evaluator behind explicit study
   selection. Run numerical/guard checks before native timing. Record failure
   or no gain as a valid completed trial; stop before further integration then.
4. **B publication and combination.** If B passes, prove coherent resource
   lifetime/failure behavior on admitted paths. Qualify A+B and measure the four
   modes. B/combined remain opt-in; do not claim broader native adoption.
5. **Close the loop.** Run focused SDF/shader/documentation checks appropriate
   to the changes. Commit source, compact reproducible receipts, decisions and
   limitations locally. Verify protected content/defaults and clean scoped
   status. Report actual gains and the remaining worst view.

Rollback is startup `reference` for the current automatic shader, or
`source-shaders=general` for the original strict shader. Keep A and B changes
independently revertible. No gain or failed gates means retain the reference,
not another optimization bundled into the same checkpoint.

## What follows only after this loop

If primary queries still dominate, the next decision is a separate guarded-step
experiment: overlap/backtracking, inside exits, endpoint bounds, far-certificate
transitions and iteration limits must be specified before coding. It may reduce
queries without reducing work after retries; measure both. Finer exact pruning
needs a separate active-reference, peak-memory and publication-cost budget
before allocating another level. Neither follows automatically from missing
16.7 ms. Lower-resolution preview is a quality/product decision; startup caching
addresses a different latency. Windowed root-far admission also needs its own
contract rather than being inferred from a faster headless shader.

The next goal, if requested, is to execute checkpoints 1–5 for A and the bounded
B trial, then close with evidence. It is not an open-ended goal to reach 60 FPS.

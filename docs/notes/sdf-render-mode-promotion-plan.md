# SDF selectable render modes and default promotion

Date: 2026-10-29. Planning baseline: local `5edaa766`,
`sdf-tracing-comparison`. Status: executed; see
[results](sdf-render-mode-promotion-results.md) and
[execution](sdf-render-mode-promotion-progress.md). The contract below records
the original plan; the results explain promotion of root-far Analytical after
the tentative ordinary backend proved too costly.

## Product direction

Keep the rendering choices available and make the best practical compromise
the default. The user finds the combined path's visible degradation minor and
supports considering it for ordinary use. C+N, cached-hit refinement followed
by source CSG normals, is the preferred default candidate. The earlier
sampled-default freeze applied to the research loop; it is not a permanent
product requirement.

The next pass is integration and default qualification. Do not start another
renderer-algorithm search. Preserve original cached, refined cached, C+N,
analytical and hybrid choices; keep numerical study variants available through
advanced controls. Authoring/history formats and canonical geometry semantics
remain unchanged.

The [quality/speed results](sdf-quality-speed-results.md) and
[HTML comparison](http://starship:8787/reports/932bc8269b0a1bfc66894eaa883396cab1397e84142f1aa2199e529de8c60503/)
provide the starting evidence. The [source audit](../evidence/sdf-render-mode-planning-2026-10.json)
pins inspected sources and the preset inventory. All API names below are
proposals, not current CLI support or already adopted implementation details.

## What the source audit changes

1. **The real presets use more samples.** Guardian limestone and Robot fieldkit
   both set `supersample: 2`; 91 of 95 saved presets do so. The recent 4.7 ms /
   8.9 ms C+N Guardian measurements used SS1. SS2 renders four times as many
   pixels internally, but its GPU cost and perceptual benefit have not been
   measured for C+N. Do not extrapolate those SS1 timings into the current
   default preset experience.
2. **The study is deliberately restricted.** C/N selection requires static
   headless authored atmosphere, SS1, debug 0 and no AO/ground/clouds/occluder.
   The app separately restricts source acceleration. Removing one validation
   clause does not qualify the missing paths.
3. **A window can change much more than the camera.** Current controls include
   history seeking, playback, debug views, material edits, specimen changes and
   authored reload. Swapchain recreation rebuilds renderer resources while
   retaining scene geometry. A mode menu must handle those transitions.
4. **Pipeline choice is made at renderer creation.** Updating only the config
   string from ImGui would leave incompatible pipelines, descriptors, field or
   region state. Analytical rendering can omit the roughly 913 MB field;
   returning to a cached mode may have to construct it.
5. **Existing tests need explicit extensions.** `check_authored_window.py`
   currently forces SS1 and its source-contract path assumes analytic/hybrid.
   Its fixed panel coordinates will also move when a mode selector is added.
   The parser already reports explicitly assigned paths, so mode compatibility
   can be handled in SDF without a shared config-framework redesign.
6. **Legacy recipe scenes lack authored CSG input.** Seven saved presets and
   the bare executable's default `sticks` route use box recipes. Source-based
   modes cannot simply be enabled there. Preserve those workflows and identify
   their supported mode explicitly.

## Phase 0: settle rendering mode versus supersampling

Make a narrowly scoped SS2 study admission for uninstrumented cached candidate
rendering. Retain the private headless/static and all other study boundaries.
Leave the 32 MiB geometry diagnostic restriction intact; full 4K internal
geometry/UV capture does not fit that cap. No default changes in this phase.

Use Guardian limestone and Robot fieldkit, preset and close cameras, 1920×1080
output, the existing matched opaque atmosphere setup. Compare S, C, C+N and
the qualified analytical A64 root-far A+B reference at SS1 and SS2. This is 32
configurations; three interleaved runs give a bounded 96-run matrix using the
existing 180/30/150-frame protocol. Retain the existing side/rear/distant SS1
evidence and add a view only if the new results expose a specific gap.

Produce the 32 uninstrumented native output images and short matched arc crops
for the most informative policies. Compare at normal display size and at
enlarged diagonal/thin-edge regions. Keep temporal geometry checks from the
qualified SS1 diagnostic distinct from SS2 native image/time evidence. Do not
claim new SS2 geometry instrumentation or widen diagnostic limits.

The decisions are separate:

- At equal SS, what do C and C+N add in cost and quality over S?
- At equal output size, is C+N/SS1 a better everyday policy than S/SS2?
- Is C+N/SS2 worth its extra cost, or should SS2 remain an explicit high-cost
  option? Also retain C/SS2 as a candidate if it offers the preferable balance.

Use 16.7 ms GPU p95 as the preferred frame target and show the 20 ms line given
the user's earlier view that roughly 50 fps can be workable. Neither number
is a measured interactive FPS promise. An SS2 budget miss does not authorize
quietly reducing supersampling. Present a joint default candidate explicitly
if changing the sample setting is justified. The intended C+N default remains
conditional on these measurements; no further broad research is needed merely
because a few enlarged pixels differ.

## Phase 1: a stable mode selection contract

Proposed public control: `--render-mode auto|cached|cached-refined|balanced|analytic|hybrid`.
Add one mode selector to the current SDF panel, with the same names available
in config files. Keep supersampling as its own option; a mode does not mutate
resolution, shadows, material values or history.

| Choice | Behavior | Cache needs |
| --- | --- | --- |
| Cached | Current primary/normal/secondary sampling | Field |
| Cached refined | C, 0.03125-voxel primary hit refinement | Same field |
| Balanced | C+N; source normals after refined cached primary hits | Field + A64 source-normal regions |
| Analytical | Source-evaluated geometry, normals and secondary queries | Source program; optional existing regions |
| Hybrid | Existing cached-bound/source-near-surface policy | Field + source program; optional existing regions |
| Auto | Resolve the supported default candidate, initially Balanced where qualified | Resolved mode's resources |

Analytical and hybrid must retain their existing public semantics and controls.
Do not silently map hybrid to C+N: they make different visibility guarantees.
The known source optimizations/acceleration settings remain explicit advanced
controls. The proposed main menu does not make the private prepared-cone,
adaptive-step or root-far analytical studies window-safe by implication.

For V1, analytical/hybrid may use their already admitted ordinary runtime
backend while retaining the faster qualified headless study routes. Identify
the backend in receipts and measure the actual public route; do not advertise
the headless A+B timing as the runtime mode's timing. Porting the remaining
private analytical accelerations is a separable follow-up if needed.

Compatibility rules to implement and test:

- With no explicit low-level tracing/study control, `auto` resolves the
  supported default after scene capabilities are known.
- Existing explicit `surface-trace`, `cached-quality`, CSG/region/acceleration
  or study controls retain their old meaning and admission when no named mode
  was explicitly requested. A historical `--surface-trace sampled` must not
  start using Balanced just because the implicit mode is `auto`.
- Explicit named modes and route-affecting low-level controls form a conflict
  unless the controls agree with the mode's resolved settings. Reject a
  contradiction with the relevant option names, including assignments from
  config files. Do not invent cross-option priority that the parser does not
  expose. Ordinary same-option config/CLI/`--set` precedence stays unchanged.
- New generated templates must round-trip through `auto`. The shared schema
  already supports optional bindings and emits absent values as JSON null;
  use project-local optional route overrides (or equivalent omission) instead
  of serializing conflicting legacy defaults beside the named mode. A legacy
  full template with explicit low-level values continues to select its legacy
  route. Keep requested overrides separate from the resolved values consumed
  by existing renderer code; a template round trip must not accidentally opt
  into a study or override the selected mode.
- Legacy field oracles and explicit comparison/profile studies retain their
  specified route. Research tools should set their reference mode explicitly.
- Unsupported explicit mode requests fail early or stay disabled in the UI
  with a concrete reason. `auto` may choose the declared compatible default
  for a recipe or an unqualified optical scope, and must display that result.
  It must not pretend Balanced is active when Cached is rendering.

The user-facing panel should show `Rendering: Balanced` (or the effective
choice), offer the choices, and give a short reason for unavailable ones.
Detailed acceleration identifiers, buffer generations and shader names belong
in diagnostics/receipts rather than the ordinary rendering flow.

This `auto` is a capability/default resolver. It is not a per-frame performance
controller, preview/settled switching scheme or a camera-dependent heuristic.

## Phase 2: qualify the runtime and mode transitions

Promote the existing primary refinement/normal policy without extending it to
shadows, AO rays or internal glass interfaces. Trace each source-normal point
against its own compatible region program. Add the necessary project-local
shader wrappers for supported lighting families; preserve the canonical
tracing helpers and their existing bounded fallback behavior.

Target authored scenes in windowed and headless operation, SS1/SS2, static and
orbit views, authored construction/presentation/material-story playback, the
existing debug modes and ordinary appearance controls. Cover atmosphere,
material-study, material-furnace and material-atmosphere. Include clear and
frosted glass checks because primary source normals meet cached internal
interfaces there. Passing opaque checks does not certify glass. If glass
exposes a transport problem, keep that declared scope on Cached under `auto`
and record an explicit capability limitation; no optical transport rewrite.

Resolve the product mode after authored/workshop capabilities are available
and before validating/allocating the runtime route. Keep comparison-input
studies on their explicit per-fixture route. The present `run_sdf` validates
source selection before constructing `App`, so normalization and validation
order need an intentional project-local change, not a second conflicting
normalization inside the draw loop.

Mode switching must publish a compatible runtime state:

1. Resolve the requested mode and validate it against the published scene.
2. Prepare missing caches, descriptors and pipelines while the previous
   complete mode remains available. Reuse field/region generations when their
   canonical geometry identity matches. Keep one owner for shared resources.
3. Retire preceding in-flight work at the existing project GPU boundary, then
   activate the prepared mode. On failure retain the previous active mode,
   pixels, scene/history position and camera; show the failure without claiming
   that the requested mode was activated.
4. Release resources that the new mode does not need after retirement. Do not
   keep five complete renderers or multiple 913 MB fields resident merely to
   make the selector cheap.

In particular, S↔C↔C+N must not rebuild the field just to select a different
shader. Entering analytical-only mode releases the field after retirement;
returning can incur the existing full field construction cost. Measure and
report that one-time stall. Changing modes must not restart playback, lose
unsaved materials, alter exported geometry or add an undo operation.

Reuse published geometry/appearance identities for reload, undo/redo, branch
edits and specimen changes. Camera, sun, exposure, debug and appearance-only
changes must not rebuild distance bricks or geometry regions. A swapchain
resize may recreate extent-dependent rendering state, but must not bake
geometry again. Diagnose any mode-specific exception rather than silently
accepting it as a general update.

Construct only the pipelines needed by the selected mode and supported debug
fallbacks. Cache warm selections where ownership is bounded, but do not add
all combinations at startup. Record pipeline creation/reuse, startup latency,
switch latency, staging and simultaneous-generation memory. Shader compilation
cost is a real concern in the earlier source studies.

## Phase 3: acceptance and default promotion

### Native behavior matrix

Use existing fixtures and representative saved content rather than replaying
all 95 presets through every permutation:

- Guardian limestone and multi-material Robot fieldkit: all five explicit
  modes, both sample settings, camera orbit/zoom/reset and resize/recreate.
- A small authored build: empty, union, subtraction, smooth overlap, undo,
  redo, branch and resumed playback. Add selected real Robot/Guardian history
  prefixes to exercise full-scene resources without rebuilding every prefix.
- Material workshop: specimen/recipe switching and unsaved material retention;
  opaque marble/wood, rough metal, clear/frosted glass, shared atmosphere,
  furnace, clouded glass and a lighting-cycle example.
- Existing AO, shadow modes 0/1/2, clay, ground/occluder where admitted,
  pattern/bump toggles, debug normal/depth/steps/brick/material views, and cut
  preview. Source modes retain their existing unavailable brick-debug behavior.
- Every directed transition between the five explicit modes on a small scene,
  plus representative cache→source→cache round trips on both large scenes.
  Inject preparation failures for new field, region, pipeline and descriptor
  stages and verify old-state retention, retry and clean exit.
- Real UI mode selection in the owned native-window harness. Extend its mode,
  sample-setting, resize and reload assertions; remove the unconditional SS1
  override. Test window recreation, mode availability and failed reload using
  an owned adjacent scene/config copy, never overwrite canonical scene files.

The Xvfb harness provides automated native lifecycle evidence. Retain ordinary
native motion captures for review, and keep physical-desktop responsiveness
and human visual judgment separately labeled if not available. Update harness
coordinates only after inspecting the actual changed panel; prefer labels or
project-owned action receipts where available.

### Numerical, compatibility and quality gates

- Source analytical/hybrid modes retain their frozen source contracts. Cached
  policies retain their explicitly approximate profile. Do not require missing
  subgrid geometry to pass as a prerequisite for offering Balanced, or claim
  that an option's availability makes it detail preserving.
- Re-run the controlled refinement helper only if its shader inputs or code
  change. Reuse existing fixture/diagnostic evidence through exact source/SPV
  identities where valid; newly admitted behavior needs its own observations.
- A public mode alias and its explicit low-level equivalent produce matching
  selection, native images and resource identities for the same configuration.
  Switching away and back reproduces the settled output; no validation errors,
  unexpected exhaustion, stale generation or lifetime failures are acceptable.
- Keep the 166 original SPVs byte-identical wherever legacy selection promises
  unchanged behavior. Explain an intentional dependency change and requalify
  its affected routes rather than refreshing goldens to hide it.
- Test no-option startup, all five explicit modes, existing low-level controls,
  mode conflicts, supported `auto` resolutions, unsupported explicit requests,
  config templates and CLI/config/`--set` layering. Run relevant SDF, shader
  build and documentation checks; no shared parser rewrite is planned.

### Performance and resource gates

- Preserve raw frame/pass samples and configuration/backend/sample-setting
  identities. Keep GPU jobs serial and owned tests/builds/capture/encoding out
  of timing batches. Record external load and cold/warm setup separately.
- Reuse Phase 0 data while code/SPV identities justify it. After runtime
  integration, repeat the shortlisted default against explicit Cached and the
  actual analytical runtime backend on Guardian/Robot preset, side and close
  views. SS1 and SS2 remain separate results. Do not re-run the entire research
  history for every menu-label change.
- For a public alias at the same settings, investigate a repeated overhead
  exceeding both 5% and 0.25 ms against its low-level equivalent. Keep preferred
  16.7 ms and secondary 20 ms target lines visible without treating present
  synchronization/host timing as physical display latency.
- Preserve the proposed 64 MiB per-generation candidate-metadata and 2 GiB
  overlapping requested-geometry-payload ceilings. Report allocator residency,
  CPU scratch, staging and process/device observations separately. Do not count
  an already resident field as a new field on every mode selection.
- Keep the previous publication stop rule: investigate a repeated geometry
  publication p95 regression exceeding both 25% and 10 ms over matched Cached.
  C+N Robot was already about 23.4% slower at this gate. Fast rendering still
  does not qualify continuous editing or fix the roughly one-second rebuild.

Once the supported runtime/compatibility gates pass, set `auto` to the selected
Balanced policy for its qualified authored scope, update the native guide and
config template, and keep every explicit alternative. If sample defaults also
change, give that its own evidence and clearly state both changed settings.
If a gate fails, retain the newly selectable modes with the previous effective
default for the failing scope and record the concrete remaining work.

## Implementation sequence and completion

1. Commit this contract and source audit before implementation.
2. Measure the bounded SS1/SS2 policy comparison and record the preferred joint
   default candidate. Keep current defaults during qualification.
3. Add project-local mode resolution, compatibility tests, CLI and panel
   selection. Establish staged activation and resource reuse before promotion.
4. Admit and qualify runtime C/C+N behavior, mode transitions, playback,
   reloads, appearance/lighting and both sample settings. Preserve source
   studies as explicit controls with their original unpromoted boundaries.
5. Repeat the selected native runtime performance gates, then promote the
   supported default and update the guide/config template in a distinct commit.
6. Publish a concise evidence/report update with actual runtime paths, mode
   transition receipts, cold/warm costs and remaining capability limits.

Expected edits are confined to `projects/sdf/` (config/mode resolution,
selection, app/renderer lifecycle, wrappers, relevant tests and harnesses) and
its documentation/evidence. A project-local mode module is preferable to more
string conditionals throughout the app. Shared host, parser, material library,
field/region builders and authored schema stay unchanged unless a concrete
dependency makes a small separately reviewed edit necessary.

Completion means the options are usable, the actual default is identified and
qualified for its stated scope, explicit legacy modes remain reproducible, and
the limits are documented. It does not require a new algorithm or proving
every approximation invisible. Keep commits local; no push, project-site
deployment or media/golden replacement. Incremental rebuilding, dynamic
quality adaptation, temporal AA/upscaling, new geometry modifiers and source
detail recovery remain outside this pass.

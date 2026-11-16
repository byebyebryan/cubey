# Dragon glass Q0.1 numerical replay evidence

2026-11-09. [Results and acceptance boundary](../../notes/raytracing-dragon-glass-q01-results.md).
**Q0.1 is qualified for the recorded matrix.** A fixed model-anchored tracing
frame and projected crease offset restore the thin-fold exits. All eleven
strict full-HD captures pass, including six independent 4096-SPP references.
The certified model, physical scene/camera settings, shared lighting and
production budgets are preserved. Q1–Q3 remain separate work.

[Qualified comparison report](http://starship:8787/reports/23d2bfda9d33aa8eda303808cb764931b9779be2b97b2d27de21213e60bec79a/)
shows matched baseline/corrected images and the eleven-capture matrix. All seven
published assets were checked byte-for-byte; comparison buttons, keyboard range
control, image loading and desktop layout were checked in the browser.

## Final qualification

The `completion/` directory owns the accepted candidate evidence:

- `qualification.json`, `qualification-identity.json`: five seed-7 controls and
  six independent view/seed references; artifact hashes, actual raw status maps,
  finite/nonnegative HDR and validation were rechecked by `collect_completion.py`.
- `candidate-build.json`, `normal-build-identity.json`: frozen source, full
  compiler commands, executable/library/shader identities; the ordinary build's
  nine participating shaders and all 36 runtime files match this candidate.
- `checks.json`, `optical.json`, `opaque.json`, `native.json`: twenty RT CTests,
  the existing optical and original opaque-dragon qualifiers and owned-Xvfb
  native edit/reload/orbit/dolly/resize checks.
- `fixture-and-fidelity.json`: two fixed source interfaces miss under the
  baseline and find the expected exits under the correction; fourteen
  non-optical smoke PNGs remain byte-identical.
- `event3288-classification.json`: unchanged-shader, real-prefix readback
  establishes a positive-weight baseline event limit at depth 128; the deeper
  control completes at depth 129. Its target RGB matches the baseline prefix.
- `shadow20-classification.json`, `shadow20-exact-plane-proof.json`: an elevated
  rebase-only prototype's shadow miss, with exact target-RGB fidelity, crosses
  exit primitive 805124 before the recorded spawn segment ends. The combined
  correction passes that view; the local predicate fixture is retained.
- `corrected-journal.json`, `corrected-journal-build.json`: the original logical
  sample on the corrected path has no medium failure or overflow and records
  its fixed tracing origin explicitly.
- `bound-components.json`, `performance.json`, `preservation.json`,
  `runtime-audit.json`: donor-bound decomposition, separate active/settled
  timings, protected hashes and runtime acceptance scope.
- `image-differences.json`: matched before/after raw RGB differences and an
  explicitly scoped independent-seed comparison.

`make_spawn_fixture.py` extracts the exact production reconstruction/spawn
expressions into a private two-boundary diagnostic. It supplies the original
source barycentrics and outgoing directions, then queries the actual certified
model. This is a geometric regression, not a primary camera/sample replay.
The original and corrected fixtures and all source/build/capture data remain in
`outputs/raytracing/dragon-glass-q01-20261109/continuation-01/`.

An optional corrected full-prefix journal rerun was stopped after over ten
minutes of active GPU work. It is not an accepted capture or a performance
result. The required original baseline full/isolated streams are validated;
the corrected isolated probe is clean. Ordinary rendering, with journal code
compiled out, passes its separate qualification and timing checks.

Ordinary optical position records now use `trace = world - trace_world_origin`;
the metric/journal field exposes the fixed origin. CPU exports and physical
scene/camera settings retain their world convention. Other scenes use zero.

## Interim investigation, retained separately

The root-level JSON files and earlier report below describe the first stopped
investigation, including its rejected crease-only candidate and restored
baseline. In particular, root `acceptance.json` is an **interim** receipt, not
the final state. Later qualification is in `completion/` above.

[Interactive review report](http://starship:8787/reports/e8329c35c16c73e15901c249aa050cc2cff262d02495a1dce086bdadc3dadd47/)
retains the local crossing schematic, capture table and ordinary-policy boundary.
Delivery bytes, assets and desktop browser interactions were checked.

## Retained, reviewable evidence

- `fidelity.json`: passive-vs-baseline RGB/status differences, exact passive vs
  isolated packet comparison, single-spawn causal comparison and restored output.
- `journals/seed7-{passive,isolated,causal}.json`: exact uint32 packets with
  uploaded matrices/uniforms and device identity. The first two have identical
  86-record streams; the causal variant changes only the known failing spawn.
- `journals/seed19-medium.json`: first failed logical sample at `(897,320)`, seed
  19, sample 1986. Its base offset crosses the opposing face without any crease
  multiplier selected.
- `journals/seed19-event-search-negative.json`: **nonreproducing** journal search
  at the ordinary event-limit pixel `(671,388)`. It retains the last sample,
  4095, with no failure. Do not interpret this as clearing the ordinary failure.
- `exact-local.json`: rational selected-triangle checks. Positive exit distances
  become negative after the recorded spawn, with valid triangle-interior tests.
  The all-triangle float64 search is a diagnostic, not a universal exact oracle.
- [Minimal face-pair fixture](../../../projects/raytracing/tools/fixtures/ray-spawn-thin-folds.json)
  and [exact predicate checks](../../../projects/raytracing/tools/test_ray_spawn_geometry.py):
  asset-independent retained geometry, shared-edge and grazing/parallel controls.
  Counterfactual smaller offsets demonstrate crossing; they do not certify a new
  origin-error bound. Stanford source attribution is retained in the fixture.
- `captures.json`: thirteen immutable capture receipts with full commands,
  asset/executable/output identities, whole-image transport status and validation.
  Rejected full-HD captures remain explicitly rejected.
- `build-provenance.json`: private baseline/probe/causal/candidate builds, complete
  compiler commands and source/library/executable/SPIR-V hashes.
- `strict-tool-rejection.json`: the new strict tool passes prefix 1335 and
  exits with the expected failure at prefix 1336 on the restored ordinary build;
  its complete shader/material/model identity is pinned.
- `final-probe.json`, `acceptance.json`, `restored-optical.json`: final diagnostic
  stream/memory checks, twenty RT/four documentation CTests, restored 160 x 120
  optical controls and owned-Xvfb native edit/reload/orbit/dolly/resize checks.
  Ordinary seed-19 prefixes 1986/1987 bracket its new failing sample.
- `starting-state.json`, `candidate-decision.json`, `journal-cli-checks.json`:
  preservation identity, candidate rejection and diagnostic CLI boundaries.

Raw images/HDR, metrics, full logs, source snapshots and private objects/shaders
remain under `outputs/raytracing/dragon-glass-q01-20261109/` (ignored build/data
artifacts). The candidate's reconstructed source snapshot exactly matches every
entry in its build-time source hash manifest. Private baseline and candidate
shader directories remain independent of the ordinary build.

The aborted probe-04 scan had an incorrectly encoded search-control bit and was
terminated without accepting output. Probe 05 corrects that wiring. It is not a
qualified result from the aborted scan. Early probe 01 includes journal storage
in resident bytes but predates its separate named metric; later diagnostic
metrics expose `ray_journal_bytes` and pass component accounting.

## Reproduce without modifying live shaders

Run from the RT worktree. A fresh private diagnostic set uses the corrected
current code; the original sample is expected to be clean. The frozen
`final-probe-build-06` executable/shader set retains the failing baseline.
Build a fresh private diagnostic set with
`projects/raytracing/tools/build_ray_replay.py --journal --output NEW_DIRECTORY`.
The private executable takes the original dragon-glass preset, `--headless`,
`--frames 1`, width 1920, height 1080, seed 7 and samples 1336, plus
`--ray-journal OUTPUT_JSON --journal-x 908 --journal-y 344 --journal-sample 1335`.
First capture a full prefix; then add `--journal-isolate` and compare the raw
packet streams. Do not change the logical render to one pixel.

`projects/raytracing/tools/inspect_ray_journal.py` validates and decodes the
versioned packet layout. `--compare` compares exact uint32 event bits. Private
builds require one sharp-glass headless dragon PNG; isolated mode rejects raw
image dumps and first-failure search. `--journal-first-failure` retains the first
failed sample at the selected pixel during a full prefix; packet 1 identifies its
actual logical sample. A zero failure index means no reproduced failure.

`projects/raytracing/tools/qualify_dragon_spawn.py` is an opt-in strict full-HD
regression; `--references` adds the six independent 4096-SPP seed/view controls.
It rejected the interim ordinary route and accepts the final corrected route.
The existing 160 × 120
optical qualifier and native Xvfb checks establish their narrower regressions,
not full-HD acceptance or physical desktop latency.

That interim hold was resolved by the subsequent fixed-frame correction and
qualification above. No arbitrary epsilon reduction, dropped positive-weight
branch, suppressed status or model change was used.

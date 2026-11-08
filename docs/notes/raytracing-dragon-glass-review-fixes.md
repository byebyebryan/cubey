# Dragon glass: review fixes

Commit checkpoint: this follow-up and the first optical pass are being committed
together after review. Earlier `committed: false` receipts describe capture-time
state; their validation identities are retained. No push or public-site update.
The commit normalizes whitespace in the NVIDIA donor/license and captured log
endings. The retained review patch uses zero context; original artifact bytes
remain under the ignored output roots.

2026-11-08, local follow-up to the [first optical pass](raytracing-dragon-glass-first-pass.md).
The fixes cover memory accounting and qualification acceptance. Rendering,
model preparation, material recipes and optical transport are unchanged.

## Requested memory includes adjacency

The edge-adjacency buffer was already allocated, used, freed and included in
`geometry_bytes`, but `Renderer::requested_bytes()` omitted it. Resident totals,
peak totals and the sampling journal now include that allocation. The additive
`edge_adjacency_bytes` metric identifies its size; it remains a subset of
`geometry_bytes`, so component sums must not add it a second time.

Matched 160 × 120 / one-sample captures show both resident and peak totals rising
by exactly **13,928,928 bytes** (13.3 MiB). The PNG files are byte-identical.
The native memory totals in the earlier first-pass receipt are historical and
understate this allocation; the review-fix comparison supplies corrected data.

## Qualification follows the preset

The old script overrode the path budget for its main optical controls and
permitted event/shadow exhaustion in every call. An isolated one-event preset
could therefore pass despite truncation.

The script now starts with a `preset-default` capture before reference overrides.
The default, neutral/tinted, grazing/thin-feature, material and save/reload checks
retain the preset's transport settings and reject event, shadow, total-query,
radiance-query, medium and stack failures. With the raw cutoff verified zero,
zero-weight termination remains permitted. Only explicitly labeled 24/64-event
diagnostic controls may record event/shadow truncation.

The default also must agree with the deeper reference within subject HDR RMSE
`1e-5` at matched resolution/SPP. Each capture verifies resident memory against
the sum of reported allocation components, and verifies peak is at least
resident. Receipts record actual budgets, acceptance policy and memory totals.

The copied one-event preset now fails at the first capture: 3,657 pixels recorded
event exhaustion and 226 recorded shadow exhaustion at 128 SPP. It emits no pass
receipt. The production preset is unchanged.

Seven asset-independent policy tests cover strict accepted views, intentional
shallow controls, non-budget failures, zero-weight termination, adjacency
accounting and peak consistency. They are registered as
`raytracing_dragon_glass_acceptance` in CTest; normal CI needs no downloaded model,
CGAL, NumPy or GPU for this test.

Full captures, baseline executable, the focused patch and failed-preset evidence
remain under `outputs/raytracing/dragon-glass-review-fixes-20261108/`.
No commit, push or public-site update is part of this follow-up.

## Validation

All 19 scoped raytracing CTests pass, including the seven policy regression
cases in the new asset-independent test. `git diff --check` is clean.

The revised 256-SPP qualification passes. All 14 accepted captures retain clean
medium and budget status; the saved reload is checked separately and matches
exactly. The preset's 128 / 1024 / 4096 budget has subject HDR RMSE
`4.09e-10` against the deeper reference. The two shallow diagnostic captures
remain explicitly distinct from accepted views.

[Compact evidence](../evidence/raytracing-dragon-glass-review-fixes-2026-11/README.md)
retains the clean qualification, one-event failure, matched memory comparison,
source identity and preservation checks. Eleven protected original SDF files,
32 existing recipe/preset files, the certified model and RT shader sources
remain unchanged.

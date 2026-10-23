# SDF environment presentation evidence

Date: 2026-10-23

Status: complete with clear sky ready for visual review and clouds explicitly
experimental. See the
[reviewed plan](sdf-environment-presentation-plan.md) for scope and gates.
Commits remain local; no push is authorized for this loop.

## Glass quality checkpoint

The runtime now exposes `glass-crossings` 4/8/16 as a render option, with four
retained as the compatibility default. It changes no material/geometry record
or field build. Config write/reload retains it and invalid values reject.

The 24-angle guardian sweep, including the earlier eight views, reports worst
cap fractions of 1.2433% / 0.1409% / 0.1258% for 4/8/16 crossings. There are no
traversal/progress errors; cap incidence is nonincreasing in each view. Sphere
hero/detail images are identical across budgets. Separated-solid/cavity
fixtures pass the unchanged native validation bounds for every budget.

Fresh sequential 1080p/1x/hard-shadow GPU medians for guardian glass are
2.379 / 2.387 / 2.387 ms for 4/8/16. Sphere values are 2.529 / 2.528 / 2.527 ms.
Each case has 148 completed samples after the established warmup. These are
fixed-environment content costs, excluding field/IBL construction and readback.
Eight meets the retained 1%/8ms gates and is selected for new presentation
presets. Sixteen remains available, with little additional benefit here.

Total internal reflection still terminates using the environment approximation;
raising the crossing budget does not implement extra internal reflection
bounces. The prior four-crossing experimental verdict remains historical.
Artifacts: `outputs/sdf/environment-glass-budget/` and its `profiles/`, with
native commands, configs, hashes, diagnostic images and compatibility receipt.

## Clear-sky integration checkpoint

`material-atmosphere` reuses the shared atmosphere reflection runtime with
previous/current bindings, actual mip counts and a separately normalized SH
diffuse response. The fixed key is replaced by shared primary-light state.
The old generated environment and legacy atmosphere remain separate routes.
The new route synchronously settles its first complete probe through an owned
GPU submission before frame zero; this setup cost is logged separately.

The first native daylight capture exposed the probe's hardcoded `SkyOnly`
ground mode: the lower hemisphere was black. An additive probe ground-mode
policy now defaults to that old behavior for existing consumers, while the intermediate SDF
trial selected `SkyOnlyNoGroundOcclusion`. Review caught that its horizon
wrapping is a backdrop-only policy. The final route uses `Ground` for physical
reflection rays and a separate full-resolution shared sky draw with
`SkyOnlyNoGroundOcclusion` for the visible backdrop. This avoids black lower
reflections without adding a ground plane or using backdrop wrapping for IBL.

Native 128/64-face daylight and low-sun glass captures pass validation. Fixed
guardian clay and four-crossing glass still match the accepted PNGs exactly.
The shared core suite passes after rebuilding against the additive policy.
Artifacts are `outputs/sdf/environment-clear-{check,qualified}/`; the first
directory retains the rejected black-horizon integration trial.


## Frozen-cloud integration checkpoint

The opt-in route reuses shared BrokenCumulus resources, half-resolution
64-step surface integration and a 128-face/64-step environment probe. Weather
is frozen at 300 seconds with coverage 0.28. Complete sky/cloud probes settle
through owned GPU submissions before the first frame. Static/orbit runs retain
one probe generation; sun edits advance coherent sky/cloud generations without
rebuilding the SDF field. The timeline is allowed to blend, but static weather
does not request repeated GPU captures.

The shared depth-aware composite runs after SDF tracing and before tone mapping.
Opaque sculpture depth excludes clouds; glass receives clouds through its
transport environment and is excluded from the background composite too.
Bronze and glass on/off compositor comparisons each test 99,474 interior color
channels with maximum error zero. Frozen first/final frames are byte-identical.
All 28 accepted catalog files retain their baseline hashes. Four legacy
atmosphere and six fixed-material captures retain exact accepted PNG bytes.
Native field/Vulkan validation, invalid inputs and config persistence pass.
Artifacts: `outputs/sdf/environment-foundation/`.

A native window run passed resize, orbit, zoom, material roughness, sun elevation
and Escape checks. Field build count stays one; sun edits advance both probe
generations. Its harness now waits for `windowed_render_ready` before interacting;
the initial harness sent input before the first 11.6-second cloud setup completed.
This was a harness readiness failure, not accepted interaction evidence.
Artifacts: `outputs/sdf/environment-native-controls/`.

The bounded lighting audition settles on 8-degree sun elevation, -155-degree
azimuth and exposure 0.6, shared with the fixed reference. The first low-exposure
trial hid the matte detail. An intermediate azimuth trial used the wrong shared
sun convention (zero points toward -Z); the final direction corrects that error.
Materials and sculpture geometry remain unchanged. Clear sky is the recommended
presentation. Clouds remain experimental: horizon cutoff and visible sampling
patterns do not meet a polished presentation bar. No further cloud look tuning
is included in this loop.

SH diffuse remains a clear-sky approximation, and clouds do not cast sculpture
shadows. Glass retains environment-only exit/TIR approximations. Full-scene GPU
timing includes the visible cloud pass and is reported separately from setup,
field construction, readback and encoding.


## Final comparison and cost

The final capture matrix contains 24 subject/material/environment cases,
48 hero/detail stills, six 240-frame guardian orbits, three empty backgrounds
and three neutral-sphere calibration images. All use the same accepted scene
files, camera contracts, exposure +0.6 and eight crossings. The selected budget
was repeated on the final environment binary: worst cap incidence remains
0.1409% across 24 guardian angles, with zero traversal/progress errors. Its
sphere, separated-solid and cavity controls pass native field validation.
Artifacts: `outputs/sdf/environment-{comparison,final-glass}/`. Final 64-face
clear-sky bronze/glass controls also pass native field/Vulkan validation in
`outputs/sdf/environment-final-extent/`; 128 faces remains the presentation choice.

Fresh sequential runs on the RTX 5070 Ti / driver 610.57.04 use 1920x1080,
1x, hard shadows, 180 frames, 30 warmup and 148 completed GPU samples. New
routes use one enclosing `sdf.whole_scene` timestamp; fixed reference sums its
nonoverlapping shading/post passes. Owned capture/UI jobs had finished before
profiling. Setup, CPU field builds, readback and encoding are excluded.

| Guardian | Fixed median / p95 | Clear sky median / p95 | Clouds median / p95 |
| --- | --- | --- | --- |
| Bronze | 1.553 / 1.655 ms | 2.001 / 2.077 ms | 2.386 / 2.458 ms |
| Glass | 2.702 / 2.875 ms | 2.967 / 3.132 ms | 3.361 / 3.513 ms |
| Marble | 1.580 / 1.674 ms | 2.012 / 2.090 ms | 2.417 / 2.494 ms |
| Terracotta | 1.561 / 1.655 ms | 2.012 / 2.095 ms | 2.399 / 2.503 ms |

The largest median across all 24 cases is sphere glass/clouds at 3.602 ms
(p95 3.738 ms), below the retained 16.7 ms provisional full-scene gate. Warm
final-launch environment setup spans 14.209–57.015 ms; generated test-IBL setup
is 249.850–279.751 ms. Sphere/guardian synchronous field builds span
4.990–921.369 ms. These are distinct CPU wall/setup costs. Early cold cloud
setup reached 10.86 s, and the first native window run reached 11.61 s; those
observations are not an isolated compilation benchmark. Interactive probe
update GPU cost is not independently benchmarked. The steady profiles have
no sun/weather changes. Full numeric and per-pass evidence is in
`outputs/sdf/environment-profile/`.

The full native gallery and orbit frame review support the clear-sky presentation
for user review. Clouds meet the technical/composition/cost gates but retain
the explicit experimental appearance verdict. The artistic verdict remains the
user's. No material, geometry or cloud-model refinement is reopened.

The 17 focused core/SDF/policy checks pass after adding the new evidence page
to the notes index. All 22 selected SDF documentation recipes also pass with
Vulkan ICDs and displays intentionally unavailable, proving CLI dry-run parsing.
Browser discovery returns no connected browser; layout/media interaction cannot
be claimed as browser-qualified. Native films are fully decoded by the capture
checker. Publication and Carbon reachability are recorded separately below.


Report packaging checks cover all 24 environment/material/view control
combinations, film visibility, diagnostic/table construction, JavaScript
syntax and relative asset paths. This is source/interaction-logic evidence,
not a replacement for a connected browser layout or playback review.


## Published delivery

The [shared sky, glass budgets and frozen-cloud report](http://starship:8787/reports/e4177f559cdff0f9f13544c57affe21ef70f6c3db3afe7aad74a64afd582ac1c/index.html)
contains the 24-case gallery, 48 stills, six native movies, calibration controls,
selected diagnostic masks, numeric sweeps, profiles, native interaction receipts
and source snapshots. The immutable bundle is 86,329,668 bytes (82.33 MiB),
575 files. Every file was fetched over HTTP from Starship and matched its local
SHA-256. The publisher reused the shared report server. Delivery receipt:
`outputs/sdf/environment-delivery.json`.

Carbon SSH timed out at connection establishment, so Carbon client reachability
remains unverified. Browser discovery likewise had no connected browser. These
limits do not change the native validation or Starship served-byte checks.
Commits remain local; no push occurred.


## Follow-up: restore shared cloud horizon quality

The initial SDF integration forced `Local` distance mode, bypassing the accepted
shared `Auto` lower-sky horizon handoff. Temporal off, terrain-post edge resolve,
SurfaceVolume density and the improved optical-depth/twilight shading were
already active. The previous explanation placed too much weight on disabled
temporal reconstruction: it is also off in the shared production baseline.

A matched native study compares local/auto, half/full quality, one/two samples,
and terrain-post/metadata-bilateral filtering on unchanged weather, cameras,
exposure, materials and geometry. Auto restores the integrated horizon source,
but cannot alone remove the hard physical planet cutoff against a sky-only
backdrop. The alpha/handoff diagnostics distinguish those two issues.

A trial applying the sky backdrop's ray remapping to structured clouds produced
mirroring and stretched horizon streaks and was rejected. The final correction
keeps cloud rays physical and fades only the visible composite across the same
0–0.08 up-direction interval used by the sky presentation. This is an opt-in
external-backdrop policy. Other shared consumers default to physical composition;
reflection probes explicitly clear external-background state and retain physical
radiance/opacity. The existing uniform ABI is unchanged (background W: standalone
0, external physical 1, external sky-only 2). Foreground depth bypass remains
before cloud filtering/composition.

SDF now defaults to Auto, half resolution, two single-frame samples, terrain-post
resolve and the sky-only backdrop fade. Updated cloud presets save these choices.
No material, density, lighting-response, exposure or weather tuning is included.
Full quality and alternate filtering remain bounded comparison options. Temporal
sampling remains off. `--no-cloud-sky-only` isolates the physical cutoff, and
`--cloud-distance-mode local --cloud-view-samples 1 --no-cloud-sky-only` restores
the first integration's visible settings for comparisons.

Final native qualification, cost and delivery receipts are recorded below.

# SDF environment presentation evidence

Date: 2026-10-23

Status: implementation in progress. See the
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

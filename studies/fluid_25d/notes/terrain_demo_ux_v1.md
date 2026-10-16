# Managed Terrain Water demo V1

Current control/lifecycle reference, with dated validation checkpoints below.
See [current behavior](current_reference.md) for the combined defaults; statements
about changes in this integration refer to its original checkpoint, not every
subsequent rendering or guide promotion.

The prepared terrain launcher owns one external SynxFlow worker and keeps one
Cubey window open across **selection + Apply & restart**. This is not instant
terrain deformation or regridding of existing water. Every replacement starts
with a new identity, dry water fields and paused simulation.

No solver mathematics, shader/material defaults, terrain payloads or native
backend-session schema changed. Cubey's builtin solver and standalone replay or
external-session viewers retain their existing behaviour. The optional native
runtime is still a separate process; this integration does not import CUDA or
GPL solver code into Cubey.

## Using the demo

```sh
rtk proxy python3 projects/fluid/fluid_25d/run_terrain_rain_demo.py \
  --study outputs/fluid/site-expansion-20261011-jtIA3s
```

The **Terrain Water** panel puts Start/Pause, Reset water and Fit simulation patch first.
Space starts the guided response on the qualified Mountain 1k case; in Manual,
or on other sites/grids, it starts/resumes the actual worker normally. While
running, Space pauses the worker. R restarts the current case dry and paused.
Closing this *managed launcher* stops its owned worker. A
standalone `--fluid25d-external-session` viewer still merely detaches on close.

- Simulation: acknowledged rainfall, rain presets/custom amount, requested pace,
  and measured physical-seconds/wall-second pace. Rain Off stops supply, not flow.
  The 128–1024 mm/h presets are deliberately exaggerated demonstration rates.
- Scene setup: current versus pending terrain/grid, Apply & restart and Discard.
  Missing or changed prepared inputs are disabled, never silently substituted.
- View: Scenic, isolated water, flow interpretation, 2D diagnostics, dots/trails,
  and existing advanced render-only appearance settings.
- Composition: only the complete selected simulation grid, with no exterior
  terrain or outer LOD. **Whole patch** retains the `overview` ID; `runoff` and
  `collection` remain the local camera presets.
- How to read: rain covers the whole terrain, water flows downhill and collects
  in basins, and the outer boundary is open. There is no authored point drain.
- Diagnostics: native lifecycle, publication age, session identity/hash, command
  acknowledgement, stop condition and shared rendering/memory statistics.

Visible rain streaks and dots/trails are presentation cues, not conserved water
parcels. Visual rain controls never change actual rainfall. Render FPS, solver
pace and the depth supplied by rain are separate quantities.

### Prepared selections

Mountain valley, Alpine terrain, Rolling hills, Upland basins, Joining valleys
and Steep ridges each have these presets in the expanded prepared study. The
site list comes from the pinned [crop catalogue](../../../projects/terrain_water/tools/terrain_water_sites.json), not
renderer name allowlists. See the [selection study](site_expansion_v1.md) for
matched rainfall evidence and reserves. The old prepared study still works,
with missing new-site cases disabled.

| Group | Grid | Cell size | Coverage |
| --- | --- | --- | --- |
| Same area / coarse | 512² | 32 m | 16.384 km square |
| Same area / default | 1024² | 16 m | 16.384 km square |
| Same area / fine | 2048² | 8 m | 16.384 km square |
| Advanced / smaller | 512² | 16 m | 8.192 km square |
| Advanced / larger | 2048² | 16 m | 32.768 km square |

Source elevation samples are 30 m. Finer simulation grids interpolate that
heightfield; they do not invent finer terrain detail. A 60x pace request is not
a promise, particularly at 2k. Prepared files specify 20 physical hours for finite
diagnostics; normal live worker mode is continuous and does not stop there.
Explicit finite runs report Completed rather than an unexplained pause.

## Replacement and failure contract

The MIT launcher supervisor writes a small separate
`cubey.fluid25d.terrain-demo.v1` control catalogue/status mailbox. The native
backend-session protocol remains unchanged. Requests name only whitelisted
prepared cases; document size, identities, numeric ranges and paths are checked.
One renderer owns the launch-control lock, and one request may be pending.

1. Validate the selected immutable inputs asynchronously while retaining the
   current worker. A preflight failure leaves it running.
2. Stop only the launcher's owned worker; start one fresh worker dry/paused.
   Restore acknowledged rain and pace before handing sole native control to Cubey.
3. Freeze the old scene and decorative motion. Prepare CPU/GPU resources for the
   replacement, checking session/grid/input/bed identities and zero initial water.
4. Drain old submissions and reader/classification jobs before installing the
   new scene. Confirm its exact identity before retiring old session files.
5. Preserve camera for same-site/same-extent resolution changes; otherwise fit
   the full simulation patch. Keep appearance, actual rain and requested pace.

Failures after the old worker stops keep a labelled **Held — not live** view.
An unresolved native command blocks restart while that worker is alive; after
the supervisor confirms its exit, fresh-session recovery may abandon the old
command. This guard and unexpected-worker-exit recovery have CPU contract tests.
Retry or restart the previous case creates dry water, not rollback of the old
water state. A failed numerical/upload audit during retirement is fatal rather
than presenting a broken old scene as recoverable. Normal successful switching
retains current plus one previous session; repeated failed installations may
retain additional artifacts until a successful installation or manual cleanup.
The worker log is retained for diagnosis rather than rotated automatically.

Shutdown joins pending preparation and releases unused GPU products while the
device still exists. The launcher terminates only its owned process groups,
with receipts. No attach to or kill of unrelated workers is attempted.

## Headless validation

`--headless --run-seconds N` uses the offscreen Vulkan host, with display/session
environment variables removed. No X11, Xvfb, GUI, readback or video encoding is
in the continuous measurement path. `--switch-script actions.json` is explicitly
headless-only; it drives the same restart/native control methods and checks
bit-exact imported bed/depth/velocity buffers at each retirement.

```sh
rtk proxy python3 studies/fluid_25d/harness/validate_terrain_demo_v1.py \
  --study outputs/fluid/live-perf-terrain-20261010-Dd6K3h \
  --out outputs/fluid/terrain-ux-validation-FRESH \
  --script /absolute/path/actions.json --seconds 150 --width 1920 --height 1080
```

Example script (time is wall time after prior action acknowledgement):

```json
{"schema":"cubey.fluid25d.terrain-demo.v1","actions":[
  {"kind":"resume","after_s":1},
  {"kind":"pause","after_s":10},
  {"kind":"switch","after_s":1,"case_id":"rolling-patch-512-32m",
   "rain_mm_per_hour":256,"pacing":60},
  {"kind":"reset","after_s":1}
]}
```

Other actions are `set-rain` (mm/h `value`) and `set-time-scale` (pace `value`).
`camera` selects `overview`, `runoff` or `collection` on the existing Mountain
site. `guide-start` and `guide-cancel` exercise the guide methods without opening
a native window. `start` exercises the primary GUI/Space routing; `manual-start`
selects Manual and uses the same routing. Existing `resume` scripts remain manual
and never implicitly run a guide.
Each action requires exactly one timing key: `after_s` is wall time after the
previous acknowledgement; `at_physical_s` is an absolute simulated timestamp
in the current session. The regression-script parser bounds these triggers to
0–72,000 seconds; that is not the continuous live solver's stopping condition. For example,
`{"kind":"set-rain","at_physical_s":7200,"value":0}` stops supply at the
first eligible synchronized boundary after two simulated hours, without
pausing flow. Cold setup and slower solver pace cannot fire this trigger early.
Scripts must finish and receive every acknowledgement before the bounded run
ends; an incomplete script fails explicitly. This is useful for shutdown tests
but is never a successful switching trial. Performance summaries filter running
lifecycle; mixed-grid switching trials are not steady-state comparisons.

### Default rain-response guide

On the measured native bed of the prepared Mountain / 1024² / 16m case,
**Guided rain response** is selected by default. **Start demo / Space** starts
only from healthy dry/paused water. It acknowledges 512 mm/h and 60x
before resuming; follows the existing Whole patch / Downhill runoff / Basin
collection cameras; turns rainfall off after two simulated hours; and completes
its explanation at six hours **without pausing the solver**. Startup remains
dry/paused with no autoplay or forcing change until Start. **Manual** preserves
the acknowledged rain/pace and your camera, with no scheduled shutoff. Ordinary
Resume after progress does not restart the narrative. Other sites, resolutions,
replay and existing headless `resume` scripts keep their prior behaviour.

The guide waits for each matching applied acknowledgement. Camera changes use
physical time, not renderer FPS or elapsed setup time. Manual solver control,
camera selection or orbit/zoom cancels the guide and selects Manual. Reset or
replacement cancels an active guide while retaining the chosen start mode.
Leaving it retains applied rain/pace and does not rewind water; resetting creates new dry
water. Choose Guided again on healthy dry/paused Mountain water to repeat the
narrative; it explicitly restores 512 mm/h. Manual Reset preserves rain, so a
rain-off reset needs a rain preset before it can wet up. Scene replacement does
not carry an active guide into another terrain.
The prepared 20-hour input horizon applies only to explicit finite diagnostics;
continuous live operation still retains float-clock/health limits.

See [rain-response evidence and qualification](rain_response_v1.md). The phase
captions explain actual rain supply and delayed upstream arrivals; animated
rain, foam and dots are visual aids, not numerical water parcels.

Evidence/report: `outputs/fluid/terrain-ux-v1-20261010-CJjqek/index.html`, served
by the existing output report server on port 8801. Automated lifecycle and GPU
evidence do not constitute native GUI layout/interaction or human acceptance.

### Validated checkpoint (2026-10-10 Starship run)

- Final matrix: all three sites and all five grid/coverage presets; 15 actions
  acknowledged, eight distinct sessions, every installation dry/paused. Eight
  bit-exact imported-field GPU audits passed, with zero viewer hydraulic solves.
- Five consecutive default-grid resets: six distinct sessions and six upload
  audits passed. Sampled active renderer RSS fluctuated around 433–617 MiB,
  ending around 617 MiB before shutdown; not monotonic growth. Returning to 1k
  after the large-grid matrix retained about 678 MiB. These are bounded samples,
  not a guarantee of zero leaks over arbitrary operation or immediate release
  of allocator/driver capacity.
- Interrupted loading: the 7-second bounded run ended during new-worker startup.
  It failed with the expected incomplete-script message, not a successful switch;
  both owned worker groups were cleaned up without Vulkan validation errors.
- Running 1k/16 m, 1080p, 60 Hz cap: separate 90-second run, 20-second running
  warmup excluded, no overlapping regression traffic, validation layers explicitly
  disabled to match the previous profiling setup. Actual pace **60.01x**, CPU
  frame-service p95 **2.68 ms**, GPU scene p95 **5.30 ms**, advancing displayed
  field age p95 **0.144 s**, frame interval p95 **16.736 ms**. This meets the
  headless frame budget; it is not a native-display FPS or precise overhead claim.
- A validation-enabled 180-second running trial also passed; its early portion
  overlapped the GPU regression suite and is retained separately, not pooled
  with the clean performance trial.
- Pinned 1k and 2k saved-state PNG captures are byte-identical to the previous
  renderer. The visual/numerical recipe did not change.
- 21 focused CTest checks, the final four managed-contract checks, and 32 Python
  checks passed. Changed C++ formatting and `git diff --check` passed.
- Successful switching retains exactly current plus one prior session. Cleanup
  receipts and a fresh process-group check found no owned processes left.

The first matrix collector mistakenly expected a nonexistent cleanup field and
omitted profile aggregation; its raw error receipt is preserved. The independent
`review.json` audit verifies renderer exit, actual `active_group_remaining`
arrays, identities and upload logs. The collector was fixed before later trials.
It now reports throughput only for an uninterrupted resume-only script; reset,
pause and switching tests are not throughput measurements.

Remaining acceptance: native GUI layout, interaction/camera behaviour and human
review. No GUI was launched or push performed in this loop.

### Patch-only contract (2026-10-13 follow-up)

Fluid no longer prepares/uploads exterior terrain for world-site bindings, and
camera bounds/shadows use only the simulation patch. Shared site/source admission
and climate-derived material organization remain validated; the Terrain
foundation's wider backdrop and captured-water LOD are unchanged. The wider
terrain checkbox is removed. Explicit legacy context `off` remains supported;
`on` fails clearly before GPU startup. All five prepared grid/area presets remain.

The [patch-only review](patch_only_v1.md) records replay parity, managed switching
and two separate steady live trials. It does not promote a camera, material,
solver or Terrain startup change. Native panel/layout acceptance remains deferred.

# Managed Terrain Water demo V1

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
  --study outputs/fluid/live-perf-terrain-20261010-Dd6K3h
```

The **Terrain Water** panel puts Start/Pause, Reset water and Fit overview first.
Space starts or pauses the actual worker; R restarts the selected current case
dry and paused. Closing this *managed launcher* stops its owned worker. A
standalone `--fluid25d-external-session` viewer still merely detaches on close.

- Simulation: acknowledged rainfall, rain presets/custom amount, requested pace,
  and measured physical-seconds/wall-second pace. Rain Off stops supply, not flow.
  The 128–1024 mm/h presets are deliberately exaggerated demonstration rates.
- Scene setup: current versus pending terrain/grid, Apply & restart and Discard.
  Missing or changed prepared inputs are disabled, never silently substituted.
- View: Scenic, isolated water, flow interpretation, 2D diagnostics, dots/trails,
  and existing advanced render-only appearance settings.
- How to read: rain covers the whole terrain, water flows downhill and collects
  in basins, and the outer boundary is open. There is no authored point drain.
- Diagnostics: native lifecycle, publication age, session identity/hash, command
  acknowledgement, stop condition and shared rendering/memory statistics.

Visible rain streaks and dots/trails are presentation cues, not conserved water
parcels. Visual rain controls never change actual rainfall. Render FPS, solver
pace and the depth supplied by rain are separate quantities.

### Prepared selections

Mountain valley, Alpine terrain and Rolling hills each have these presets:

| Group | Grid | Cell size | Coverage |
| --- | --- | --- | --- |
| Same area / coarse | 512² | 32 m | 16.384 km square |
| Same area / default | 1024² | 16 m | 16.384 km square |
| Same area / fine | 2048² | 8 m | 16.384 km square |
| Advanced / smaller | 512² | 16 m | 8.192 km square |
| Advanced / larger | 2048² | 16 m | 32.768 km square |

Source elevation samples are 30 m. Finer simulation grids interpolate that
heightfield; they do not invent finer terrain detail. A 60x pace request is not
a promise, particularly at 2k. The prepared native case has a finite 20-physical-
hour horizon, which is shown as Completed rather than an unexplained pause.

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
   the overview. Keep appearance, actual rain and requested pace.

Failures after the old worker stops keep a labelled **Held — not live** view.
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
rtk proxy python3 projects/fluid/fluid_25d/validate_terrain_demo_v1.py \
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
Scripts must finish and receive every acknowledgement before the bounded run
ends; an incomplete script fails explicitly. This is useful for shutdown tests
but is never a successful switching trial. Performance summaries filter running
lifecycle; mixed-grid switching trials are not steady-state comparisons.

Evidence/report: `outputs/fluid/terrain-ux-v1-20261010-CJjqek/index.html`, served
by the existing output report server on port 8801. Automated lifecycle and GPU
evidence do not constitute native GUI layout/interaction or human acceptance.

# Rain-response demonstration V1

Scope: existing Mountain / 1024² / 16 m / 16.384 km patch, unmodified generated
elevation, uniform 512 mm/h demonstration rain, requested 60x pace and fixed
daylight. No new solver, terrain, material, backdrop or startup autoplay.

## Hydraulic study checkpoint

`run_rain_response_v1.py` supervises the existing managed launcher and offscreen
Vulkan viewer. Controls run through the same C++ client as the GUI. Rain-off is
triggered at 7,200 physical seconds and applies at a synchronized solver boundary;
the observer retains exact depth/momentum packets roughly every 60 physical
seconds, with explicit acknowledgement and final endpoint observations.

The bounded study explicitly pauses at its six-hour endpoint for capture and
owned shutdown. That is study automation, not the guide's end policy.
Observer I/O and field archiving are excluded from throughput claims. The
archive is capped at 512 snapshots (approximately 6 GiB at 1k), with a finite wall
deadline and cleanup receipts; incomplete horizons fail rather than qualify.

```sh
rtk proxy outputs/fluid/native-runoff-reuse-v1-20261002-trm3Ve/synxflow-setup/env/.venv/bin/python \
  studies/fluid_25d/harness/run_rain_response_v1.py \
  --study outputs/fluid/site-expansion-20261011-jtIA3s \
  --out outputs/fluid/rain-response-FRESH
```

Add `--rain-on-control` for the same dry start and six-hour run without shutoff.
Use the existing optional NumPy environment; no dependency installation or
native extension import occurs in the client-side observer. Standard-library
schedule tests run without NumPy; numerical metric tests run separately in that
existing environment.

The first 1k study retained 335 exact snapshots. Rain-off applied at
7,203.878 seconds; the paused endpoint was 21,604.482 seconds. Whole-map stored
water fell from 77.79 to 33.86 million m³ (56.47%). Material wet coverage
(`h >= 0.1 m`) shrank from 36.75 to 2.95 km². The fixed approximately 2 km-wide
runoff-camera region lost 98.40% of its stored water. The fixed collection-camera
region initially grew another 4.84%, peaking at 8,427.617 seconds as upstream
water arrived, before slowly receding. This is delayed collection and drainage,
not proof of a calm lake or calibrated hydrology.

Initial numerical evidence: `outputs/fluid/rain-response-v1-20261014-a1`.
Its first recording metadata omitted the established binary-capture provenance
fields. `--reseal` rechecks case identity, every field hash and native bed, then
creates a fresh recording leaf without rerunning the solver or overwriting the
original evidence. Corrected recording: `outputs/fluid/rain-response-v1-20261014-a2`.
The review harness reconstructs each original packet header and checks its
SHA-256 against the observed service publication.

Recording rain knots use a one-microsecond **display-only** taper ending at the
actual shutoff acknowledgement. Exact field payloads are unchanged; these knots
are not an exact native source ledger or a conservation certificate. Hydraulic
states are held between irregular snapshots, never physically interpolated.

## Default guided entry (promoted)

The guide is selected by default only for the measured native bed identity, not
a different crop that happens to reuse the Mountain case name. It uses the same
acknowledged solver controls and the existing Whole patch / Downhill runoff /
Basin collection cameras. It starts only from a healthy
dry/paused Mountain 1k session, sets 512 mm/h and 60x, and resumes the solver.
Its camera milestones are 0.5h, 1h, 2.5h and 3h; rain stops at 2h. At 6h it
finishes its explanation while the solver continues draining. No automatic
pause, stop, reset or looping is part of the guide. Manual solver control,
camera selection or orbit/zoom exits it and selects Manual; reset/replacement
cancels it while retaining the chosen start mode. Startup remains dry/paused:
**Start demo / Space** launches the narrative, without autoplay. **Manual** keeps
ordinary continuous simulation and your selected rain/pace/camera; other sites,
grids, replay and existing headless `resume` scripts stay unchanged.
Reset retains acknowledged rainfall. Guided Start explicitly restores 512 mm/h;
after a rain-off reset, Manual Start needs a rain preset to wet up. Ordinary
Resume after physical progress never restarts the guide.

## Qualification and verdict

The pre-promotion Release renderer (`86015f7af9538171d6f9ebf80bd967830e45ffc26b08e4441220c37394b0714a`)
passed the complete nine-milestone guide. It completed at 21,600 physical seconds
with rain off, then continued advancing before the **test fixture's separate**
pause at 21,729.990 seconds. Manual rain and camera overrides cancelled two
guides with no subsequent automatic camera changes; two resets installed fresh
dry sessions and a later ordinary Resume did not start a guide. Native GPU
bed/depth/decoded-velocity audits passed at all three scene retirements.

An uninterrupted 90-second 1920x1080 headless live trial, excluding the first
20 seconds of running warmup, measured GPU p95 **5.15 ms**, CPU service p95
**2.78 ms**, advancing-field age p95 **0.143 s**, and achieved pace **59.999x**.
It had no capture, encoding or observer field archive. These pass the existing
8 ms GPU / 4 ms CPU / 1 s age / 54x minimum gates; native display/scanout FPS is
not inferred from this result. No solver, water/terrain shader or default change
was made. One fixed-daylight reference capture remained pixel-exact.

Eleven scoped CPU groups and three hardware GPU groups passed. The default
Python helper group skips its numerical metric test without NumPy; both helper
tests separately passed in the existing optional NumPy environment. Native GUI
layout/interaction review remains deferred.

The continuous-rain control retained 338 exact snapshots. At approximately six
physical hours it stored **93.89 million m³** versus **33.86 million m³** after
the matched two-hour storm plus four-hour recession. Its collection-camera
region held 32.33 versus 21.92 million m³. Early checkpoints agree closely but
are not bit-exact parity claims: the observer records actual solver-boundary
times, with comparison samples within 75 seconds of requested physical times.

Four 30-second recorded clips show 333 or 336 distinct native hydraulic states
each, with twelve matched stills. The timelapse is 720x, not a live-FPS capture;
water fields are held between saves, and foam/dots/rain remain visual cues.
The browser comparison button, media decoding, measured timeline and layout
passed review. The initial display taper remains only rainfall-presentation
metadata; physical water fields are never interpolated or amplified.

The promoted entry uses renderer
`81284d3e4a716d27ebb584544820aae15b5e4f9bb743d0727b20d205aff2eeac`.
An 80-second headless lifecycle trial exercised the shared Start/Space route:
one guided start, three applied setup commands, manual rain override to 128 mm/h,
one cancellation, then a dry reset and ordinary Start retaining 128 mm/h. No
automatic guide camera fired after the override. Separate 25-second manual-start
and 20-second idle-start trials started no guide; the idle trial stayed at
physical time zero. All owned-process cleanup passed, and native GPU import
audits remained bit-exact. Eleven scoped CPU groups and three hardware GPU
groups passed. These are entry/lifecycle checks, not a new six-hour hydraulic or
throughput study; the measured guide sequence, terrain and shaders are unchanged.
GUI interaction/layout review remains deferred.

Verdict: promote the measured guide to the primary Start action on its qualified
Mountain site, retaining Manual and current rendering defaults.
The clearest story is stream formation and retreat, plus delayed basin response;
do not promote the remaining collection to a certified quiet lake. Ordinary
live operation, replay, other prepared sites and the builtin solver are retained.

[Browser review: rain becomes runoff, then drains](http://starship:8787/reports/9072f6efec32e982a19c3cac339e657af57906cd8efb059bdd5e192503fe7817/).
Local evidence:

- `outputs/fluid/rain-response-v1-20261014-a2` — corrected exact recession recording.
- `outputs/fluid/rain-response-v1-20261014-rain-on` — continuous-rain control.
- `outputs/fluid/rain-guide-v1-20261014-final` — frozen final guide trial.
- `outputs/fluid/rain-guide-v1-20261014-override` — overrides, reset and ordinary resume.
- `outputs/fluid/rain-guide-v1-20261014-performance` — uninterrupted live performance.
- `outputs/fluid/rain-guide-promotion-20261014-final` — promoted Start, override/reset,
  CPU and hardware GPU checks on the final binary.
- `outputs/fluid/rain-guide-promotion-20261014-manual` — explicit manual Start.
- `outputs/fluid/rain-guide-promotion-20261014-dry` — idle startup without autoplay.
- `outputs/fluid/rain-response-review-v1-20261014` — captures, GPU tests, fixed reference,
  browser review receipt and self-contained export. Earlier `full`/`qualified`
  trials are development evidence, not the frozen final binary qualification.

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
owned shutdown. That is study automation, not the optional guide's end policy.
Observer I/O and field archiving are excluded from throughput claims. The
archive is capped at 512 snapshots (approximately 6 GiB at 1k), with a finite wall
deadline and cleanup receipts; incomplete horizons fail rather than qualify.

```sh
rtk proxy outputs/fluid/native-runoff-reuse-v1-20261002-trm3Ve/synxflow-setup/env/.venv/bin/python \
  projects/fluid/fluid_25d/run_rain_response_v1.py \
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

## Intended optional guide

The guide uses the same acknowledged solver controls and the existing Whole
patch / Downhill runoff / Basin collection cameras. It starts only from a healthy
dry/paused Mountain 1k session, sets 512 mm/h and 60x, and resumes the solver.
Its camera milestones are 0.5h, 1h, 2.5h and 3h; rain stops at 2h. At 6h it
finishes its explanation while the solver continues draining. No automatic
pause, stop, reset or looping is part of the guide. Manual solver control,
camera selection, orbit/zoom or scene reset exits it without fighting the user.

Native GUI layout/interaction review remains deferred. Headless qualification,
matched rain-on captures and fresh performance receipts are separate gates.

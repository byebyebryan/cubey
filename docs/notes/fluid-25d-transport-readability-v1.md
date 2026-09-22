# Fluid 2.5D Transport Readability V1

Date: 2026-09-22  
Status: runner and review protocol prepared; evidence is pending the
Transport Inspection presentation slice

## Study question

Can a bounded conservative dye pulse make the source-to-outlet story legible
in motion, while the hydraulic solve remains the same finite-volume water
solve? The study is deliberately narrower than a general fluid-feature
evaluation. It uses one authored compact scene, one fixed pulse, one fixed
camera, and a fixed 900-step trace. It does not select terrain, change the
River V0 default, or claim that the dye model is a general advection product.

The reproducible runner is
[`run_transport_readability_v1.sh`](../../projects/fluid/fluid_25d/run_transport_readability_v1.sh).
It accepts one optional output-directory argument and otherwise creates a
timestamped root under `outputs/fluid/transport-readability-v1-*`. `SUBSTEPS`
defaults to `16` and can be overridden for a repeat run (for example,
`SUBSTEPS=32 .../run_transport_readability_v1.sh <output-dir>`). The long
evidence run must be made only after the Transport Inspection view is present
and reviewed.

## Fixed protocol

- `source-outlet-demo`, finite-volume solver only;
- `128x64` grid with one-metre cells;
- closed outer boundary, authored SOURCE centroid `(14,32)`, and OUTLET
  centroid `(110,32)`;
- one fixed outer step of `1 s`, with 16 initial solver substeps;
- 900 post-step frames at 30 fps (30 seconds of presentation time, 900
  simulated seconds);
- hydraulic source remains active at the configured `0.03 m3/s` throughout;
- unit dye concentration `1.0` is injected only for the half-open interval
  `[300 s,360 s)`; the diagnostics-only material-front threshold is `0.01`;
- no floating objects, rain, terrain audition, water-table forcing, or
  per-candidate tuning.

The pulse is independent of hydraulic forcing. The first dyed result is
capture `f301`: `f300` is the last pre-dye result, `f360` is the last dyed
step, and `f361` is the first undyed step. This follows the runtime's fixed
step schedule rather than a wall-clock approximation.

## What the runner writes

The runner invokes the app once, so the video and profile metrics describe the
same state trace. A successful root contains:

- `transport-inspection.mp4`, checked as H.264, `yuv420p`, 1280x720, 30 fps,
  and exactly 900 frames. Its nominal presentation duration is 30.0 seconds;
  the metadata records the actual container duration separately because H.264
  timestamps may end one frame before the nominal `frame_count / fps` value;
- `profile/transport-readability.{frames.csv,passes.csv,metrics.csv,trace.json,summary.txt}`;
- `transport-profile.csv`, a normalized one-row-per-step table with water and
  tracer ledgers, concentration, centroid, and downstream extent;
- `checkpoints/` extracted from the encoded video itself, not independently
  recomputed runs, plus `checkpoints.csv` mapping capture frame to decoded
  index and simulation time;
- `review/contact-sheet.png` showing the semantic checkpoints;
- `acceptance.txt`, `metadata.txt`, `commands.txt`, the ffprobe report, and a
  SHA-256 manifest verified with `sha256sum -c`.

The semantic mapping is:

```text
decoded video index N-1 -> capture frame N -> post-step time N seconds
profile row N-1        -> capture frame N
```

The runner also adds the first measured material outlet-arrival frame to the
checkpoint set. Arrival is measured from the cumulative tracer sink ledger,
using `max(1e-6 m3, 1e-6 * final cumulative tracer source amount)`; it is not
inferred from a visible pixel or from the configured outlet capacity.

## Acceptance contract

The report requires all 900 profile rows and zero finite-volume status flags.
It checks that every reported concentration stays in `[0,1]`, the closed
boundary has no tracer outflow, and the final cumulative source amounts are
consistent with the fixed protocol:

- hydraulic water source: `0.03 * 900 = 27.0 m3`;
- dyed tracer source: `0.03 * 1.0 * 60 = 1.8 m3`.

The injected concentration and the reporting threshold are intentionally
separate. `1.0` is the source schedule value used by the conservative tracer
ledger; `0.01` only classifies cells as materially dyed for dyed-cell count and
downstream-extent reporting.

The acceptance report also requires the `f300` profile row to have effectively
zero tracer total, source, sink, and boundary amounts before the pulse begins.
The zero guard, source tolerance, boundary tolerance, conservation limit, and
material-arrival threshold are printed explicitly in `acceptance.txt`.

The source checks use a fixed one-percent relative tolerance to accommodate
GPU float ledger accumulation. Maximum absolute tracer conservation residual
must remain below `0.01%` of the final tracer source. A first material outlet
arrival must be measured after the dye start (`f>300`) and by the final frame;
the actual frame and threshold are recorded in `acceptance.txt` and
`metadata.txt`. The runner also records the water residual as context, but the
study gate is the tracer conservation result.

## How to read the result

Transport Inspection is a source-to-outlet explanation surface, not a
velocity/quiver diagnostic:

- green SOURCE ring: where the hydraulic source and dye pulse enter;
- amber OUTLET ring: where the explicit sink removes water and, after arrival,
  dyed tracer;
- base blue: water depth and terrain context, not dye concentration;
- magenta/violet packet: conservative dyed water, with stronger tint meaning
  a higher tracer-to-water concentration;
- movement from left to right: the dyed packet is being advected by the
  simulated water route. It is not a particle path or a new water body.

Read the checkpoints in this order: reset/early continuity, `f300` versus
`f301` for pulse onset, `f360` versus `f361` for pulse end, the measured
arrival frame for outlet response, and `f900` for the late state. The profile
centroid and downstream extent quantify what the video suggests; cumulative
source, sink, boundary, and residual fields establish whether the visual
story is backed by the conservative ledger.

## Evidence boundary

Until the runner is executed against a reviewed Transport Inspection build,
there are no V1 capture claims or measured arrival times. The runner rejects a
pre-existing output root so stale products cannot enter a new manifest. A
successful run is
an opt-in readability study for the compact authored source/outlet demo. It
does not promote dye into virtual-pipes/default behavior, imported terrain
auditions, rain dynamics, floaters, or a shared generic tracer framework.

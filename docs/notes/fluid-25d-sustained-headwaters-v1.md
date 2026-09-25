# Fluid 2.5D sustained-headwaters control V1

This is an opt-in, authored numerical control for the question “can continuous
water inputs make two initially dry tributaries join and persistently discharge?”
It is not a Terrain Diffusion site, a rainfall catchment, a calibrated river,
or a change to the default virtual-pipes scenario. The terrain is immutable
during the solve. The separate [evidence directory](../../outputs/fluid/sustained-headwaters-v1-20260925/metadata.txt)
records the exact app and runner hashes from a dirty worktree; HEAD alone does
not identify this run.

## Protocol and reading guide

- Grid: 65×33 cells at 4 m spacing (256×128 m between outer cell centers).
  Initial water depth is zero everywhere.
- Two 3×3 patches centered on `(4,8)` and `(4,24)` each supply 0.25 m³/s
  continuously. Their graded, banked reaches meet near `(40,16)` and flow
  through a common trunk. There is no explicit water sink. Only the three
  east-facing boundary faces at `y=15..17` are open, and they are outflow-only.
- Finite-volume solver: 1 simulated second per capture frame, 32 substeps
  (0.03125 s each). The hydraulic review lasts 900 s. A separate conservative
  tracer pulse colors water from both sources over the half-open interval
  `[900,960)` s without turning off either water source; its review lasts
  1800 s.
- Green rings mark continuous sources; the amber semicircle marks the open
  outlet at the right edge. They are render-only. Composite uses a 4×
  render-only height scale and a bank/elevation tint; neither changes solver
  elevations. Water Isolation suppresses the bed. Flow Inspection's arrows
  are fixed-grid velocity samples, not particles. Magenta in Transport
  Inspection is advected tracer concentration, not a procedural animation.

Watch the [dry-to-mature Composite video](../../outputs/fluid/sustained-headwaters-v1-20260925/captures/dry-to-mature-composite.mp4)
or [Water Isolation video](../../outputs/fluid/sustained-headwaters-v1-20260925/captures/dry-to-mature-water-isolation.mp4)
first, then the [dye-transport video](../../outputs/fluid/sustained-headwaters-v1-20260925/captures/headwaters-dye-transport.mp4).
Each is about 30 s long. For quick comparisons, see the
[dry start](../../outputs/fluid/sustained-headwaters-v1-20260925/captures/dry-start-composite-f001.png),
[developing water](../../outputs/fluid/sustained-headwaters-v1-20260925/captures/developing-water-f300.png),
[mature flow field](../../outputs/fluid/sustained-headwaters-v1-20260925/captures/mature-flow-inspection-f900.png),
[dyed branches](../../outputs/fluid/sustained-headwaters-v1-20260925/captures/transport-branches-f1200.png),
and [dyed outlet](../../outputs/fluid/sustained-headwaters-v1-20260925/captures/transport-outlet-f1500.png).
Capture `fN` is after `N` one-second steps; profile index `N-1` samples that
same state.

## Measured outcome

The [hydraulic profile](../../outputs/fluid/sustained-headwaters-v1-20260925/profiles/sustained-headwaters.metrics.csv)
first samples branch depths above 2 mm at frame index 232 (233 s), the
confluence at 319 (320 s), the trunk at 435 (436 s), and the outlet at 522
(523 s). These are sampled upper bounds on arrival, not exact wet-front times.
Small boundary discharge first appears in the sampled series at index 493
(494 s), before the outlet cell crosses the 2 mm reporting threshold.

At 900 s, the two source patches have supplied 449.910 m³ by the GPU ledger
against a nominal 450 m³. The scene stores 298.385 m³ and has discharged
151.562 m³ through the aperture; the water sink ledger and all non-east
boundary bins are zero. The visible conservation residual is +0.037 m³
(0.0083% of nominal input), and sampled finite-volume status flags are zero.
The maximum depth is 0.082 m, so this is a shallow stream-control scene, not a
near-bankfull river.

The last four 29-second outlet rates are 0.469, 0.476, 0.482, and
0.486 m³/s. Their 116-second mean is 0.479 m³/s, 4.3% below the 0.500 m³/s
nominal input; storage is still increasing slowly. This passes a bounded
near-balance gate, **not** an exact steady-state claim. At 900 s the branch
`y` velocities are +0.0479 and −0.0479 m/s toward the join; trunk `x`
velocity is +0.301 m/s and outlet `x` velocity is +0.320 m/s. The complete
outflow is attributed only to east bins 7 and 8, covering the marked aperture.

The [tracer profile](../../outputs/fluid/sustained-headwaters-v1-20260925/profiles/headwaters-dye.metrics.csv)
reports 30.0006 m³ dyed input. By the sample at 1799 s, 29.9402 m³
(99.80%) has crossed the open boundary and 0.0554 m³ remains in-domain; no
cell exceeds the 1% material-concentration reporting threshold. It also
reports 0.0048 m³ of tracer “sink” despite zero configured sink and zero water
sink, with a −0.00019 m³ tracer conservation residual. That tiny sink entry
is retained as an unresolved numerical/accounting discrepancy, not described
as physical removal. Boundary outflow is the discharge evidence.

The focused CPU test covers dry construction, both branches, confluence,
trunk, eventual outlet discharge, CFL, finite/nonnegative depth and per-step
water balance over 600 s. A bounded pulse test checks tracer injection from
both sources and continuous water supply after the pulse. A 961-step
[GPU/CPU oracle comparison](../../outputs/fluid/sustained-headwaters-v1-20260925/logs/dye-gpu-oracle.log)
passes with zero status flags and maximum final depth and tracer-field errors
of about 5×10⁻⁷ m. This is final-state parity through the pulse, not an
independent 1800-step CPU validation.

## Transfer gate

Every currently pinned Terrain Diffusion heightfield manifest in
`cache/terrain/sources/v1` identifies a native 30 m sample spacing. The
control needs 4 m cells to represent two short, separate tributaries and
their narrow banks. Interpolating the 30 m raster would not supply genuine
4 m channel geometry, so this result does **not** promote a natural-terrain
source/outlet fixture. The existing RainPulse protocol instead rains across
its entire crop and opens every perimeter face; it is a separate distributed
runoff study, not this two-source control. The next natural-site gate needs
an actual appropriately resolved elevation source plus an explicit basin and
outlet-boundary audit. No terrain was reshaped to force a pass here.

Reproduce the evidence with
`projects/fluid/fluid_25d/run_sustained_headwaters_demo.sh`; its products go
under `outputs/fluid` by default.

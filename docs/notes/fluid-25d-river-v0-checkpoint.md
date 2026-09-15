# Fluid 2.5D River V0 checkpoint

Date: 2026-09-14

This note records the River V0 numerical, GPU, artifact, and timing evidence
against the pre-evidence implementation baseline `a63615a5` (`feat(fluid):
present 2.5D river catchment`). That baseline predates the project-local timing
instrumentation; the instrumentation and this note land together in the
checkpoint commit identifiable from the repository Git history. The Release
binary and timing hashes below were produced with that instrumentation present.
The test counts below are a pinned snapshot of this checkout, not an evergreen
project promise.

## CPU contract

The project-local CPU virtual-pipes oracle lives under
`projects/fluid/sim/fluid_25d`. River V0 uses metres, metres per second, cubic
metres, a fixed frame delta divided into fixed substeps, closed outer faces, and
one explicit sink. Each cell owns four persistent directed outgoing discharges;
outflow is positivity-limited against available post-source/sink volume. The
CPU fixtures are deterministic:

- `dry-bed`: uneven terrain with no water or source/sink rates;
- `lake-at-rest`: uneven terrain with a constant free surface;
- `river-catchment`: an authored downhill channel, one upland source, and one
  initially dry downstream sink.

`fluid_25d_tests` covers Config V2 parsing/validation, scenario determinism,
dry-bed stability, lake-at-rest, dynamic closed-domain conservation, retained
flux inertia, finite/nonnegative depth and flux, and source/sink ledger
reconciliation. The generated test tree registers eight Fluid 2.5D tests. The
canonical Dev preset selection (`ctest --preset dev -N -R '^fluid_25d'`) selects
the seven non-windowed tests, and that 7/7 run passed. The eighth
`fluid_25d_reaches_glfw` test is a separately registered windowed lane; an
unfiltered focused run can include it and report it skipped when the windowed
gate is disabled. The standalone CPU test itself passed.

## GPU oracle

The GPU solver is a project-local virtual-pipes implementation with ping-pong
depth, persistent face flux, velocity/wet state, and a source/sink ledger. The
opt-in `--fluid25d-gpu-oracle-validation` lane reads back depth, flux, velocity,
and ledger only at the explicit capture/evidence point and compares them with
the CPU oracle. The focused lake-at-rest and river-catchment GPU-oracle CTest
lanes passed in the Dev snapshot. Normal headless and windowed runs keep the
simulation state GPU-resident and do not perform this validation readback.

## Headless artifact

The deterministic Release evidence run was:

```sh
build/release/projects/fluid/fluid_25d/fluid_25d --headless --capture png \
  --frames 300 --profile-warmup-frames 60 \
  --profile-output outputs/evidence/fluid-25d-river-v0-20260914/fluid-25d-river-v0-profile \
  --width 1280 --height 720 --grid-width 256 --grid-height 128 \
  --fluid25d-scenario river-catchment --no-validation \
  --output outputs/evidence/fluid-25d-river-v0-20260914/fluid-25d-river-v0.png
```

The run emitted:

```text
fluid_25d: GPU oracle validation disabled; simulation state remains GPU-resident
headless_png: NVIDIA GeForce RTX 5070 Ti wrote .../fluid-25d-river-v0.png at 1280x720
profile: wrote .../fluid-25d-river-v0-profile.{frames.csv,passes.csv,metrics.csv,trace.json,summary.txt}
```

Artifact paths and SHA-256 values from that run:

| Artifact | SHA-256 |
| --- | --- |
| `outputs/evidence/fluid-25d-river-v0-20260914/fluid-25d-river-v0.png` | `1207af84bd1a9ee3ef7d0d0c572cc91d002e4ed8b38728091bcaa577b2d4a6ad` |
| `build/release/projects/fluid/fluid_25d/fluid_25d` | `e4d815e44017513fb878710cdce6db473b10af1df22ee2a662bdc5647d1e5d5e` |
| `outputs/evidence/fluid-25d-river-v0-20260914/fluid-25d-river-v0-profile.passes.csv` | `27ce86ac3ac780fe58f7776ddbad48b133b478a85c7e9554abb30bd82e16598d` |
| `outputs/evidence/fluid-25d-river-v0-20260914/fluid-25d-river-v0-profile.summary.txt` | `2f9217efc8637c4c80285efdcac3b30aa010f1236c64d258b6969c276be864c6` |

The local Vulkan query (`vulkaninfo --summary`) reported GPU `NVIDIA GeForce
RTX 5070 Ti`, Vulkan device API `1.4.341`, driver name `NVIDIA`, and
`driverInfo=610.57.04`. These are host-local artifact facts, not portable
hardware requirements.

## GPU solver timing

The project-local timing lane follows the existing `GpuTimestampProfiler`
resource and frame-slot pattern. It creates/destroys one profiler query pool per
frame slot, collects the completed slot before recording the next windowed
frame, and uses an immediate command buffer plus the completed slot for
headless simulation. Unsupported timestamp queries leave the profiler disabled
and the solver continues with its normal GPU-resident state.

Only one timestamp scope is recorded: `fluid_25d solver`. It encloses an
optional reset, all fixed solver substeps, and the aggregate solve visibility
barrier. There are no per-substep spans, no simulation readback for timing, and
the rows below are solver durations rather than full-frame time or FPS.

The Release profile summary contains 240 rows (300 simulation frames minus 60
warmup frames):

```text
label              count  avg_ms   min_ms  median_ms  p95_ms   max_ms   total_ms
fluid_25d solver   240    0.010600 0.009088 0.010656  0.011136 0.011872 2.543904
```

Because this was a PNG capture lane, the host emitted one capture frame and the
simulation driver advanced the 300 fixed frames before it. Consequently the
profile summary reports `frames: 0` after the 60-frame warmup; the solver rows
are still recorded against simulation frame indices 60 through 299. They must
not be interpreted as interactive frame-time or FPS evidence.

## Windowed evidence

The canonical Dev preset run did not select the separately registered
`fluid_25d_reaches_glfw` lane. An unfiltered focused run reports that lane
skipped because the windowed test gate was not enabled
(`CUBEY_ALLOW_WINDOWED_TESTS` was unset). After that run, the explicitly enabled
windowed command

```sh
CUBEY_ALLOW_WINDOWED_TESTS=1 ctest --preset dev-windowed -R '^fluid_25d_reaches_glfw$' --output-on-failure
```

also skipped because the application reported `fluid_25d: glfwInit failed`.
That is a host/display limitation, not a passing smoke result. Windowed
timestamp collection is implemented, but controls, swapchain visual output,
and interactive performance remain unexercised in this checkpoint.

## Product boundary and reopen triggers

This remains River V0: no coupling to Terrain/Ocean/PBR, no erosion, sediment,
rain, spray, breaking waves, open/periodic boundaries, or generic solver
framework. The current river fixture uses a narrow physically seeded channel;
that is an accepted presentation limit for this checkpoint, not a reason to
broaden the model.

Reopen this slice when one of these concrete triggers appears:

- solver budget is exceeded by a larger or more varied target catchment;
- CPU/GPU mass or oracle comparisons diverge;
- source/sink or closed-boundary product requirements change;
- the river is unreadable in the target presentation; or
- a second independent consumer justifies shared extraction.

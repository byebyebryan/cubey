# Water 3D Profiling Notes

Date: 2026-05-23

Profiling now writes four files per run:

- `<prefix>.frames.csv`
- `<prefix>.passes.csv`
- `<prefix>.trace.json`
- `<prefix>.summary.txt`

If `--profile-output` has no directory component, outputs are written under
`outputs/profiles/`.

Current capture commands:

```sh
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 120 --grid-width 64 --grid-height 64 --grid-depth 64 --output outputs/water3d-profile-headless.png --profile-output water3d-64-headless --profile-warmup-frames 30 --no-validation
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 90 --grid-width 96 --grid-height 96 --grid-depth 96 --output outputs/water3d-profile-headless-96.png --profile-output water3d-96-headless --profile-warmup-frames 30 --no-validation
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 60 --grid-width 128 --grid-height 128 --grid-depth 128 --output outputs/water3d-profile-headless-128.png --profile-output water3d-128-headless --profile-warmup-frames 20 --no-validation
```

These runs measure the simulation GPU passes in the headless path. They do not
include the windowed screen-space water renderer beyond the final PNG capture.
`frames.csv` records memory rows with `delta_ms = 0` for headless simulation
frames so the summary does not imply a wall-clock FPS.

## Current Results

| Grid | Recorded frames | Device-local usage | Total avg GPU pass time | Top pass |
| --- | ---: | ---: | ---: | --- |
| 64^3 | 90 | 108.6 MiB | 8.607 ms | particle to grid, 5.028 ms |
| 96^3 | 60 | 333.8 MiB | 32.373 ms | particle to grid, 20.611 ms |
| 128^3 | 40 | 772.1 MiB | 68.676 ms | particle to grid, 43.001 ms |

Top average passes:

| Grid | 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- |
| 64^3 | particle to grid 5.028 ms | pressure 1.189 ms | extrapolate velocity 0.709 ms | refresh bins pre-p2g 0.618 ms | refresh bins post-advect 0.326 ms |
| 96^3 | particle to grid 20.611 ms | pressure 3.721 ms | extrapolate velocity 2.358 ms | refresh bins pre-p2g 1.969 ms | refresh bins post-advect 1.123 ms |
| 128^3 | particle to grid 43.001 ms | pressure 8.155 ms | extrapolate velocity 5.439 ms | refresh bins pre-p2g 4.356 ms | refresh bins post-advect 2.739 ms |

## Readout

P2G is still the dominant cost and scales worse than the other visible passes.
Pressure and velocity extrapolation are the next meaningful costs, but they are
not the first bottleneck. Bin refresh is also large enough to matter because it
runs twice per substep and feeds the active-face dispatch path.

Near-term optimization candidates:

1. Add finer P2G instrumentation before changing the shader again: bin build,
   active-face indirection, particle neighborhood iteration, atomics, and active
   particle scan count should be separable in the profile.
2. Keep P2G correctness fixed while experimenting. The previous fixed-stencil
   transfer attempt produced solver instability and should stay as a documented
   failed experiment until a narrower hypothesis is tested.
3. Profile the screen-space renderer separately in a stable windowed or offscreen
   path. The current headless data is useful for solver cost, not final frame
   composition cost.
4. Consider pressure/extrapolation only after P2G is better understood, unless a
   visual quality change lets us lower iteration counts without hiding errors.

## Review Notes

Review pass, 2026-05-23:

- The first profiling slice is intentionally built into Cubey instead of tied to
  Nsight or another external profiler. The artifacts are lightweight enough to
  keep around for regression comparisons, and Chrome trace JSON gives us a
  common viewer without adding a runtime dependency.
- Current Water 3D numbers are solver-oriented. The headless path captures
  compute pass timings and memory budget rows, but it does not yet measure the
  full interactive screen-space renderer as a steady-state frame workload.
- Headless simulation rows use `delta_ms = 0`; use pass summaries for GPU cost
  and not `frames.csv` for FPS in this mode.
- The next diagnostics pass should split P2G more finely before trying another
  optimization. In particular, separate active-face setup, bin traversal,
  particle neighborhood iteration, and atomic accumulation if possible.

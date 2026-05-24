# Water 3D Profiling Notes

Date: 2026-05-23

Profiling now writes five files per run:

- `<prefix>.frames.csv`
- `<prefix>.passes.csv`
- `<prefix>.metrics.csv`
- `<prefix>.trace.json`
- `<prefix>.summary.txt`

If `--profile-output` has no directory component, outputs are written under
`outputs/profiles/`.

Current capture commands:

```sh
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 120 --grid-width 64 --grid-height 64 --grid-depth 64 --output outputs/water3d-profile-headless.png --profile-output water3d-64-headless --profile-warmup-frames 30 --no-validation
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 90 --grid-width 96 --grid-height 96 --grid-depth 96 --output outputs/water3d-profile-headless-96.png --profile-output water3d-96-headless --profile-warmup-frames 30 --no-validation
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 60 --grid-width 128 --grid-height 128 --grid-depth 128 --output outputs/water3d-profile-headless-128.png --profile-output water3d-128-headless --profile-warmup-frames 20 --no-validation
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 120 --grid-width 64 --grid-height 64 --grid-depth 64 --output outputs/water3d-profile-diagnostics.png --profile-output water3d-64-diagnostics --profile-warmup-frames 30 --profile-diagnostics --profile-diagnostic-interval 4 --no-validation
```

These runs measure the simulation GPU passes in the headless path. They do not
include the windowed screen-space water renderer beyond the final PNG capture.
`frames.csv` records memory rows with `delta_ms = 0` for headless simulation
frames so the summary does not imply a wall-clock FPS.

`--profile-diagnostics` enables extra water 3D compute passes that write a fixed
uint diagnostics buffer. Those passes are gated by
`--profile-diagnostic-interval N`, and the headless simulation path reads the
buffer back after sampled frames and writes rows to `<prefix>.metrics.csv`; the
same values also appear as Chrome trace counter events and aggregate rollups in
the summary. Diagnostics requires `--profile-output` and `--headless`. Windowed
runs reject `--profile-diagnostics` for now because the metric path depends on
synchronous readbacks that are only wired into the deterministic headless loop.

Metric categories:

- `water_3d.workload`: active/inactive scan particles, out-of-bounds particles,
  liquid/rain particle split, nonempty and overpacked cells,
  transfer-truncated particles, max cell occupancy, active face count/ratio, and
  active-face indirect dispatch groups.
- `water_3d.solver`: post-projection divergence residual sum, max, average, and
  contributing cell count. Divergence is fixed-point encoded in the shader with a
  scale of 1,000,000 before CPU decode.
- `water_3d.whitewater`: per-frame emitted particles, compacted active particles,
  capacity, and active ratio.
- `water_3d.p2g`: active/blocked/processed face counts, processed U/V/W face
  split, neighborhood cell visits, scanned particle slots, positive/zero-weight
  particle candidates, APIC sample count, max cell occupancy seen by P2G, and
  derived ratios such as average slots per processed face.
- `water_3d.p2g.histogram`: per-face buckets for candidate particle slots,
  positive-weight candidates, and overpacked neighbor-cell counts. These rows
  show whether P2G time is broad uniform work or a smaller set of heavy faces.

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

## P2G Histogram Results

Captured 2026-05-23 with `water3d-p2g-hist-*` profile prefixes.

| Grid | P2G avg | Processed faces | Avg slots/face | High-candidate face ratio | Max slots/face | Overpacked-neighbor face ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 64^3 | 4.933 ms | 149.8k | 85.4 | 19.6% | 336 avg / 449 max | 0.78% |
| 96^3 | 20.391 ms | 393.6k | 115.4 | 35.7% | 440 avg / 514 max | 2.66% |
| 128^3 | 42.770 ms | 898.5k | 131.4 | 30.4% | 512 avg / 616 max | 1.87% |

At 128^3, P2G time correlates strongly with heavy-tail work: sampled-frame
correlation is 0.957 with average slots per face, 0.979 with high-candidate face
ratio, 0.952 with overpacked-neighbor face ratio, and 0.919 with max candidate
slots per face. The 64^3 and 96^3 captures also correlate strongly with
overpacked-neighbor ratio and max candidate slots, even when total scanned slots
fall later in the run. This points at uneven per-face work and clumped bins, not
only raw active-face count.

The broad candidate scan is still wasteful. Across all grids, only about 30% of
candidate particle slots have positive kernel weight. On the 128^3 capture, the
average face distribution is concentrated in the 97-128 candidate-slot bucket,
with another heavy tail in 129-384 and occasional 385+ faces. The next
optimization should target the per-face gather shape and cell/bin locality before
changing pressure or extrapolation.

## P2G Support-Aware Gather Results

Captured 2026-05-23 with `water3d-p2g-support-*` profile prefixes after changing
U/V/W face gathers from a generic `3x3x3` cell scan to face-support-aware
`2x3x3` scans along each face normal.

| Grid | P2G avg before | P2G avg after | Slots scanned | Zero candidates | Positive candidates | Solver residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 64^3 | 4.933 ms | 4.369 ms (-11.4%) | -31.4% | -45.4% | +0.04% | unchanged |
| 96^3 | 20.391 ms | 17.733 ms (-13.0%) | -31.9% | -45.9% | +0.01% | unchanged |
| 128^3 | 42.770 ms | 36.670 ms (-14.3%) | -32.4% | -46.4% | +0.02% | unchanged |

The optimization removed guaranteed-zero support cells without changing the
useful transfer set. Positive-weight candidate counts and processed face counts
stayed effectively unchanged, while the diagnostic P2G scan dropped roughly 25%
and the real P2G pass dropped 11-14%. Solver residual averages/maxima stayed
within noise across the measured captures.

This confirms that support-aware gather was the correct first optimization, but
P2G is still dominant. The remaining heavy-tail buckets point next at
cell/bin-locality and clumped-neighborhood work: particle sorting or compacted
cell ranges should be evaluated before a larger scatter/cooperative-transfer
rewrite.

## P2G Cell-Sorted Locality Results

Captured 2026-05-23 with `water3d-locality-*` profile prefixes after adding a
GPU-only cell-range particle sort. The first measured version copied canonical
particle buffers to temporary source buffers and scattered particles back into
canonical storage sorted by cell. The current implementation keeps canonical
particle IDs stable and only scatters compact `sorted_particle_indices` ranges
per cell; P2G walks `cell_offsets + cell_counts` ranges and dereferences those
IDs instead of random fixed-bin particle slots.

| Grid | P2G support baseline | P2G sorted ranges | Approx. sort overhead/substep | Summed profiled GPU avg |
| --- | ---: | ---: | ---: | ---: |
| 64^3 | 4.369 ms | 2.219 ms (-49.2%) | 1.14 ms | 15.066 ms -> 11.482 ms |
| 96^3 | 17.733 ms | 8.096 ms (-54.3%) | 3.84 ms | 52.303 ms -> 37.232 ms |
| 128^3 | 36.670 ms | 21.731 ms (-40.7%) | 9.05 ms | 106.521 ms -> 85.079 ms |

The sorted path is a clear net win despite the scan/scatter cost. The old fixed
cell-particle index buffer was removed after this capture because sorted cell
ranges are now the only P2G path. The later stable-ID revision deliberately gave
up canonical payload shuffling to avoid corrupting particle identity across
rendering, whitewater, APIC state, and diagnostics.

Stable-ID transfer update, 2026-05-24: particle positions, velocities, and APIC
affine rows are no longer shuffled during bin refresh. The sort lifecycle now
clears and writes `sorted_particle_indices`, while diagnostics separately report
raw cell occupancy and particles truncated by the configured transfer sample
limit. This keeps the optimization aligned with the renderer and makes overpack
pressure visible instead of silently hiding it behind a fixed bin cap.

## P2G Active Tile Experiment

Captured 2026-05-23 with `water3d-p2g-active-*` and `water3d-p2g-tiled-*`
profile prefixes. The tiled path is selected with
`--water3d-p2g-mode tiled`; the default remains `active`.

The experiment builds compact `4x4x4` active P2G tiles from the existing active
face list, then dispatches one 192-invocation workgroup per tile. Each workgroup
tests 64 U, 64 V, and 64 W face slots and skips inactive slots through
`active_face_flags`. There is no shared-memory particle payload cache in this
version, so the path is primarily a scheduling/locality experiment.

| Grid | Active P2G | Tiled P2G | Tile build | Non-diagnostic GPU avg | Total GPU avg with diagnostics | Tile slot / active face |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 64^3 | 2.222 ms | 1.157 ms | 0.037 ms | 5.801 -> 4.768 ms | 7.335 -> 6.288 ms | 1.61x |
| 96^3 | 8.118 ms | 6.677 ms | 0.109 ms | 19.697 -> 18.439 ms | 24.598 -> 23.423 ms | 1.36x |
| 128^3 | 21.779 ms | 23.954 ms | 0.211 ms | 48.540 -> 51.032 ms | 58.926 -> 61.505 ms | 1.26x |

The result is mixed. Tiling helps at 64^3 and 96^3, but the 96^3 total win is
small and 128^3 regresses. The lower tile-slot inflation at larger grids suggests
the regression is not simply extra inactive face work; the 192-invocation tile
shader is likely hitting occupancy, scheduling, or cache behavior that the
active-face path avoids at high occupancy.

Decision: keep `active` as the default and retain `tiled` as an opt-in profiling
path. The next tiled attempt should not just reshape dispatch. It needs a real
cooperative strategy, such as shared-memory cell metadata, particle payload
staging, or particle-owned scatter with a correctness plan for write conflicts.

## Readout

P2G remains the largest solver cost, but the first locality pass changed the
shape of the problem. The next high-gain work is either a deeper P2G rewrite
that avoids one-thread-per-face repeated neighbor scans, or a separate renderer
profile slice if visual composition becomes the limiting cost.

Near-term optimization candidates:

1. Investigate a deeper cooperative P2G path or particle-splat/grid-scatter
   variant now that particle memory is cell-local. Simple active-tile dispatch
   alone was not enough at 128^3.
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
- A built-in diagnostic readback path now captures workload, P2G scan, solver
  residual, and whitewater counters. Use that before another P2G rewrite so we
  can see whether the cost is coming from active-face count, cell occupancy,
  particle-slot traversal, weight sparsity, or residual pressure error rather
  than guessing from pass names alone.

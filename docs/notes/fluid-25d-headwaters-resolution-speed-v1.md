# Fluid 2.5D headwaters resolution and speed study V1

The opt-in `sustained-headwaters-demo` now supports a physically matched
resolution comparison: 65×33 at 4 m, 129×65 at 2 m, and 257×129 at 1 m.
The original 4 m construction and product defaults are unchanged. This is
still an authored, initially dry, shallow two-source control, not a
calibrated river or a natural-terrain fixture.

## Protocol and evidence

All grids cover 256×128 m between outer cell centers, use the same bed and
channel formula in metres, and retain two 12×12 m source footprints at
0.25 m³/s each. The eastern outflow-only aperture remains 12 m wide; its
binary face mask shifts the aperture center by at most 1 m on the refined
grids. Elsewhere the perimeter is closed. The finer grids have no seeded
water. Each run advances 1 simulated second per frame with 32 finite-volume
substeps. A separate 60 s dye pulse begins at 900 s while water supply
continues. Diagnostics are sampled every 10 s; first outlet discharge means
the first sampled cumulative value above 0.01 m³, not the exact onset.

The [resolution summary](../../outputs/fluid/sustained-headwaters-resolution-v1-20260925-final/summary.json)
and [1 m damping summary](../../outputs/fluid/sustained-headwaters-damping-1m-v1-20260925/summary.json)
record app and runner hashes, per-case profiles, and checks. See the matched
900 s Composite frames at [4 m](../../outputs/fluid/sustained-headwaters-resolution-v1-20260925-final/4m-damping-0.150/captures/hydraulic-f900.png),
[2 m](../../outputs/fluid/sustained-headwaters-resolution-v1-20260925-final/2m-damping-0.150/captures/hydraulic-f900.png),
and [1 m](../../outputs/fluid/sustained-headwaters-resolution-v1-20260925-final/1m-damping-0.150/captures/hydraulic-f900.png).
The source/outlet rings were corrected to keep a fixed physical render size
on the finer grids; this is presentation-only. The older resolution videos
under the non-`final` output directory predate that marker correction and
should not be used to compare marker size.

| Grid; damping | First outlet discharge | Dye 50% out | Late outlet rate | Water at 891 s | GPU solver average |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4 m; 0.15/s | 501 s | 1531 s | 0.479 m³/s | 298.3 m³ | 0.336 ms |
| 2 m; 0.15/s | 511 s | 1551 s | 0.467 m³/s | 309.7 m³ | 0.366 ms |
| 1 m; 0.15/s | 521 s | 1571 s | 0.459 m³/s | 316.6 m³ | 0.526 ms |
| 1 m; 0.075/s | 321 s | 1291 s | 0.500 m³/s | 179.5 m³ | 0.526 ms |
| 1 m; 0.05/s | 251 s | 1191 s | 0.500 m³/s | 132.2 m³ | 0.526 ms |

The 4 m-to-1 m cell count grows about 15.5× while the measured headless
solver span grows about 1.56× on the local RTX 5070 Ti. This is one GPU,
one scene, and a solver-only timestamp; it is **not** GUI frame rate or a
general scaling promise. All five long runs reported zero sampled/sticky
finite-volume status and water conservation residual below 0.5% of source
volume. At the last hydraulic sample, 891 s, the source ledger is the same
445.411 m³ in every case.
At 1791 s, 99.61–99.97% of the 30.001 m³ dye pulse has crossed the outlet.
The initial 4/2/1 m 900-frame Composite video runs took 12.53/12.55/12.65 s
of wall time including headless readback and encoding. Those videos predate
the render-only marker correction; their wall times show no large gross
presentation penalty in this capture path, but are not interactive FPS.

The finer grid makes the oblique bank silhouette less blocky, but flow
timing changes in the opposite direction from local trunk velocity: at
0.15/s, the 1 m trunk sample is 0.371 m/s versus 0.301 m/s at 4 m, yet
the wet front arrives later and more water remains stored. The solution has
**not** demonstrated grid convergence at 1 m. Refinement is a visual and
measurement resolution choice, not a way to speed the physical front.

At the same 1 m grid, reducing momentum damping is a strong flow-speed
control. Compare dye at 1200 s for [0.15/s](../../outputs/fluid/sustained-headwaters-damping-1m-v1-20260925/1m-damping-0.150/captures/dye-f1200.png),
[0.075/s](../../outputs/fluid/sustained-headwaters-damping-1m-v1-20260925/1m-damping-0.075/captures/dye-f1200.png),
and [0.05/s](../../outputs/fluid/sustained-headwaters-damping-1m-v1-20260925/1m-damping-0.050/captures/dye-f1200.png).
The matching [0.15/s](../../outputs/fluid/sustained-headwaters-damping-1m-v1-20260925/1m-damping-0.150/captures/dry-to-mature.mp4),
[0.075/s](../../outputs/fluid/sustained-headwaters-damping-1m-v1-20260925/1m-damping-0.075/captures/dry-to-mature.mp4),
and [0.05/s](../../outputs/fluid/sustained-headwaters-damping-1m-v1-20260925/1m-damping-0.050/captures/dry-to-mature.mp4)
videos show the dry-to-wet advance. At 0.075/s, the dye has nearly reached
the outlet at f1200; at 0.05/s it has begun leaving. The tradeoff is lower
stored water, not just faster movement: 179.5 m³ and 132.2 m³ respectively,
versus 316.6 m³ at 0.15/s. Neither tuned run is a deeper river.

## Provisional reading and validation boundary

For a faster, smoother **opt-in preview**, 1 m with 0.075/s is the balanced
candidate: the front and dyed water advance substantially sooner without
the stronger loss of stored water at 0.05/s. A 2 m grid is a lower-cost
visual alternative. Keep 4 m and 0.15/s as the control, and do not change
the project default or call the tuned scene calibrated. A side-by-side
windowed review and broader grid-convergence study remain open.

Focused CPU tests cover all three exact grid tuples, physical source/outlet
geometry, 4 m baseline hashes, short fine-grid CFL/positivity/ledger checks,
and the original 600 s 4 m flow regression. A 10-step GPU/CPU oracle passes
at all three resolutions at 0.075/s; a 30-step 1 m oracle passes at the
0.15/s control. The 30-step 1 m, 0.075/s oracle currently **fails** its
fixed 0.002 m/s maximum-velocity tolerance (measured maximum 0.003159
m/s), although maximum depth and momentum differences are about 2×10⁻⁶ m
and 1×10⁻⁶ m²/s, status is zero, and the source-ledger difference is
0.000149 m³. This is a validation limitation to investigate, not a pass
or evidence of a large visible water error. The long-run GPU evidence above
cannot substitute for that CPU/GPU parity gate.

Reproduce with `python3
projects/fluid/fluid_25d/run_sustained_headwaters_resolution_v1.py`.
Use `--mode resolution` for the three grids, or `--mode damping
--damping-grid-m 1 --video` for the speed sweep. Results go under
`outputs/fluid` by default. The runner's `summary.json` includes the exact
app hash because this study was produced from a dirty worktree; HEAD alone
does not identify the binary.

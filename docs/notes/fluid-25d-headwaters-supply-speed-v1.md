# Fluid 2.5D headwaters supply and speed study V1

This opt-in study asks whether the finer 1 m sustained-headwaters scene can
start dry, reach the outlet sooner, and carry more water without altering its
authored terrain. The baseline source is two continuous 0.25 m³/s patches
(0.5 m³/s total). `--fluid25d-headwaters-source-scale` multiplies their
rates but changes neither footprint, bed, aperture, other scenarios, nor
defaults. The original 4 m scenario arrays retain their exact hashes at 1×.

## Matched runs

Every case uses the 257×129 grid at 1 m, 1 simulated second per frame, and
32 finite-volume substeps. Hydraulic profiles last 900 s; a separate dye
run pulses both sources over `[900,960)` s and lasts 1800 s. Profiles sample
every 10 s. “First outlet” is the first sample with at least 0.01 m³ of
cumulative east-boundary outflow; it is an upper bound, not an exact arrival.
Depth, stored volume, and velocity below are from the final 891 s hydraulic
sample. “Dye half-out” is the first sample with half of the case's injected
dye amount across the outlet.

| Source scale; damping | Total supply | First outlet | Trunk depth | Stored water | Trunk speed | Dye half-out |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1×; 0.075/s | 0.5 m³/s | 321 s | 0.036 m | 180 m³ | 0.712 m/s | 1291 s |
| 2×; 0.075/s | 1.0 m³/s | 291 s | 0.067 m | 353 m³ | 0.741 m/s | 1281 s |
| 4×; 0.075/s | 2.0 m³/s | 261 s | 0.129 m | 710 m³ | 0.732 m/s | 1281 s |
| 8×; 0.075/s | 4.0 m³/s | 221 s | 0.245 m | 1444 m³ | 0.711 m/s | 1291 s |
| 8×; 0.05/s | 4.0 m³/s | 171 s | 0.180 m | 1034 m³ | 1.040 m/s | 1191 s |

The [1×/2×/4× summary](../../outputs/fluid/sustained-headwaters-supply-v1-20260925/summary.json),
[4×/8× summary](../../outputs/fluid/sustained-headwaters-supply-v1-20260925-upper/summary.json),
and [combined-control summary](../../outputs/fluid/sustained-headwaters-supply-v1-20260925-combined/summary.json)
contain per-case profiles, timings, and app/runner hashes. All three packs
used the same app SHA-256
`1b41c47e5d3662e9170fad3178ad5d210821993b13afbd249cc8b953ad9858c5`.
The runner hash changes only for the final pack because its supply-damping
option was added between captures.

For matched mature stills, compare [1×](../../outputs/fluid/sustained-headwaters-supply-v1-20260925/1m-damping-0.075-source-1x/captures/hydraulic-f900.png),
[4×](../../outputs/fluid/sustained-headwaters-supply-v1-20260925-upper/1m-damping-0.075-source-4x/captures/hydraulic-f900.png),
[8×](../../outputs/fluid/sustained-headwaters-supply-v1-20260925-upper/1m-damping-0.075-source-8x/captures/hydraulic-f900.png),
and [8× with lower damping](../../outputs/fluid/sustained-headwaters-supply-v1-20260925-combined/1m-damping-0.050-source-8x/captures/hydraulic-f900.png).
The [4×](../../outputs/fluid/sustained-headwaters-supply-v1-20260925-upper/1m-damping-0.075-source-4x/captures/dry-to-mature.mp4),
[8×](../../outputs/fluid/sustained-headwaters-supply-v1-20260925-upper/1m-damping-0.075-source-8x/captures/dry-to-mature.mp4),
and [combined](../../outputs/fluid/sustained-headwaters-supply-v1-20260925-combined/1m-damping-0.050-source-8x/captures/dry-to-mature.mp4)
videos show dry-to-wet development. Transport at f1200 is especially
revealing: compare [8× at 0.075/s](../../outputs/fluid/sustained-headwaters-supply-v1-20260925-upper/1m-damping-0.075-source-8x/captures/transport-f1200.png)
with [8× at 0.05/s](../../outputs/fluid/sustained-headwaters-supply-v1-20260925-combined/1m-damping-0.050-source-8x/captures/transport-f1200.png).

More supply deepens and broadens the wetted corridor and moves its *first*
water to the outlet sooner. It barely changes established trunk velocity or
dye transit: from 1× to 8× at 0.075/s, dye half-out remains around 1280–1290
s. Thus source rate is primarily a fill/front lever here, not an advective
speed lever. The one combined point—8× input plus 0.05/s damping—is both
faster and deeper than the 1×/0.075/s control, but lowering damping reduces
its trunk depth from 0.245 to 0.180 m relative to 8×/0.075/s.

## Health and limits

All long GPU runs report zero sampled and final sticky finite-volume status.
Water conservation residuals range from −0.011 to +0.477 m³, below 0.5% of
their respective supplied volumes. Late outlet rate approaches the configured
input: 0.500/1.000/1.992/3.934/3.995 m³/s in table order. These are
late-window estimates, not a proof of exact steady state. Headless solver
spans stay near 0.526 ms per 32-substep simulated frame on the local RTX
5070 Ti; encoded-video wall time includes readback and encoding and is not
interactive FPS.

The 30-step GPU/CPU oracle passes at 4×/0.075/s, 8×/0.075/s, and 8×/0.05/s.
The 1×/0.075/s case still fails only the strict global derived-velocity
comparison: 0.003159 m/s versus a 0.002 m/s tolerance at one cell with
0.000406 m depth. Its maximum depth and momentum differences are about
2×10⁻⁶ m and 1×10⁻⁶ m²/s, wet/dry bits agree, and status is zero. The oracle
now reports the failing cell/depth; its tolerance was **not** relaxed.

The authored banks rise about 0.65 m from the reach bed, while even the 8×
runs have only 0.310–0.316 m global maximum depth and 0.180–0.245 m trunk
depth. The mature captures do not show broad spill, but this scene has no
quantitative headwaters freeboard/overbank diagnostic, so containment and
near-bankfull status are **not** certified. This remains a stylized stream,
not a near-bankfull river. More source alone has diminishing value for dye
speed; a separately designed downstream stage/backwater control would be
the next depth mechanism to investigate, with its own outlet and transient
validation. No such boundary change is made here.

The 8×/0.05/s point is the strongest *opt-in review candidate* from this
bounded sweep for the requested faster-and-fuller direction. It is not
calibrated or promoted to a default; windowed visual review and a proper
freeboard/overbank measure remain open.

Reproduce the base sweep with:

```sh
python3 projects/fluid/fluid_25d/run_sustained_headwaters_resolution_v1.py \
  --mode supply --source-scales 1 2 4
```

Replace the scale list with `--source-scales 8 --video` for the higher-supply capture, or also
`--supply-damping 0.05` for the combined point. Each run writes a new
directory under `outputs/fluid` unless `--output-dir` is supplied. The
output summary records the exact binary hash because this study was made
in a dirty worktree; HEAD alone does not identify the run.

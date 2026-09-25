# Fluid 2.5D sustained-headwaters bankfull study V1

This is a bounded, opt-in check of whether more continuous supply can make the
authored 1 m headwaters river look fuller without spilling. It changes no
terrain, outlet, solver, render geometry, or default. Two source patches still
feed the same branches and trunk; only their common rate multiplier varies.

## Measurement and protocol

The new profile diagnostics sample three *interior* cross sections (one on
each angled branch and one on the trunk) and four separately labelled
source/outlet *endpoint* sections. Each samples the actual immutable terrain
across a reach-normal transect. Bank crests are sought around the outer edge
of the authored 12 m bank transition; bankfull fraction is wetted section area
divided by the area under the lower sampled crest. Freeboard is that lower
crest elevation minus an area-weighted representative water-surface elevation,
not a maximum stage. Local overbank water is counted only near that transect.
An additional whole-grid indicator counts wet cells and water volume beyond
the nearest authored reach's half-width plus its 12 m bank transition. It
includes endpoint regions, but the finite centerline corridors are approximate
around the confluence and endpoints. Zero in this indicator is **not** a proof
of terrain containment between sampled sections.

The matched sweep uses a 257×129 grid at 1 m, 0.05/s momentum damping, 1
simulated second per frame, 32 finite-volume substeps, and a dry start. Both
source patches together supply `0.5 × scale` m³/s. Hydraulic runs last 900 s;
separate dye runs pulse both inputs during `[900,960)` s and last 1800 s.
Profiles sample every 10 s. The table uses the final hydraulic profile at
891 s and the dye profile through 1791 s. The first outlet value is the first
10-second sample with at least 0.01 m³ cumulative east-boundary outflow, not
an exact arrival time.

| Source scale | Total supply | First outlet | Branch interior bankfull | Trunk interior bankfull | Near-outlet endpoint bankfull | Outside corridor at 891 s | Dye half-out |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8× | 4 m³/s | 171 s | 12% | 20% | 33% | 0 cells | 1191 s |
| 16× | 8 m³/s | 151 s | 28% | 46% | 61% | 0 cells | 1201 s |
| 20× | 10 m³/s | 141 s | 38% | 59% | 74% | 0 cells | 1211 s |
| 24× | 12 m³/s | 141 s | 48% | 71% | 88% | 0 cells | 1211 s |
| 28× | 14 m³/s | 131 s | 59% | 83% | 102% | 678 cells / 16.4 m³ | 1221 s |
| 32× | 16 m³/s | 131 s | 69% | 95% | 117% | 1846 cells / 104.7 m³ | 1221 s |

The [five-case summary](../../outputs/fluid/sustained-headwaters-bankfull-v1-20260925/summary.json)
and [28× follow-up](../../outputs/fluid/sustained-headwaters-bankfull-28x-20260925/summary.json)
record inputs, ledger quantities, dye times, timings, and the binary/runner
SHA-256 hashes. Both used app
`7e086345e1fb35fe92eb2a2039db7edeb28b394d759631c4ca96b3bc440de87f`
and runner
`be73a613bc0ff1d5c3dbb9c7179426f9ed1d3979d5cd471c6251b8f3455d1f65`.
Raw station and corridor values are in each case's
`profiles/hydraulic-f900.metrics.csv` and `profiles/dye-f1800.metrics.csv`.
These linked `outputs/` captures and profiles are local, ignored evidence;
they are not included in the source commit.

At 24×, the two branch interior sections have about 0.269 m representative
freeboard, the trunk has 0.148 m, and the near-outlet endpoint has 0.064 m;
all four have zero local overbank water. The whole-grid corridor indicator is
also zero at both 891 s and 1791 s. At 28×, the near-outlet endpoint already
exceeds bankfull and has local overbank water; the whole-grid indicator rises
to 1053 cells / 38.2 m³ by 1791 s. At 32× it rises to 2469 cells / 172.2 m³.
The [24× mature still](../../outputs/fluid/sustained-headwaters-bankfull-v1-20260925/1m-damping-0.050-source-24x/captures/hydraulic-f900.png)
and [28× mature still](../../outputs/fluid/sustained-headwaters-bankfull-28x-20260925/1m-damping-0.050-source-28x/captures/hydraulic-f900.png)
show the broader water and the onset of visible outlet-side spill. The
[24× dry-to-mature video](../../outputs/fluid/sustained-headwaters-bankfull-v1-20260925/1m-damping-0.050-source-24x/captures/dry-to-mature.mp4)
and [dye video](../../outputs/fluid/sustained-headwaters-bankfull-v1-20260925/1m-damping-0.050-source-24x/captures/dye-transport.mp4)
are captures from the same integrated binary; the dye is a passive transport
marker, not extra water. Static 3D views remain a weak way to judge these
sub-metre depth differences.

## Verdict and validation boundary

**24× is the fullest tested case without a positive authored-corridor spill
indicator at the 10-second profile samples through 1791 s, not a proven
threshold or a new default.** It is a useful opt-in visual-review candidate.
Uniform source scaling does not meet a near-bankfull *whole-river* goal: the
branches are still only about 48% bankfull when the outlet is already about
88%, and the next tested supply level spills near the outlet. A different
outlet/geometry control would need its own design and validation rather than
silently raising this source scale further.

All six long GPU cases completed with zero sticky finite-volume status and
water-ledger residual below 0.5% of supplied water; the separate dye runs
transported more than 99.6% of injected marker through the outlet by 1791 s.
The integrated 24× case passed the unchanged 30-step GPU/CPU oracle (maximum
depth difference 0.0000005 m, momentum 0.0000010 m²/s, derived velocity
0.0000977 m/s). A separate pre-diagnostic-binary 24× 220-step oracle attempt
did **not** pass the strict derived-velocity comparison: 0.008770 m/s at a
roughly 0.00054 m near-dry cell, despite depth/momentum differences around
4×10⁻⁶ and zero status. No oracle tolerance was relaxed. The short pass and
long-run ledger/finite checks do not establish long-horizon CPU/GPU parity;
that is an explicit limit before any default or general validation claim.

The new cross-section estimator was exercised on the authored scene at 1,
2, and 4 m, plus synthetic dry, wetted, opposite-branch, overbank, endpoint,
invalid-field, and profile-category fixtures. These are diagnostics only; the
fluid stepping and scenario construction are unchanged. Measured solver spans
near 0.526 ms per 32-substep simulated frame on the local RTX 5070 Ti do not
include readback, encoding, or interactive presentation cost.

Reproduce the matched sweep with:

```sh
python3 projects/fluid/fluid_25d/run_sustained_headwaters_resolution_v1.py \
  --mode supply --supply-damping 0.05 \
  --source-scales 8 16 20 24 32
```

Run `--source-scales 28` separately for the intermediate point. The runner
creates a new output directory by default; use `--output-dir` to pin one. The
new diagnostics appear in raw profile CSVs without changing runner parsing or
acceptance thresholds.

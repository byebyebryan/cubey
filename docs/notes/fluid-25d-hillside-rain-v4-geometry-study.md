# Hillside Rain V4: shared-geometry CPU study

This isolated study follows the sealed
[V3 transport study](fluid-25d-hillside-rain-v3-cpu-transport-study.md).
It tests whether explicitly shared terrain faces fix V3's thin-water transport
failure. It does not change application/GPU solvers, raw terrain inputs,
forcing, defaults, historical fixture thresholds, or V3 evidence. No commit,
push, new mountain-rain acceptance comparison or human GUI acceptance is included.

Evidence root: `outputs/fluid/hillside-rain-v4-geometry-20261002-wAniYn/`.

## Geometry and reference contract

The application terrain raster is queried at point locations, without a cell
area integration. Its one-float-per-cell array does not by itself establish a
finite-volume mean elevation. Study fixtures explicitly treat their supplied
terrain values as cell-center point samples. Water and depth-integrated
momentum, by contrast, are finite-volume means. The V3 curved reference mixes
center-sampled sine bed with integrated depth and constant mean discharge.

V4 averages four adjacent center samples to shared corners, extrapolates
linearly at exterior edges and wraps periodic X. A bilinear patch defines its
effective mean bed and shared edge midpoints. This is a declared change in
the numerical geometry, not unchanged effective terrain: the raw scenario
array remains untouched, but effective bed-mean deltas are reported. The
native sinusoidal case changes the effective mean by up to about 7.47 cm,
larger than its roughly 2 cm water depth. No smoother DEM is written back.

Original references and all failed flags remain. Additional compatible lakes
use `h=max(eta-effective_mean_b,0)`, explicitly a discrete mean-bed convention,
not the exact area integral of a shoreline cutting a bilinear cell. The
additional moving-water reference integrates the Bernoulli shallow root over
the effective piecewise-linear shared bed. It uses the same 1% depth and 5%
velocity/actual-transfer limits, including the native 30 m gate. New reference
passes cannot waive historical migration failures.

## Candidate and implementation boundary

The candidate follows [Chen and Noelle's BSGM reconstruction](https://www.igpm.rwth-aachen.de/Download/reports/pdf/IGPM517.pdf):
theta=1.3 limited free-surface/velocity reconstruction, paired bed/free-surface
correction when a reconstructed endpoint depth is negative, HLL interface
flux, matched within-cell slope source and dimension-by-dimension 2D extension.
Edge midpoint quadrature is explicit; it is not exact 2D shoreline integration.

Singular interface sources use [Chen and Noelle 2017, equations 2.15-2.18](https://www.igpm.rwth-aachen.de/Download/reports/noelle/2017.ChenNoelle_SINUM_M105307_newHR.pdf).
The right source uses the right starred depth. The BSGM preprint's equation
3.11 appears to print the left starred depth; the implementation follows the
side-correct earlier source and its lake-balance algebra.

The isolated double-state kernel retains V3's SSPRK2, symmetric half-step
source/sink and exponential drag, existing film-momentum cutoff, donor tracer
concentrations and unique stage-weighted applied face transfers. Publication
of state, diagnostics and dye clock remains transactional across all substeps
and stages. The .225 summed reconstructed-wave CFL is a checked conservative
study policy, not a general positivity or accuracy proof for this prototype.

`inspect_transport()` exposes actual face flux rates and the unforced
semidiscrete RHS before time integration. Its corrected geometry is stage-local;
it does not modify the stored shared geometry or raw scenario. A separate
direct initial-state calculation in `independent_initial.mjs` cross-checks
the historical curved reconstruction without using the kernel or timestepping.

## Bounded fallback decision

The first probe fails the historical and geometry-compatible curved native
gates, while the steep/thin straight incline passes. Shared input faces remove
the original HR zero-clipping, but free-surface reconstruction produces
negative endpoint depths. The positivity correction itself introduces new
face-bed mismatches and zero/doubled discharge on some faces. This already
happens before the first timestep; more substeps cannot remove that spatial
error.

No ad-hoc second kernel is added. The first-order
[Berthon/Michel-Dansac hydrodynamic reconstruction](https://www.math.sciences.univ-nantes.fr/~berthon/articles/hydrodynamic_reconstruction.pdf)
targets constant-discharge/energy moving equilibria, but at zero initial
discharge its interface reconstruction reduces to hydrostatic reconstruction.
Its moving-equilibrium guarantee alone does not establish this study's
accelerating-incline and wet/dry controls. In particular, the matched source
and dry reconstructed faces carrying original discharge need explicit handling,
not just a different flux formula. Its higher-order construction additionally
couples steady-state indicators, source blending and reconstruction; its dry
limits and finite-volume mean reference contract need their own faithful
implementation. This is a next
design question, not an attempted/rejected full implementation of that paper.

GeoClaw/PyClaw was not present in the inspected active local environment. Nothing was installed,
and GeoClaw was not treated as an assumed accuracy reference or acceptance
shortcut. No conclusion rejects the published method families generally.

The next useful design pass is a common terrain/finite-volume reference
contract plus a moving-equilibrium-aware reconstruction, with lake-rest and
rain/wet-dry behavior retained. Conserved discharge and energy/head, rather
than free surface alone, need explicit treatment in the fast curved reference.
Cell-mean water must not silently be substituted for point depth when deriving
that equilibrium. Extra substeps, removing depth protection, deepening the
film or raising rain are not replacements for this spatial contract. Further
refinement may help a diagnostic, but cannot waive the native hard gate.

## Final evidence

**Bounded negative verdict: this CPU prototype is not accepted for production.**
The authoritative `receipt-full/report.json` reports diagnostic execution and
conservation health true, candidate acceptance false; the benchmark correctly
returns 2. No threshold was weakened or historical failed flag removed.

All 36 straight-incline rows pass the old accuracy gates. The native steep/thin
case has relative maxima about `4.93e-7` depth, `8.95e-7` velocity and `2.51e-5`
actual cumulative face transfer. The 12 mm/h uniform-incline rain reference
also passes. This shows that the straight slope force is recoverable, not that
the curved or wet/dry problem has been solved.

Curved errors below are percent maxima over cells/faces and observations at
2, 4 and 10 seconds. The limits remain 1% depth and 5% velocity/transfer.

| Reference | Cell size | Depth error | Velocity error | Actual transfer error |
| --- | --- | --- | --- | --- |
| Historical sine | 30 m | 347.26% | 21.05% | 100.00% |
| Historical sine | 15 m | 69.49% | 2.86% | 52.58% |
| Historical sine | 7.5 m | 30.15% | 0.30% | 8.74% |
| Compatible shared bed | 30 m | 345.36% | 21.16% | 100.00% |
| Compatible shared bed | 15 m | 69.74% | 3.07% | 52.67% |
| Compatible shared bed | 7.5 m | 30.14% | 0.35% | 8.75% |

All three resolutions fail both reference families. Before the first timestep,
the native case has 16 corrected X cell-pairs across two identical rows, a
maximum reconstructed bed gap of `.0659147 m`, and discharge ranging from
zero to `.411404 m^2/s` instead of `.2`. HR zero-clipping is absent, but the
reconstructed flow is already wrong. The independent direct initial-state
calculation agrees with the kernel at all three resolutions. At 7.5 m, even
without positivity corrections or bed gaps, initial discharge still ranges
from `.143249` to `.256751 m^2/s`: shared geometry alone is not sufficient.

Eight of the 11 historical controls pass, including high-datum film, flat dry
rain, dam/dye migration, source/sink/outflow, film threshold, both rejection
paths and dye reset. All three historical lakes fail after the explicit
geometry reinterpretation. Maximum depth drifts are `.0267494`, `.0167735`
and `.000106077 m`; the curved partial lake rewets 24 initially dry cells.
The old `1e-5 m` and `1e-5 m/s` rest limits remain required. Both separately
initialized compatible discrete-mean-bed lakes pass with zero depth drift;
this does not waive those migration failures or prove an integrated shoreline.

Expanded-X comparisons pass 24 of 27 rows. All three native steep-depth cases
fail the unchanged `1e-8 m` / `1e-8 m/s` isolation budgets. The largest deltas
are `2.40e-6 m` and `3.31e-6 m/s` in the deep case. They are retained, though
they do not explain the percent-scale periodic curved failures.

Continuous 12 mm/h rain for 600 seconds into an initially dry, closed 9x9
quadratic bowl supplies `.162 m^3` of water and `.0648 m^3` tracer amount at
concentration .4. Its lowest 3x3 cells gather **96.06%** of both, versus 11.11%
area share. Maximum step-ledger residuals are below `3.2e-16 m^3` water and
`1.3e-16 m^3` tracer. This is a useful positive collection observable, not an
independent transient-accuracy reference, mountain comparison or GUI result.

The integrated gate passes 141 non-windowed native tests and 232 Python tests.
Focused ASan/UBSan implementation self-test and native incline/curved probe
pass their execution contracts, with the probe retaining negative numerical
acceptance. Primary owns numerical design, kernel and overall acceptance;
Luna supplied the audit, harness and independent kernel/results review.

The closure retains source/binary snapshots, raw streams, reference PDFs,
independent calculation, receipt hashes and pre-pass worktree snapshots. All
pre-existing sources remain byte-identical except the additive isolated CMake
target/test. V3's cold sealed artifacts are preserved; live CMake/build-command
provenance necessarily changes for the added V4 target and is labelled
separately. The historical legacy report is replayed byte-identically. See
`final-local-closure.json`, `closure-readback.json` and `validation-final/receipt.json`.

To reproduce into a new, unused evidence leaf:

```sh
rtk proxy cmake --build build/dev --target cubey_project_fluid_25d_geometry_study_benchmark
rtk proxy python3 projects/fluid/fluid_25d/run_geometry_study_v4.py --mode full --baseline outputs/fluid/hillside-rain-v3-cpu-20261002-CvcIYq/baseline-transport.json --output outputs/fluid/rain-v4-review-new
```

Expected full-study exit is 2 with valid diagnostics and failed numerical
acceptance. `--mode self-test` instead verifies implementation controls and
returns 0; it does not imply acceptance of the numerical matrix. Existing
evidence directories are refused rather than overwritten.

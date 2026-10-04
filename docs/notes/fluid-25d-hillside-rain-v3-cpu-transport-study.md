# Hillside Rain V3: bounded CPU transport correction study

This is an opt-in, CPU-only numerical study following the
[Rain V2 diagnosis](fluid-25d-hillside-rain-v2-transport-diagnosis.md).
It is not selected by the application and does not change the production CPU
oracle, GPU shaders, terrain, forcing, solver defaults, or existing tolerances.
Double study state is deliberately not a claim of float/GPU acceptance.

Evidence root: `outputs/fluid/hillside-rain-v3-cpu-20261002-CvcIYq/`.
The pre-pass dirty worktree, original sources and historical transport report
are preserved there. No commit, push, mountain rain comparison, or GUI review
is included in this pass.

## Acceptance contract

The independent straight-incline reference is the shallow-water acceleration
with exponential drag: `u(t)=g*S/gamma*(1-exp(-gamma*t))`. Uniform depth must
stay within 1% and per-cell velocity within 5%. Integrated water transfer
across selected actual solver faces must be within 5% of
`dx*h*integral(u dt)`; `h*u` is not a substitute for that observable.

The physical 1920 by 240 m matrix uses 30/15/7.5 m cells, slopes
0/.005/.05/.25, depths .02/.2/2 m, and observations at 2/4/6/8/10 s.
The wider-domain comparison is a discrete boundary control, separate from a
PDE signal-travel argument. Per-cell extrema matter, not only a mean.

The second reference is a periodic, frictionless Bernoulli flow over
`z(x)=2*sin(2*pi*x/480)` with constant discharge `.2 m^2/s`. Its shallow
supercritical root solves `h+q^2/(2*g*h^2)=E-z`, with `E` fixed by
`h(0)=.02 m`. Native 30 m accuracy is a hard gate, not waived if a finer grid
converges. Analytic depth cell means, rather than point depths, are compared
to finite-volume water. Float input rounding and the sampled-bed geometric
representation remain explicit limitations.

Safety controls cover still wet and partially wet lakes, positive shallow
films at high elevation, datum shifts, dry-start continuous rain, moving
wet/dry fronts, dye transport with the same water transfers, independent
water/tracer ledgers, reset/dye timing, and transactional timestep rejection.
A passing executable/self-test is distinct from a passing accuracy matrix.

## Candidate A

The study reconstructs limited depth, free-surface differences and velocities
at cell faces. Reconstructed bed is free surface minus depth, retaining the
original per-cell bed mean. Hydrostatic face pressure corrections and the
within-cell slope source use those same face states. Opposite-face velocity
weighting retains mean momentum. These ingredients follow the second-order
discussion in [Delestre et al., A limitation of hydrostatic reconstruction](https://arxiv.org/pdf/1206.4986),
not an extra unmatched gravity kick.

Transport uses SSPRK2, with symmetric half-step source/sink and exponential
drag. Each internal water transfer is owned once and applied with opposite
signs. Tracer takes the water-transfer donor's concentration; all stage
weights are retained in the reported face transfers. The public fixed step
publishes state, diagnostics and dye time only after every substep succeeds.
The conservative `.225` study CFL policy is checked at reconstructed stages;
it is not a general positivity theorem for this implementation.

Exactly dry cells use zero reconstruction slopes, preserving the existing
cellwise partially wet lake contract. This does not resolve subcell shoreline
volume. No new bed, terrain smoothing or channel painting is introduced.

## Bounded fallback and verdict

The initial probe reproduces nearly exact straight-incline transport, but
fails the native curved gate decisively. It triggers the one permitted
fallback, rather than promotion of the linear-only correction.

Candidate B is a first-order spatial implementation of the 2D free-surface
upwind equations 5.11-5.13 in [Berthon and Foucher, Efficient well-balanced
hydrostatic upwind schemes for shallow-water equations](https://www.math.sciences.univ-nantes.fr/~berthon/articles/upwind_hydro.pdf).
It evaluates a Rusanov flux in `W=(H,H*u,H*v)` and scales it by the
upwind water fraction `X=h/H`, with a matched cell-side source. There is no
hydrostatic max-bed clipping. It uses the same transactional time/forcing
framework as A. Its extra homogeneous-H draining restriction and `.125`
maximum-axis wave CFL are checked, alongside the conservative `.225` summed
wave policy. The gauge is explicitly `z-min(z)+.01 m`, computed difference
first without altering stored elevations or water. Numerical diffusion and
wave speeds depend on this relative free surface, not only water depth.

The fallback probe uses .01 s steps and stops after the three curved
resolutions plus lake/datum controls because hard gates fail. A bounded
first-order prototype failure does not reject every higher-order method in
that paper. No full B incline matrix or third prototype is run.

## Results and verdict

**Neither candidate is accepted for production integration.** This is the
bounded negative-study exit, not an abandoned claim that a fix has landed.

All 36 A incline rows execute with conservation health and pass the depth,
velocity and actual-face-transfer targets. The paired unchanged CPU oracle
also executes all 36 cases with conservation health at the explicitly matched
64-substep setting, but passes the velocity gate in only 12 of 36 rows. This
is separate from the retained historical 16-substep
benchmark, which replays byte-identically. A's straight steep/thin native case
has maximum relative errors `2.24e-8` in depth, `8.95e-7` in velocity, and
`2.51e-5` in integrated face transfer. Thus the missing linear-slope force is
correctable; extra simulation time alone is not the answer.

The curved flow remains inaccurate. Percent errors below are maxima across
all measured cells/faces and sample times through 10 s, not averages:

| Candidate | Cell size | Depth error | Velocity error | Actual transfer error |
| --- | --- | --- | --- | --- |
| A | 30 m | 397.69% | 25.47% | 102.97% |
| A | 15 m | 224.75% | 22.66% | 103.13% |
| A | 7.5 m | 69.99% | 7.73% | 59.29% |
| B | 30 m | 113.90% | 8.47% | 63.55% |
| B | 15 m | 19.79% | 3.86% | 9.43% |
| B | 7.5 m | 6.77% | 2.15% | 4.19% |

The thresholds stay 1% depth and 5% velocity/transfer. Finer grids improve
some results but do not waive the native gate; even B's finest depth fails.
Reconstructed bed mismatches and altered face depths remain relevant in A;
recovering a linear force does not establish curved moving-water accuracy.
Conservation and safe CFL do not contradict these accuracy failures.

All 11 A safety/reference controls pass, including the smooth partially wet
lake with 45 wet and 36 dry cells. Its measured depth drift is zero and maximum
speed about `1.75e-17 m/s`. B preserves the fully wet lake and equivalent-datum
film, but moves the smooth partially wet lake: maximum depth drift
`6.45e-5 m` and speed `.03193 m/s`, against unchanged `1e-5 m` and
`1e-5 m/s` limits. This is another substantive rejection, not visual progress.

The 12 mm/h wet-incline rain reference passes A, with maximum relative errors
`2.23e-8` depth, `8.97e-7` velocity and `2.51e-5` transfer. Flat dry-start
rain for 600 s supplies the expected 2 mm of water. These are analytic CPU
fixtures, not successful mountain rainfall or calibrated hydrology.

Expanded-X controls compare every sampled cell at fixed physical coordinates
for all 27 nonflat incline cases. They retain the same transverse extent;
the fixtures are row-invariant, not a general 2D boundary study. Twenty-six
pass. One native steep/deep case has maximum velocity delta
`1.0046229e-8 m/s`, narrowly above the unchanged `1e-8 m/s` isolation budget;
its depth delta is `5.50e-9 m`, within `1e-8 m`. The failed flag is retained,
not rounded away. This tiny boundary effect is not an explanation of the
percent-scale periodic curved failures, which have no X boundary.

The all-dyed dam release verifies actual downstream migration, equality of
water/tracer transfers, and per-cell depth change against transfer incidence
to about `1.46e-16 m`. Source, sink and outward-only boundary ledgers are
separate. Rejection after a prior substep and rejection in SSPRK stage two
both leave published state, tracer, face observations and clock unchanged.

The next design decision is a more substantial terrain-face/wet-dry and
moving-water discretization pass, using these gates. Neither adding rain,
changing damping nor porting either rejected candidate to GPU is justified
by this result. No river-site search, terrain modification, or stronger-rain
mountain run is implied.

## Reproduction and validation

The final integrated development build and all 140 non-windowed CTest tests
pass. The project Python suite passes 222 tests (214 existing plus 8 new
receipt tests). Focused ASan/UBSan runs pass the CPU self-test and complete
the bounded fallback probe without sanitizer findings. The latter exits 2
because its numerical accuracy gates fail, as expected. Existing offscreen
GPU tests are part of the integrated repository check; they do not validate
either new CPU candidate on GPU. CTest elapsed time is not a simulation
performance measurement.

From the repository root, use fresh output leaves:

```bash
rtk proxy cmake --build build/dev --target cubey_project_fluid_25d_transport_study_benchmark
rtk proxy build/dev/projects/fluid/fluid_25d/cubey_project_fluid_25d_transport_study_benchmark --self-test
rtk proxy python3 projects/fluid/fluid_25d/run_transport_study_v3.py \
  --mode full \
  --baseline outputs/fluid/hillside-rain-v3-cpu-20261002-CvcIYq/baseline-transport.json \
  --output outputs/fluid/rain-v3-full-new
rtk proxy python3 projects/fluid/fluid_25d/run_transport_study_v3.py \
  --mode fallback-probe \
  --baseline outputs/fluid/hillside-rain-v3-cpu-20261002-CvcIYq/baseline-transport.json \
  --output outputs/fluid/rain-v3-fallback-new
```

Both numerical study modes are expected to exit 2 with a valid report,
`diagnostic_success=true` and `candidate_acceptance_pass=false`; the safety
self-test exits 0. The authoritative final packets are
`receipt-full-A/` and `receipt-probe-B/` under the evidence root. Each binds
the source inputs and executable before and after execution, the unchanged
historical baseline, raw output and parsed report. `validation-final/receipt.json`
records the exact six integrated validation commands, environments, exits,
log hashes and timings. `reviewed-results.json` is the primary readback;
`final-local-closure.json` seals the reviewed source snapshots and artifacts,
with hash verification in `closure-readback.json`.

Earlier `probe-A/self-test.json` is an exploratory failure of the edge-only
rejection fixture; the corrected
3 by 3 fixture actually rejects in stage two. The earlier report is retained,
not silently overwritten. Passing self-tests certify references and controls,
not the separately failing full accuracy study.

This pass used `worker-goal-loop`: the primary owned numerical design and
acceptance; a `luna-max` worker owned the bounded proof harness, receipt tests
and an independent kernel review. Review found no additional implementation
defects in the final kernel, but does not override the measured failed gates.
All work remains local and uncommitted, with the pre-existing dirty Rain V1/V2
files preserved. No live GUI acceptance or product improvement is claimed.

# Hillside Rain V2: camera bounds and downhill transport diagnosis

This pass keeps the existing 512×512 native-30 m mountain crop, Rain V1 forcing,
solver defaults, and historical evidence unchanged. It takes the approved
negative transport-gate branch: **do not promote a stronger-rain comparison
until downhill transport is numerically credible**. No numerical correction or
48 mm/h mountain run is included.

Evidence root: `outputs/fluid/hillside-rain-v2-20261002-t6mh4w/`. The final local
closure identifies authoritative reports separately from retained exploratory
iterations. Changes are uncommitted; no push or live-GUI acceptance is implied.

The final integrated tree passed 139 non-windowed native tests and 214 Python
tests. Targeted formatting checks cover the new benchmark and changed camera
ranges; inherited formatting outside this pass is preserved. Short GPU probes
and the retained 64-step point-source compatibility replay pass separately.
Camera captures and receipts are bound to the final rebuilt app; their timings
are not performance benchmarks (correctness jobs may overlap).

## Camera correction

The old far plane depended on the starting camera distance. On this crop, the
travel and collection presets had far planes of about 17.1 km and 8.1 km, but
interactive orbit distance could reach 61.32 km. This was a projection clipping
defect, not evidence of terrain chunk/frustum-culling failure.

The new bound is the maximum legal orbit distance plus the terrain AABB's
maximum Euclidean radius relative to the current orbit target, plus 5% (at
least 32 m) headroom. The triangle inequality covers every yaw and pitch, not
just a sampled orientation. Near planes, home poses, zoom limits, terrain
heights, and water shaders remain unchanged. The existing D32 depth format and
thin-water bias remain in use; this is not a new depth-precision solution.

Pure geometry tests include the three actual rain presets, other scene scales,
corners and legal distances, and invalid/overflowing bounds. Matched short
offscreen replays check all three presets, portrait/widescreen aspect ratios,
and minimum/maximum legal orbit distances. These use CLI-configured poses,
not SDL scroll events; human interactive/animation acceptance remains pending.

## Independent downhill reference

The new CPU-only `cubey_project_fluid_25d_transport_benchmark` drives the
existing finite-volume oracle without changing its implementation. A uniform
water sheet initially at rest lies on `z = -S*x`; rain, sources, sinks and
outflow are disabled. Before physical boundary waves reach the interior,
depth is constant and the independent shallow-water reference is
`u(t) = g*S/gamma * (1-exp(-gamma*t))`, or `g*S*t` for zero damping.

The baseline matrix uses the same 1920×240 m physical domain at 30/15/7.5 m
spacing; slopes 0/.005/.05/.25; depths .02/.2/2 m; damping .15/s; and 2 s fixed
steps with 16 substeps. Interior measurements are taken at 2/4/6/8/10 s.
Additional controls remove damping, halve the substep, and double both domain
extents while preserving the same physical sample coordinates.

The analytical signal-travel calculation is a PDE isolation argument, not
proof of zero discrete boundary influence. The selected doubled-domain
control also has exactly zero measured mean depth/velocity difference at all
five sample times. It does not establish boundary independence for every row.

All 36 baseline rows preserve interior depth within 1%; only 12 meet the
5% velocity target (the nine flat controls and three deep/gentle cases).
The 39-row execution/conservation diagnostic is separate from accuracy. It
uses the existing CPU regression's water tolerance, `max(1e-5 m³,
volume_after*2e-6)`, not the app's looser GPU-vs-CPU parity budget. Largest
observed fixed-step water residual is .02060 m³; largest cumulative absolute
residual is .01674 m³. No existing test or production threshold was changed.
Representative measured mean velocities at 10 s are:

| Grid | Slope | Depth | CPU m/s | Independent reference m/s |
| --- | --- | --- | --- | --- |
| 30 m | .005 | 2 m | .242225 | .254036 |
| 30 m | .005 | .02 m | .016777 | .254036 |
| 30 m | .25 | .02 m | .016777 | 12.701822 |
| 7.5 m | .25 | .02 m | .067110 | 12.701822 |
| 30 m | .25 | .2 m | .167775 | 12.701822 |

The 2 cm sheet computes the same speed on the .005 and .25 slopes at 30 m.
Refining to 7.5 m increases speed fourfold but leaves the steep/thin case about
99.47% below the reference. Halving the substep changes the selected .2 m,
.05-slope, 15 m case's 10 s speed by only about .00158 m/s. The error is not
explained by a presentation clock, inadequate simulation steps or damping
alone.

Three separate GPU probes use the existing synthetic terrain-case sheet
release and strict CPU/GPU oracle validation. They are open-edge fixtures,
not the closed CPU matrix and not mountain rain. For a 30 m grid, .02 m sheet
and .25 slope at 10 s, the GPU's **whole-domain maximum** speed is .018203 m/s
versus the interior reference 12.701822 m/s. Even this upper bound is too low
by at least 99.86%. With zero damping, the maximum is .035588 m/s versus
24.525 m/s. Strict CPU/GPU parity passes separately: agreement between the two
implementations does not validate physical accuracy.

## Mechanism and decision

The existing first-order hydrostatic reconstruction clips a face's downstream
reconstructed water depth to zero when `S*dx >= h`. For a spatially uniform
descending sheet, pressure-correction differences then yield an apparent
gravity acceleration `g*h/(2*dx)` rather than `g*S`. This is an algebraic
diagnosis of the inspected production formula, corroborated by the measured
slope-insensitive speeds. It is not a measurement of internal mass flux.

This behavior is consistent with [Delestre et al., A limitation of the
hydrostatic reconstruction technique](https://arxiv.org/pdf/1206.4986).
The paper also cautions that higher order alone does not remove every
slope/depth/mesh limitation. CPU/GPU agreement, positivity and conservation
are necessary but do not settle this accuracy question.

Blindly lowering damping, adding rain or adding substeps cannot repair the
clipped slope force. A simple extra centered gravity source is not accepted:
it can break the existing lake-at-rest pressure/source balance and leaves the
mass-flux reconstruction unchanged. A slope-aware, well-balanced face/source
reconstruction is the one plausible correction direction reviewed here, but
it couples reconstruction, wet/dry positivity, source quadrature, time
integration, CPU/GPU parity and tracer transport. That exceeds the approved
small-correction branch; it needs a separate solver pass, not a quick
mountain-demo tuning patch. No solver candidate was implemented or promoted.

The benchmark does not expose actual production interior face mass flux.
Depth×velocity is not silently substituted for that quantity. This remains
an explicit observability requirement for a future correction's transport
acceptance gate. Existing solver tests and tolerances remain unchanged.

## What the mountain can collect

`hillside_rain_basins_v2.py` reuses priority-flood depression reading on an
analysis-only copy of the **current crop**. It verifies the manifest, elevation
and transformed-crop hashes. No filled bed is serialized as a simulation input,
and no new terrain site is surveyed.

There are 331 connected positive-fill patches. Median adjacent bed step is
7.165 m; 99.82% of adjacent steps exceed the 24 mm supplied directly by two
hours at 12 mm/h. Even at 48 mm/h, 99.29% exceed the 96 mm direct-rain depth.
These ratios diagnose the shallow-sheet regime, not actual local water depths
once convergence occurs.

The largest analytical depression lies around cell (476,114), away from both
the inherited collection camera and V1's peak-depth cell. Its fill-to-spill
proxy capacity is 24.17 million m³, versus 5.66 million m³ supplied to the
entire crop by two hours of 12 mm/h rain. It cannot fill to that analytical
spill level from this input alone. Partial pooling is still possible: its
floor-connected footprint at +1 m contains only one 30 m cell, and at +5 m
contains six. The second-ranked patch at (329,266) has four cells at +1 m and
33 at +5 m. Storage rank is not a water-delivery or lake ranking; connected
fill patches can merge nested basins, and crop-edge outlets omit upstream
terrain outside this simulation.

The report freezes analytical masks and provides tested explicit-field
observation helpers for volume, connected depth footprints, surface range,
net storage beyond direct rain, and moving/slow water **volume**. They are
offline analysis functions, not a newly integrated live observer. No new
actual-water basin classification, incoming-branch count or spill measurement
is claimed. Rain-specific basin cameras and longer 12/48 mm/h comparisons
remain deferred behind the failed transport gate.

## Reproduce

```sh
rtk proxy cmake --build build/dev --target cubey_project_fluid_25d_transport_benchmark
rtk proxy build/dev/projects/fluid/fluid_25d/cubey_project_fluid_25d_transport_benchmark
rtk proxy build/dev/projects/fluid/fluid_25d/cubey_project_fluid_25d_transport_benchmark --self-test
rtk proxy uv run --python 3.12 projects/fluid/fluid_25d/hillside_rain_basins_v2.py --output outputs/fluid/<fresh-basin-reading>
```

The full matrix exits nonzero for execution/health failures and records failed
rows. Faithfully measured analytic accuracy failure is separately reported;
it is not converted into an expected-failing CTest or hidden by relaxed
accuracy thresholds. `--self-test` validates the reference constants and the
flat control only; it does not certify the complete slope matrix.

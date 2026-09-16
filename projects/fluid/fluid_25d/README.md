# Fluid 2.5D

`fluid_25d` is Cubey's terrain-water project: a 2D shallow-water simulation
over a heightfield terrain. Water should flow downhill, pool in basins, follow
an authored channel, and drain through an explicit sink without requiring a
full 3D fluid volume.

The broader technique map lives in
[`docs/architecture/fluid-simulation.md`](../../../docs/architecture/fluid-simulation.md).

## Status

River V0 has a project-local GPU checkpoint. `fluid_25d` is a windowed and
headless Vulkan application with a GPU-resident virtual-pipes default and an
opt-in GPU finite-volume comparison solve, plus a
deterministic oblique terrain-and-water catchment presentation. Top-down
diagnostics remain explicitly selectable. It has deterministic dry-bed,
lake-at-rest, and river source/sink fixtures; headless oracle lanes compare
virtual-pipes depth/face-flux/velocity/ledger and finite-volume
depth/momentum/velocity/ledger against their corresponding CPU oracle.

Terrain-Water Audition V1 adds an opt-in, terrain-case-only discovery boundary:
an immutable `cubey.terrain.heightfield.v1` crop receives either a uniform rain
pulse or a uniform sheet release, and every outward perimeter face drains. The
same parameters must be used for every crop. These protocols are not a
hydrology generator, a water table, or product selection; they are a way to
observe solver response before humans choose a case.

The Config V2 product default is a bounded `256x128` catchment. Focused
CPU/GPU oracle fixtures intentionally set smaller dimensions so their evidence
stays quick and inspectable.

The project-local GPU timing lane records one aggregate `fluid_25d solver`
span per profiled simulation frame and reuses the common `--profile-output` and
`--profile-warmup-frames` options. The dated Release artifact and evidence
boundary are recorded in
[`docs/notes/fluid-25d-river-v0-checkpoint.md`](../../../docs/notes/fluid-25d-river-v0-checkpoint.md).

The CPU target remains a reference for the GPU implementation, not a shared
fluid framework and not a claim of scientific Saint-Venant fidelity.

Terrain-Water Audition V1 met the numerical-replacement trigger: untouched
30 m relief made the virtual-pipes positivity limiter look like grid-scale,
multi-cell transport rather than a credible wetting front. An isolated,
opt-in finite-volume Saint-Venant CPU/GPU comparison now exists to evaluate
that decision. It has no product claim: `virtual-pipes` remains the default,
and neither solver has selected or tuned a terrain case.

## Product boundary

The first numerical fixture is an authored upland catchment:

```text
source -> terrain-guided channel/basin -> explicit downstream sink
```

River V0 includes:

- deterministic terrain and scenario generation;
- explicit `terrain-case` ingestion from an immutable
  `cubey.terrain.heightfield.v1` raster crop;
- opt-in, uniform `rain-pulse` and `sheet-release` terrain-case forcing for
  matched discovery auditions;
- one or more depth-field sources and sinks;
- explicit closed or outflow-only perimeter faces, with boundary discharge
  recorded separately from explicit sinks;
- virtual-pipes shallow-water evolution;
- water depth, surface height, outgoing face flux, velocity, and wet/dry state;
- explicit source/sink/boundary-outflow volume accounting;
- a GPU-resident steady-state solve with no per-frame readback in normal use;
- an implicit oblique grid (no uploaded mesh) with opaque terrain, translucent
  premultiplied-alpha water, depth testing, restrained lighting, and flow cues;
- top-down terrain, depth, surface, flow magnitude/direction, and wet/dry
  diagnostics as an explicit alternate presentation mode;
- opt-in headless CPU/GPU fixtures that read back only at their final evidence
  point;
- opt-in headless profile diagnostics that read depth, velocity, and ledger
  only at the requested common diagnostic interval.

It deliberately excludes erosion, sediment, interactive editing, spray,
breaking waves, periodic or inflow-capable outer boundaries, water-table or
infiltration state, and high-order flood modeling claims. Uniform audition
rainfall is a bounded source-field protocol, not climate or a flooding product.
Outflow-only perimeter faces are a bounded numerical primitive, not a flooding
product. Flooding and an interactive terrain/water toy are later products, not
hidden requirements of River V0.

## Numerical contract

All quantities use physical units:

- terrain height, water depth, and cell size are metres;
- source and sink fields are depth rates in metres per second;
- face fluxes are discharge rates in cubic metres per second;
- the water ledger is cubic metres;
- `fixed_delta_seconds` is the simulation-frame duration and
  `simulation_substeps` divides that duration into fixed solver steps;
- `minimum_wet_depth_m` is a numerical wet/dry threshold: cells at or below it
  do not retain or originate outgoing flux.

For a cell with depth `h`, bed height `b`, and surface height `eta = b + h`,
the oracle advances a persistent directed discharge for each neighboring pipe:

```text
pipe_area  = cell_size * cell_size
f_next     = max(0, exp(-damping * delta_seconds) * f_previous
                    + delta_seconds * pipe_area * gravity
                    * (eta - eta_neighbor) / cell_size)
```

The four fluxes are owned by the source cell. `pipe_area` is deliberately a
project-local cell-area choice for this first oracle; it is not a public tuning
option. A higher neighboring surface decelerates retained directed flux before
the nonnegative clamp. Before updating depth, total outgoing discharge is
scaled so that the cell cannot export more than its available volume during the
current substep. The depth update then applies

```text
h_next = h + delta_seconds / cell_area * (incoming_flux - outgoing_flux)
```

with source and sink depth rates converted to volume by multiplying by cell
area. Outer faces are closed in River V0; the explicit sink is the only
intentional removal path. The positivity limiter and finite-value checks keep
the water field nonnegative and finite.

This is a compact virtual-pipes reference scheme. It is retained as the
product default while the separate well-balanced finite-volume comparison is
audited on the same immutable scenarios.

### Opt-in finite-volume comparison

The audition trigger above is exercised by a separate CPU/GPU comparison, not
by a silent change to River V0. It advances the depth-integrated Saint-Venant state
`U = (h, h*u, h*v)` with a first-order Cartesian local Lax-Friedrichs/Rusanov
flux. At every face it uses the hydrostatic reconstruction of Audusse et al.,
"A Fast and Stable Well-Balanced Scheme with Hydrostatic Reconstruction for
Shallow Water Flows," *SIAM J. Sci. Comput.* 25(6), 2004,
[doi:10.1137/S1064827503431090](https://doi.org/10.1137/S1064827503431090):

```text
h*_left  = max(0, h_left - max(0, z_right - z_left))
h*_right = max(0, h_right - max(0, z_left - z_right))
U*  = (h*, h* u, h* v)
F̂   = 1/2 (F(U*_left) + F(U*_right))
       - 1/2 a (U*_right - U*_left)
a   = max(|u_n,left| + sqrt(g h*_left), |u_n,right| + sqrt(g h*_right))
```

Each adjacent cell receives its own hydrostatic pressure correction
`g/2 * (h² - h*²)` in the face-normal momentum flux. This preserves a
constant free surface across uneven beds, including a wet/dry shoreline,
while internal mass flux remains exactly equal and opposite. Closed faces use
a reflected normal-velocity ghost; an opened P2 face uses a dry ghost at the
edge cell's bed elevation, cannot add external water, and contributes only its
outward mass flux to the existing boundary ledger.

Sources and sinks are applied before transport. A source adds no momentum; a
sink removes the same fraction of local depth-integrated momentum as water.
After conservative transport, the existing exponential `flow_damping` is an
explicit linear momentum drag and never changes water mass. Apart from a
`1e-7 m` final float residue, a non-finite or negative depth fails closed; the
comparison does not use the virtual-pipes export clamp.

Every finite-volume substep checks the conservative unsplit two-dimensional
CFL number before mutating persistent state:

```text
CFL_2D = delta_seconds / cell_size
         * (max(|u| + sqrt(g h)) + max(|v| + sqrt(g h))) <= 0.45
```

`0.50` is the hard ceiling for this first-order Cartesian form; the CPU oracle
rejects any step above the `0.45` target. The GPU path performs the identical
global-max criterion through a status prepass/finalize/candidate/commit
sequence. Candidate writes only inactive `h, hu, hv` plus isolated velocity
and ledger-delta buffers; after the global status barrier, commit accepts all
candidate cells (leaving candidate `h, hu, hv` in that inactive destination)
or copies the source `h, hu, hv` into the next parity unchanged, leaving common
velocity and cumulative ledger untouched.
Candidate validates each prospective cumulative ledger before commit, so
commit has no late partial-write error path. Error flags remain sticky until
explicit solver reset while only the two CFL-max scratch values are cleared per
substep; GPU-oracle validation and opt-in profile diagnostics reject any
nonzero flag. Ordinary headless finite-volume capture also reads sticky status
once at final capture; windowed comparison runs stay GPU-resident and require
an explicit diagnostic/readback mode to surface a device flag to the host.
This is comparison evidence, not product-ready evidence: the untouched-terrain
V2 auditions retain a ranked shortlist, but no case advances until a concrete
water story and a presentation-validity pass justify promotion.

## Deterministic fixtures

The project-local scenario generator provides three product-facing analytic
fixtures:

- `dry-bed`: uneven terrain with zero initial water and no rates;
- `lake-at-rest`: uneven terrain with a constant free-surface height and no
  source or sink;
- `river-catchment`: a downhill quadratic channel seeded with a thin water path,
  one source near the upstream end, and one initially dry downstream sink.

The CPU tests cover deterministic generation, configuration parsing and
validation, virtual-pipes regression behavior, and finite-volume dry uneven
beds, wet/dry lake-at-rest preservation, a symmetric flat-bed dam break,
open-boundary drainage, source/sink/boundary ledger reconciliation, and
fail-closed CFL rejection.

## Configuration

River V0 uses a project-owned Config V2 facade. It composes the shared 2D grid
schema and currently exposes:

- `--grid-width`, `--grid-height`, and `--grid-size`;
- `--fluid25d-view catchment|diagnostics` (default: `catchment`);
- `--fluid25d-solver virtual-pipes|finite-volume` (default: `virtual-pipes`;
  finite-volume is an opt-in numerical comparison);
- `--fluid25d-scenario dry-bed|lake-at-rest|river-catchment|terrain-case|boundary-drain-fixture`;
- `--terrain-heightfield <manifest-or-directory>` (required by `terrain-case`);
- `--fluid25d-terrain-crop-x` and `--fluid25d-terrain-crop-z` (native sample
  indices; the configured grid dimensions define the crop extent);
- `--fluid25d-terrain-water-protocol none|rain-pulse|sheet-release` (default:
  `none`; terrain-case only);
- `--fluid25d-rainfall-rate-mm-per-hour` and
  `--fluid25d-source-active-duration-seconds` (required together by
  `rain-pulse`);
- `--fluid25d-sheet-depth-m` (required by `sheet-release`);
- `--fluid25d-cell-size-m`;
- `--fluid25d-fixed-delta-seconds`;
- `--fluid25d-substeps`;
- `--fluid25d-gravity-m-per-s2`;
- `--fluid25d-flow-damping-per-second`;
- `--fluid25d-minimum-wet-depth-m`;
- `--debug-view terrain|depth|surface|flow|direction|wet-dry`;
- `--profile-output`, `--profile-warmup-frames`, `--profile-diagnostics`, and
  `--profile-diagnostic-interval` (profile diagnostics are headless-only and
  deliberately opt into readback); solver rows are not full-frame or FPS
  measurements;
- `--fluid25d-gpu-oracle-validation` (headless-only; performs final-state
  readback against the CPU oracle).

Normal headless capture keeps the simulation GPU-resident. Finite-volume
capture additionally reads its sticky solver-status word once at final capture
and fails if the solver rejected a substep:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 8 \
  --width 512 --height 256 --output fluid-25d.png
```

The normal headless command produces the same static oblique catchment framing
as the windowed default. Virtual-pipes does not read back simulation state;
finite-volume reads only its final sticky status. To capture a top-down
numerical diagnostic instead, select it deliberately:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 8 \
  --fluid25d-view diagnostics --debug-view flow --output fluid-25d-flow.png
```

The two focused evidence lanes are intentionally opt-in:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 12 \
  --grid-width 12 --grid-height 8 --fluid25d-scenario lake-at-rest \
  --fluid25d-gpu-oracle-validation --output fluid-25d-lake.png

build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 24 \
  --grid-width 24 --grid-height 9 --fluid25d-scenario river-catchment \
  --fluid25d-gpu-oracle-validation --output fluid-25d-river.png
```

An imported terrain case begins dry with no source or sink rates. Its cell size
is resolved to the source's native sample spacing unless
`--fluid25d-cell-size-m` is supplied explicitly; an explicit conflicting value
is rejected. The application reports a stable identity containing the source
elevation SHA-256, transformed crop SHA-256, crop coordinates and extent, and
native spacing. Terrain crops are
asset inputs only: they do not invoke Python/Torch or the Terrain application,
and they do not add water-table, candidate-specific boundary policy, erosion,
or sediment behavior. Terrain-case `none` remains dry and closed. The two
audition protocols construct complete uniform fields and open every outward
perimeter face; they never paint a channel, move an outlet, or reshape the
terrain.

The checked-in V1 runner preserves the frozen virtual-pipes negative baseline:

```sh
projects/fluid/fluid_25d/run_terrain_water_audition.sh
```

The separate finite-volume-only V2 runner keeps the same immutable five crops,
but uses a shared 1,200-second physical-horizon rain/sheet protocol:

```sh
projects/fluid/fluid_25d/run_terrain_water_audition_v2.sh
```

Both write ignored evidence under `outputs/fluid`. V2 supports an explicit
`CASE_FILTER` only for bounded pilots; it cannot change crop transforms,
forcing, or perimeter policy. Profile metrics include solver status, wet and
active-flow coverage, stored water, depth, speed, slow/pooled wet fraction,
cumulative source/sink/outflow ledger totals, and the visible conservation
residual. “Slow/pooled” means a wet cell at or below `0.02 m/s`; this is a
reporting threshold, not a solver control. The exact V1/V2 evidence and
shortlist/no-promotion decision live in
[`docs/notes/fluid-25d-terrain-water-audition-v2.md`](../../../docs/notes/fluid-25d-terrain-water-audition-v2.md).

Analytic fixtures remain closed by default. `boundary-drain-fixture` exists
solely to validate the numerical outflow contract: an opened perimeter face
sees dry exterior at its own bed elevation, permits no external inflow, and
records its post-limiter discharged volume separately from source and sink
ledgers. It is not a product scenario or terrain audition policy.

In a window, left-drag or scroll orbits/zooms the catchment, `Space` pauses,
`R` resets the deterministic scenario, `A` switches between catchment and
diagnostics, and `D` cycles diagnostic contents.

## Source layout

- `projects/fluid/fluid_25d`: project-facing CMake and design authority;
- `projects/fluid/fluid_25d/run_terrain_water_audition.sh`: matched evidence
  runner for the immutable virtual-pipes V1 baseline;
- `projects/fluid/fluid_25d/run_terrain_water_audition_v2.sh`: finite-volume
  physical-horizon audition runner with solver-qualified ignored outputs;
- `projects/fluid/sim/fluid_25d`: project-local config, scenarios, CPU oracle,
  and focused tests.

The solver is intentionally not coupled to `projects/terrain`, `projects/ocean`,
`TerrainOceanFieldView`, paused hydrology work, or the shared rendering
foundation. Those products may become future consumers only after a concrete
contract is demonstrated.

## Next implementation gate

The finite-volume terrain audit retains a ranked shortlist, not a product
fixture. The next product decision must supply a concrete water story and a
presentation-validity review before selecting mountain floodplain or rolling
catchment; it must not tune a candidate inside the neutral audition. Any shared
helper or foundation change still waits for a second independent consumer or a
measured bottleneck.

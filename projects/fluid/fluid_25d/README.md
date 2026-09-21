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
diagnostics remain explicitly selectable. The default Composite catchment
view preserves the original terrain-and-water shading; Water Isolation
quiets the bed to expose the wet edge, and Flow Inspection adds a fixed-grid
velocity-arrow field to the explicit flow-reading surface. It has deterministic
dry-bed, lake-at-rest, and river source/sink fixtures; headless oracle lanes compare
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
  premultiplied-alpha water, depth testing, restrained lighting, and a
  deterministic render-only advected flow cue;
- a Flow Inspection-only, fixed-grid GPU quiver field. Arrow angle shows
  local downstream velocity, while restrained length and brightness show
  speed; the arrows are samples, not waves, foam, water mass, or particles;
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

The windowed visual cue is a project-local ping-pong scalar field at the
fluid-grid resolution. Reset restores the same broad seeded field; an accepted
outer fixed step backtraces it with the final physical velocity after all
solver substeps, while a rejected finite-volume status leaves it unchanged. It
is not solver state and never enters depth, momentum, CFL, ledgers, or
oracle/diagnostic readback. Pausing performs no cue update; calm water uses
the stable base shade rather than a time-driven pattern.

Flow Inspection has a separate project-local quiver-state buffer. Every state
element owns one deterministic, fixed anchor in a regular 96 by 48 lattice on
the default 256 by 128 product grid and the 128 by 64 source-to-outlet demo;
the compact demo therefore has about 1.3 cells between anchors, while the
product grid has about 2.7. Small fixtures reduce that count rather than
producing duplicate anchors. A three-by-three wet/finite-aware neighborhood
average samples the published physical velocity around each anchor after an
accepted outer fixed step. Direction, strength, and opacity smooth in
simulation time, but anchor coordinates never advect, reseed, or retire.
`0.025 m/s` is the visibility threshold, intentionally above the retained
initial terrain-sheet maximum, so the dry bed, lake at rest, and honest initial
terrain state remain arrow-free. Arrow silhouette length follows local lattice
pitch (about 58--72% of the pitch), while blue-to-yellow color and length use
the fixed `0.025..0.80 m/s` physical range; this keeps the dense demo field
readable without making larger-grid arrows disappear. Current dry or
nonfinite data hides an arrow immediately; calm valid water fades it through
the same presentation-only state. Reset samples the current parity-selected
field, allowing an established paused flow to be read without a host readback.
A sticky finite-volume status is a strict no-write boundary for reset and
update. This state never enters solver descriptors, wet/dry policy,
diagnostics, profiler diagnostics, or CPU/GPU oracle readback.
Composite, Water Isolation, and top-down Diagnostics never dispatch or draw
the quiver. Flow Inspection suppresses Composite's broad scalar-cue highlight
so its fixed arrows are the only direction language. Windowed Flow Inspection
includes optional work in the existing presentation command span; explicitly
selected headless Flow Inspection updates after each solver record, outside
the solver-only timestamp/oracle lane. Its initial reset is recorded before
the first headless solver step, so the first capture remains the truthful
unanimated initial state; this changes only presentation state and does not
change solver dispatches or timestamps.

### Opt-in source-to-outlet scene

`source-outlet-demo` is a separate authored explanation scene, not a change to
the River V0 virtual-pipes fixture or its default camera. It is meaningful only
when launched explicitly with the opt-in finite-volume solver. It uses compact
bounded SOURCE and OUTLET regions (the colored rings mark their representative
centroids), a shallow connected initial ribbon, closed outer faces, and one
terminal outlet basin. The bed falls from the green SOURCE along the winding
channel to the amber OUTLET, then rises across a broad downstream containment
shoulder; there is no implied drain beyond the scene edge.

This is deliberately stylized authored relief, not an imported real-world DEM.
On the recommended `128x64` grid the representative centroids are at columns
`14` and `110`. Between them, two broad S-bends, changing valley width, and a
single constriction make the route read as terrain-guided rather than as a
straight wet line. A scene-only height/slope tint and render-only relief scale
make the dry banks legible from the home camera; numerical terrain heights,
water depths, and every other scenario's material and camera are unchanged.
The compact OUTLET-rate mask is the wet core of the visible terminal basin, so
its configured capacity is not assigned to dry shoulder cells.

The ribbon is intentionally present at reset so the full route is legible
before transport crosses it. It does not claim that the same water first
injected at SOURCE reaches OUTLET in the initial seconds. In the finite-volume
demo, configured source input and outlet capacity are each `0.03 m3/s`; actual
outlet removal remains limited by water available in its region and must be
measured from the ledger rather than assumed from the configured capacity. The
terminal basin keeps the sustained sink supplied without a bypass beyond its
marker. The first home camera frames the
two endpoints and route with modest terrain context, while later user orbit
and zoom remain unchanged. Ring words live in the control-panel legend rather
than in-scene text so they stay readable at every camera distance.

Use the compact one-metre `128x64` presentation grid and select the finite-
volume solver explicitly; this does not alter the global `river-catchment` or
virtual-pipes defaults:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d \
  --grid-width 128 --grid-height 64 \
  --fluid25d-scenario source-outlet-demo --fluid25d-solver finite-volume
```

Append `--fluid25d-catchment-view flow-inspection` to use the optional
fixed-grid velocity-field reading mode. For reproducible long-horizon evidence,
use the headless profile command:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --capture png \
  --frames 54001 --width 1280 --height 720 --grid-width 128 --grid-height 64 \
  --fluid25d-scenario source-outlet-demo --fluid25d-solver finite-volume \
  --profile-output outputs/fluid/source-outlet-demo-profile --profile-diagnostics \
  --profile-diagnostic-interval 600 --output outputs/fluid/source-outlet-demo.png
```

Use early checkpoints for reset/mature continuity and a late interval (for
example `890–900 s`) for the sustained-flow acceptance check: finite-volume
status must remain zero; source and *actual* sink-removal increments over the
recent interval should both be about `0.03 m3/s`; stored water must remain
bounded; and boundary outflow must stay zero. Capture Composite plus
Depth/Flow diagnostics when changing this local scene geometry.

### Opt-in immutable mountain source/outlet scene

`mountain-source-outlet-demo` is a separate finite-volume-only product
demonstration on one pinned, immutable `mountain-valley-1` crop. It does not
promote the neutral `terrain-case` audition protocol, alter River V0, or edit
the imported elevation. Its 256 by 128 native 30 m crop is pinned at
`(1536,1664)` by both the input elevation SHA-256
`2a919b516d8ae4fb8c193cdd8db1a8ba055ba702e5cbd1ff50ad2b7fc6ab3c48` and the
transformed crop SHA-256
`9bfebfe229886ded533556acaf11de541caddfc4cf8104d864da1232fa8b24c6`.

The green ring is the 81-cell source region centred at `(8,60)`, whose total
configured input is `0.75 m3/s`. The amber ring is a visible outlet basin
centred at `(232,122)`. Its actual explicit drain is deliberately narrower:
the three reviewed low cells `(232,127)`, `(229,126)`, and `(231,126)` remove a
total configured `0.75 m3/s`. All outer faces are closed. The route is a
reviewed, radius-four prewetted initial-water corridor; it makes the whole
source-to-outlet story readable at reset, but is not a claim that one water
parcel, a surveyed river, or a provenance record follows that exact path.

The home camera frames the full 6.97 km endpoint route. A mountain-only
render-space relief scale and elevation/slope palette expose the valley and
ridges; they do not alter elevation buffers, water state, source/sink masks,
the finite-volume solve, or any other scene. Composite is the terrain-and-water
overview, Water Isolation makes the wet corridor easier to see, and Flow
Inspection overlays the existing fixed 96 by 48 stable velocity samples. The
small arrows show local direction and speed, not water particles or a tracked
parcel.

Launch it directly with the pinned source and the recommended 2 s outer step
and eight solver substeps:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d \
  --fluid25d-scenario mountain-source-outlet-demo \
  --fluid25d-solver finite-volume \
  --terrain-heightfield cache/terrain/sources/v1/presets/mountain-valley-1 \
  --fluid25d-fixed-delta-seconds 2 --fluid25d-substeps 8
```

For a reproducible remote-review pack (reset, mature Composite, Water
Isolation, Flow Inspection, a 120-frame CPU/GPU oracle smoke, and 600-frame
profiles), run:

```sh
projects/fluid/fluid_25d/run_mountain_source_outlet_demo.sh \
  outputs/fluid/mountain-source-outlet-v1-20260921
```

The 600-frame profile represents 1200 s. Expect zero finite-volume status and
boundary ledger, late-window source and actual sink removal near `0.75 m3/s`,
and bounded stored water. The runner writes the exact capture hashes, pinned
input identity, metrics, and measured late-window rates into its output
directory; use those results rather than assuming the configured sink rate was
fully realised.

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
- `--fluid25d-view catchment|diagnostics` (top-level surface selector; default:
  `catchment`);
- `--fluid25d-catchment-view composite|water-isolation|flow-inspection`
  (windowed or headless catchment presentation; default: `composite`; cannot
  be combined with `--fluid25d-view diagnostics`);
- `--fluid25d-presentation-time-scale <0.125..8>` (windowed-only; defaults to
  `1`, with `4` and `8` useful for review playback). Fast requested playback is
  best-effort: slow or stalled rendering can hit the four-step catch-up cap and
  drop excess backlog;
- `--fluid25d-solver virtual-pipes|finite-volume` (default: `virtual-pipes`;
  finite-volume is an opt-in numerical comparison);
- `--fluid25d-scenario dry-bed|lake-at-rest|river-catchment|source-outlet-demo|terrain-case|boundary-drain-fixture`;
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

The normal headless command produces the same static oblique Composite
catchment framing as the windowed default. To compare the reading aids in
reproducible captures, select the catchment presentation explicitly:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 8 \
  --fluid25d-catchment-view water-isolation --output fluid-25d-water-isolation.png

build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 8 \
  --fluid25d-catchment-view flow-inspection --output fluid-25d-flow-inspection.png
```

Virtual-pipes does not read back simulation state;
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

In a window, the compact Fluid 2.5D panel selects the Catchment or Diagnostics
surface; while Catchment is selected, a second control chooses Composite, Water
Isolation, or Flow Inspection. The catchment choice is retained when `A`
switches to Diagnostics and back. The panel also exposes Pause/Resume, Reset,
and the supported `0.125..8x` windowed playback speed. The legend reads the
terrain as the matte bed, bright cyan as shallower water, and darker blue as
deeper water. The moving highlight is a passive render-only marker advected by
velocity; it is not waves or a depth cue. Left-drag or scroll orbits/zooms the
catchment; `Space` pauses, `R` resets the deterministic scenario, and `D`
cycles diagnostic contents. UI controls only affect presentation/pacing; solver
fixed delta, headless timing, and numerical evidence remain unchanged.

In Flow Inspection, arrows stay anchored to a uniform grid: angle means local
flow direction, and length/brightness mean speed. They sample the velocity
field and are not moving water particles. The source-to-outlet scene retains
its green SOURCE and amber OUTLET rings in this view.

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

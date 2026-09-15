# Fluid 2.5D

`fluid_25d` is Cubey's terrain-water project: a 2D shallow-water simulation
over a heightfield terrain. Water should flow downhill, pool in basins, follow
an authored channel, and drain through an explicit sink without requiring a
full 3D fluid volume.

The broader technique map lives in
[`docs/architecture/fluid-simulation.md`](../../../docs/architecture/fluid-simulation.md).

## Status

River V0 is currently in the numerical-contract phase. The project has a
project-local CPU virtual-pipes oracle and deterministic dry-bed,
lake-at-rest, and river source/sink fixtures. No windowed application, Vulkan
resources, shaders, or renderer target exists yet.

The Config V2 default grid (`32x16`) is intentionally an oracle-phase fixture,
not an eventual runtime product default. The bounded runtime direction remains
a separate roughly `256x128` catchment decision after GPU evidence.

The CPU target is a reference for the later GPU implementation, not a shared
fluid framework and not a claim of scientific Saint-Venant fidelity.

## Product boundary

The first product story is an authored upland catchment:

```text
source -> terrain-guided channel/basin -> explicit downstream sink
```

River V0 includes:

- deterministic terrain and scenario generation;
- one or more depth-field sources and sinks;
- virtual-pipes shallow-water evolution;
- water depth, surface height, outgoing face flux, velocity, and wet/dry state;
- explicit source/sink volume accounting;
- numerical fixtures that can be compared across CPU and future GPU paths.

It deliberately excludes erosion, sediment, rainfall, interactive editing,
spray, breaking waves, open or periodic outer boundaries, and high-order flood
modeling claims. Flooding and an interactive terrain/water toy are later
products, not hidden requirements of River V0.

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

This is a compact virtual-pipes reference scheme. A well-balanced,
positivity-preserving finite-volume comparison may be warranted later if the
River V0 fixtures show that virtual pipes cannot provide the desired still-water
or wetting-front behavior.

## Deterministic fixtures

The project-local scenario generator provides three analytic fixtures:

- `dry-bed`: uneven terrain with zero initial water and no rates;
- `lake-at-rest`: uneven terrain with a constant free-surface height and no
  source or sink;
- `river-catchment`: a downhill quadratic channel seeded with a thin water path,
  one source near the upstream end, and one initially dry downstream sink.

The CPU tests cover deterministic generation, configuration parsing and
validation, dry-bed stability, lake-at-rest preservation, finite/nonnegative
depth, closed-domain mass conservation, and source/sink ledger reconciliation.

## Configuration

River V0 uses a project-owned Config V2 facade. It composes the shared 2D grid
schema and currently exposes:

- `--grid-width`, `--grid-height`, and `--grid-size`;
- `--fluid25d-scenario dry-bed|lake-at-rest|river-catchment`;
- `--fluid25d-cell-size-m`;
- `--fluid25d-fixed-delta-seconds`;
- `--fluid25d-substeps`;
- `--fluid25d-gravity-m-per-s2`;
- `--fluid25d-flow-damping-per-second`;
- `--fluid25d-minimum-wet-depth-m`.

The facade is exercised by the CPU target even though no runtime target exists
yet. Common host options are accepted only as part of the eventual project
boundary; they do not imply a working windowed or headless application today.

## Source layout

- `projects/fluid/fluid_25d`: project-facing CMake and design authority;
- `projects/fluid/sim/fluid_25d`: project-local config, scenarios, CPU oracle,
  and focused tests.

The solver is intentionally not coupled to `projects/terrain`, `projects/ocean`,
`TerrainOceanFieldView`, paused hydrology work, or the shared rendering
foundation. Those products may become future consumers only after a concrete
contract is demonstrated.

## Next implementation gate

The next slice can add GPU resources and a compute schedule only after this CPU
contract is treated as the acceptance oracle. The first GPU checkpoint should
match the small fixtures, keep steady-state state on the GPU, and expose depth
and flow diagnostics. Any shared helper or foundation change should wait for a
second independent consumer or a measured bottleneck.

# Fluid 2.5D

`fluid_25d` is Cubey's terrain-water project: a 2D shallow-water simulation
over a heightfield terrain. Water should flow downhill, pool in basins, follow
an authored channel, and drain through an explicit sink without requiring a
full 3D fluid volume.

The broader technique map lives in
[`docs/architecture/fluid-simulation.md`](../../../docs/architecture/fluid-simulation.md).

## Status

River V0 has a project-local GPU checkpoint. `fluid_25d` is a windowed and
headless Vulkan application with a GPU-resident virtual-pipes solve and a
deterministic oblique terrain-and-water catchment presentation. Top-down
diagnostics remain explicitly selectable. It has deterministic dry-bed,
lake-at-rest, and river source/sink fixtures; the headless lake and river lanes
compare GPU depth, face flux, and source/sink ledger against the CPU oracle.

The Config V2 product default is a bounded `256x128` catchment. Focused
CPU/GPU oracle fixtures intentionally set smaller dimensions so their evidence
stays quick and inspectable.

The CPU target remains a reference for the GPU implementation, not a shared
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
- a GPU-resident steady-state solve with no per-frame readback in normal use;
- an implicit oblique grid (no uploaded mesh) with opaque terrain, translucent
  premultiplied-alpha water, depth testing, restrained lighting, and flow cues;
- top-down terrain, depth, surface, flow magnitude/direction, and wet/dry
  diagnostics as an explicit alternate presentation mode;
- opt-in headless CPU/GPU fixtures that read back only at their final evidence
  point.

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
- `--fluid25d-view catchment|diagnostics` (default: `catchment`);
- `--fluid25d-scenario dry-bed|lake-at-rest|river-catchment`;
- `--fluid25d-cell-size-m`;
- `--fluid25d-fixed-delta-seconds`;
- `--fluid25d-substeps`;
- `--fluid25d-gravity-m-per-s2`;
- `--fluid25d-flow-damping-per-second`;
- `--fluid25d-minimum-wet-depth-m`.
- `--debug-view terrain|depth|surface|flow|direction|wet-dry`;
- `--fluid25d-gpu-oracle-validation` (headless-only; performs final-state
  readback against the CPU oracle).

Normal headless capture keeps the simulation GPU-resident:

```sh
build/dev/projects/fluid/fluid_25d/fluid_25d --headless --frames 8 \
  --width 512 --height 256 --output fluid-25d.png
```

The normal headless command produces the same static oblique catchment framing
as the windowed default and does not read back simulation state. To capture a
top-down numerical diagnostic instead, select it deliberately:

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

In a window, left-drag or scroll orbits/zooms the catchment, `Space` pauses,
`R` resets the deterministic scenario, `A` switches between catchment and
diagnostics, and `D` cycles diagnostic contents.

## Source layout

- `projects/fluid/fluid_25d`: project-facing CMake and design authority;
- `projects/fluid/sim/fluid_25d`: project-local config, scenarios, CPU oracle,
  and focused tests.

The solver is intentionally not coupled to `projects/terrain`, `projects/ocean`,
`TerrainOceanFieldView`, paused hydrology work, or the shared rendering
foundation. Those products may become future consumers only after a concrete
contract is demonstrated.

## Next implementation gate

The next slice should gather bounded capture and timing evidence from the
product surface, then decide whether River V0 needs better visual legibility,
more scenario variety, or a numerical comparison. Any shared helper or
foundation change still waits for a second independent consumer or a measured
bottleneck.

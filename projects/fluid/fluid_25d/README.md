# Fluid 2.5D — builtin shallow water

The [shallow-water showcase](https://dev.byebyebryan.com/cubey/demos/simulation/shallow-water/)
shows Narrows and Sweep, an offline relief capture of Cubey’s finite-volume solver.
This guide covers the interactive Vulkan experiments.

A standalone demo of Cubey's own Vulkan shallow-water solvers. This is not the
SynxFlow terrain-water showcase; that lives in [Terrain Water](../../terrain_water/README.md).

## What runs here

Bare startup runs the existing **River V0** authored catchment: 256×128 cells
at 1 m spacing, using virtual pipes. The optional finite-volume solver and the
source/outlet, headwaters, lake-at-rest, drainage and dye-transport cases remain
available. Depth, velocity, quiver and transport diagnostics help read the flow.

These are retained experiments, not a newly polished showcase or an equivalent
mountain-flow backup. No SynxFlow, CUDA worker, Python environment or generated
terrain cache is required for the default authored scene.

## Build and run

```sh
cmake --preset release
cmake --build --preset release --target cubey_project_fluid_25d -j 6
build/release/projects/fluid/fluid_25d/fluid_25d
```

Add `--fluid25d-solver finite-volume` for the retained numerical alternative.
For a capture, add `--headless --frames 120 --output outputs/fluid/FRESH.png`.
The built-in app uses the Original presentation.

## Narrows and Sweep capture

The showcase film follows an inlet surge through 222 m of winding channel.
The [Narrows and Sweep capture guide](../../../studies/fluid_25d/narrows_and_sweep/README.md)
reproduces the solver trace, shared atmosphere, relief, poster and film.

## Ownership and retained history

- Runtime and numerical tests: `projects/fluid/sim/fluid_25d`.
- Shared terrain/water rendering: Cubey foundation; reused without copying math.
- Live SynxFlow, replay, setup and published terrain media: [Terrain Water](../../terrain_water/README.md).
- Earlier research, reports, capture scripts and fixtures: [indexed studies](../../../studies/fluid_25d/README.md).
- Broader family: [fluid architecture](../../../docs/architecture/fluid-simulation.md).

The old `run_terrain_rain_demo.py` is a small compatibility forwarder. Existing
native external/replay flags also remain labelled compatibility entries; no
silent fallback or water-state conversion occurs. Prefer the dedicated Terrain
Water launcher for that experience.

# Terrain

`projects/terrain` is the rebooted terrain workbench. It starts as a local
CPU/reference terrain product generator, not as a direct continuation of
`terrain_lab_legacy`, not as a coastal/ocean demo, and not as a planet renderer.

The first goal is a deterministic product contract that downstream systems can
inspect and eventually consume:

- coherent source fields;
- terrain feature and process fields;
- material and vegetation-potential hints;
- summaries and debug exports.

The current first slice is a temperate mountain river catchment over a local
kilometer-scale grid. Rendering, ocean integration, planet streaming, foliage
rendering, and physically complete erosion are deferred until the product fields
are credible.

The current generator emits source fields, height/slope analysis, static
drainage, active river trunk and tributary fields, wetness/deposition, material
masks, and vegetation potential. The drainage pass is deliberately
process-informed rather than a full hydraulic simulation.

See [Terrain reboot direction](../../docs/architecture/terrain-reboot.md) for
the current design checkpoint.

## Commands

```sh
cmake --build --preset dev --target cubey_project_terrain cubey_project_terrain_tests
ctest --preset dev -R terrain --output-on-failure

./build/dev/projects/terrain/terrain
./build/dev/projects/terrain/terrain --headless --terrain-debug-view final --output outputs/terrain/current/final.png
./build/dev/projects/terrain/terrain --headless --terrain-debug-view flow-accumulation --grid-size 129 --output outputs/terrain/current/flow-accumulation.png
./build/dev/projects/terrain/terrain --headless --grid-size 513 --terrain-debug-view all --terrain-output-dir outputs/terrain/current
```

## Current Review Outputs

Use `--terrain-debug-view all --terrain-output-dir outputs/terrain/current` for
the standard review set. The current local review images are generated at
`513x513`, large enough to inspect the field structure rather than just a tiny
thumbnail. `outputs/` is ignored by git, so this directory is a disposable local
review artifact.

The review set includes:

- `final.png`
- `height.png`
- `slope.png`
- `ridge-uplift.png`
- `flow-accumulation.png`
- `stream-order.png`
- `river-mask.png`
- `river-trunk.png`
- `tributaries.png`
- `wetness.png`
- `deposition.png`
- `material.png`
- `vegetation.png`

The active river fields come from a coherent low-frequency drainage potential
plus routed flow accumulation. `river-mask` is the combined product of the soft
`river-trunk` and `tributaries` fields. The current D8 routing still leaves
visible straight reaches, sharp turns, and angular segments that read less
organic than real rivers; smoothing/vectorizing the extracted channel path or
adding a stronger depression-fill/breach hydrology pass is a later quality
target.

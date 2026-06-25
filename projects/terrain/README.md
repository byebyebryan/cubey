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

The current generator revision is `7`. It emits source fields, height/slope
analysis, static drainage, routing diagnostics, smoothed active river trunk and
tributary fields, wetness/deposition, material masks, and vegetation potential.
The drainage pass is deliberately process-informed rather than a full hydraulic
simulation.

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
./build/dev/projects/terrain/terrain --headless --grid-size 513 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network
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
- `drainage-potential.png`
- `flow-direction.png`
- `flow-accumulation.png`
- `stream-order.png`
- `river-mask.png`
- `river-trunk.png`
- `tributaries.png`
- `sink-mask.png`
- `channel-width.png`
- `wetness.png`
- `deposition.png`
- `material.png`
- `vegetation.png`

The optional `temperate-mountain-river-stress` recipe keeps the same source
terrain and routing diagnostics but expands active channel extraction for review
stress testing. It renders several selected corridor trunks and lower-threshold
tributary support so `outputs/terrain/stress-river-network` exposes more of the
patch to river-network artifacts. Treat it as a diagnostic recipe, not the
default product target.

The active river fields come from a coherent low-frequency drainage potential
plus routed flow accumulation over a padded hidden routing domain. Revision `7`
uses continuous D-Infinity-style flow angles and fractional accumulation for the
diagnostic catchment field, then promotes `stream_order` into connected support
selection for the active product. The default recipe keeps a conservative trunk
and attached branch network. Additional branches are accepted only when they
reach or closely approach an existing active channel, which avoids independent
local strokes. Channel rasterization carries discharge/stream-order width scale
through the path, and the stress recipe renders more selected corridors to expose
coverage, parallel-branch, and straight-segment artifacts that the default
composition may hide. See [Terrain river stream-order corridor plan](../../docs/notes/terrain-river-stream-order-corridor-plan.md).

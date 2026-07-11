# Terrain

`projects/terrain` is the active directly sampled terrain v1 runtime. The CPU
source library currently provides deterministic world-space height and gradient
queries for the shared `mountain`, `upland`, and `plains` parameterized source.
The matching GLSL evaluator and traversable renderer are the next layers of this
same project.

This project does not own regional hydrology or a baked terrain product. The
previous patch, exporter, routing, and analytical landscape code lives in
`projects/terrain_hydrology_lab`.

Build and test the source contract with:

```sh
cmake --build --preset dev --target cubey_project_terrain_source_tests
ctest --preset dev -R '^terrain_source_tests$' --output-on-failure
```

See [`docs/architecture/terrain-v1.md`](../../docs/architecture/terrain-v1.md)
for the complete runtime boundary.

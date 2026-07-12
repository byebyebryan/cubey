# Terrain

`projects/terrain` is the active directly sampled terrain v1 runtime. The CPU
source library currently provides deterministic world-space height and gradient
queries for the shared `mountain`, `upland`, and `plains` parameterized source.
The matching GLSL evaluator consumes the packed resolved parameters and is
checked against CPU samples through Vulkan readback. Optional local weathering
is bounded, footprint-filtered, and explicitly non-hydraulic.

The `terrain` app displaces a camera-centered eight-level clipmap directly from
that GLSL evaluator. It uses shared atmosphere transport and sky irradiance,
terrain-local heightfield shadows, linear-space procedural nonmetal materials,
explicit single-owner LOD transitions, diagnostic views, and a surface
controller whose clearance comes from the CPU query contract.

This project does not own regional hydrology or a baked terrain product. The
previous patch, exporter, routing, and analytical landscape code lives in
`projects/terrain_hydrology_lab`.

Build and test the source contract with:

```sh
cmake --build --preset dev --target \
  cubey_project_terrain_source_tests \
  cubey_project_terrain_source_gpu_parity_tests
ctest --preset dev -R '^terrain_source(_gpu_parity)?_tests$' --output-on-failure

./build/dev/projects/terrain/terrain \
  --terrain-seed 9012 \
  --terrain-preset mountain \
  --terrain-weathering local \
  --terrain-camera-preset surface

./build/dev/projects/terrain/terrain_source_report
projects/terrain/capture_v1_review.sh
projects/terrain/capture_rendering_review.sh
```

The source review pack includes multi-seed shape and presentation sheets. The
rendering-refinement pack adds multi-sun clay, component diagnostics, one- and
two-meter ground controls, a TerrainEngine control, and a deterministic
eye-level traversal video under `outputs/terrain/rendering-refinement/`.

Source presets are `mountain`, `upland`, and `plains`. Weathering is `off` or
`local`. Camera presets are `oblique`, `profile`, `top`, `surface`,
`surface-low`, and `ground`; debug views are `surface`, `height`, `base-height`,
`slope`, `weathering`, `lod`, `clay`, `shadow`, and `aerial-transmittance`.

See [`docs/architecture/terrain-v1.md`](../../docs/architecture/terrain-v1.md)
for the complete runtime boundary and
[`docs/notes/terrain-v1-runtime-checkpoint.md`](../../docs/notes/terrain-v1-runtime-checkpoint.md)
for the fixed review pack and current measured baseline.

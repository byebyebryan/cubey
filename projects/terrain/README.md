# Terrain

`projects/terrain` is the active directly sampled terrain runtime. The default
remains the v1 control path. An opt-in mountain quality prototype adds adaptive
tessellation, source v2 detail, and generated mipmapped material tiles. The CPU
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
  --terrain-camera-preset backdrop \
  --terrain-presentation backdrop

./build/dev/projects/terrain/terrain \
  --terrain-seed 9012 \
  --terrain-preset mountain \
  --terrain-render-path quality \
  --terrain-source-version v2 \
  --terrain-target-edge-px 4 \
  --terrain-camera-preset backdrop

./build/dev/projects/terrain/terrain_source_report
projects/terrain/capture_v1_review.sh
projects/terrain/capture_rendering_review.sh
projects/terrain/capture_backdrop_review.sh
projects/terrain/capture_resolution_bandwidth_review.sh
```

The source review pack includes multi-seed shape and presentation sheets. The
rendering-refinement pack adds multi-sun clay, component diagnostics, one- and
two-meter ground controls, a TerrainEngine control, and a deterministic
eye-level traversal video under `outputs/terrain/rendering-refinement/`.
The backdrop pack adds a nine-case framing matrix, standard/coverage controls,
distance controls, a 1920 x 1080 showcase, and a moving surface diagnostic under
`outputs/terrain/backdrop-presentation/`.

Source presets are `mountain`, `upland`, and `plains`. Weathering is `off` or
`local`. Camera presets are `oblique`, `profile`, `top`, `surface`,
`surface-low`, `ground`, and `backdrop`; debug views are `surface`, `height`,
`base-height`, `slope`, `weathering`, `lod`, `clay`, `shadow`,
`aerial-transmittance`, `vegetation-coverage`, `tessellation-factor`,
`projected-edge`, `source-bands`, `material-albedo`, and `material-normal`. The
`backdrop` camera is a deterministic source-aware frame intended for terrain
beginning about 300 m from the visible foreground. It uses at least 150 m AGL
and raises candidates when final terrain would violate that foreground
boundary. Presentation modes are `standard` (default) and `backdrop`.

See [`docs/architecture/terrain-v1.md`](../../docs/architecture/terrain-v1.md)
for the complete runtime boundary and
[`docs/notes/terrain-v1-runtime-checkpoint.md`](../../docs/notes/terrain-v1-runtime-checkpoint.md)
for the fixed review pack and current measured baseline.

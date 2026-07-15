# Terrain

`projects/terrain` is the active directly sampled terrain runtime. The default
remains the v1 control path. Opt-in mountain quality prototypes add adaptive
tessellation and source v2 detail. Source v2.1 preserves v2 above a 64 m
footprint while moving sub-110 m detail into bounded additive relief. The
opt-in `layered` surface-detail mode adds generated albedo-height and
normal-roughness-cavity material layers for the supported backdrop and
midground tiers. The CPU source library currently
provides deterministic world-space height and gradient queries for the shared
`mountain`, `upland`, and `plains` parameterized source.
The matching GLSL evaluator consumes the packed resolved parameters and is
checked against CPU samples through Vulkan readback. Optional local weathering
is bounded, footprint-filtered, and explicitly non-hydraulic.

The `terrain` app displaces a camera-centered eight-level clipmap directly from
that GLSL evaluator. It uses shared atmosphere transport and sky irradiance,
terrain-local heightfield shadows, linear-space procedural nonmetal materials,
explicit single-owner LOD transitions, diagnostic views, and a surface
controller whose clearance comes from the CPU query contract.

The `backdrop` preset is a 360-degree orbit stage around a local mid-air
foreground focus. Detached mode reserves the inner 300 m for a consuming scene,
maps a deterministically selected source location under that local stage, and
solves a vertical offset that keeps lower-frame terrain at least 1.5 km away.
`backdrop-stage` adds a neutral foreground proxy for interactive validation
without changing the clean backdrop product view. Grounded mode keeps terrain
continuous as a placement diagnostic. `midground` retains the older directional
surface stress view.

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
  --terrain-source-version v2.1 \
  --terrain-surface-detail layered \
  --terrain-target-edge-px 4 \
  --terrain-camera-preset backdrop \
  --terrain-backdrop-mode detached \
  --terrain-backdrop-orbit-radius 100 \
  --terrain-backdrop-elevation 8 \
  --terrain-presentation backdrop

./build/dev/projects/terrain/terrain_source_report
./build/dev/projects/terrain/terrain_source_report \
  --source-version v2.1 --scale-response
./build/dev/projects/terrain/terrain_backdrop_stage_report
projects/terrain/capture_v1_review.sh
projects/terrain/capture_rendering_review.sh
projects/terrain/capture_backdrop_review.sh
projects/terrain/capture_resolution_bandwidth_review.sh
projects/terrain/capture_midground_detail_review.sh
projects/terrain/capture_midground_correction_review.sh
projects/terrain/capture_source_v2_1_review.sh
projects/terrain/capture_orbit_stage_review.sh
```

The source review pack includes multi-seed shape and presentation sheets. The
rendering-refinement pack adds multi-sun clay, component diagnostics, one- and
two-meter ground controls, a TerrainEngine control, and a deterministic
eye-level traversal video under `outputs/terrain/rendering-refinement/`.
The backdrop pack adds a nine-case framing matrix, standard/coverage controls,
distance controls, a 1920 x 1080 showcase, and a moving surface diagnostic under
`outputs/terrain/backdrop-presentation/`.

Source presets are `mountain`, `upland`, and `plains`. Weathering is `off` or
`local`. Surface detail is `tile` (default) or mountain-quality-only `layered`.
Camera presets include `oblique`, `profile`, `top`, `surface`, `surface-low`,
`ground`, `backdrop`, `backdrop-stage`, and `midground`. The deterministic
source-aware backdrop planner evaluates 24 azimuth sectors and supports
unrestricted yaw within a 50-150 m orbit. Detached elevation is limited to
4-12 degrees and defaults to a 1.5 km lower-frame terrain exclusion. Grounded
elevation is limited to 12-32 degrees. Initial azimuth, radius, elevation, and
validation distance are optional controls. `midground` remains the directional
1.6 km detail stress tier. Presentation modes are `standard` (default) and
`backdrop`.

Source versions are `v1` (default), mountain-only `v2` and `v2.1`, and the
retained experimental `v3` hierarchy. V2.1 and v3 use dedicated shader bundles
so their opt-in source evaluators do not inflate v1/v2 pipeline compilation.
Debug views include final/base height,
slope, weathering, LOD, clay, shadow,
aerial transmittance, vegetation coverage, source/material normals, material
weights, projected edges, source bands, albedo, roughness, blend height, and
cavity. `classification-normal` shows the geometry-footprint normal that owns
macro material selection, while `normal` includes optional layered source
recovery. The fixed v3 A/B pack remains under
`outputs/terrain/midground-detail-v3/`; the accepted correction pack is under
`outputs/terrain/midground-correction-v4/`. The focused source v2.1 comparison
is under `outputs/terrain/source-v2-1/`.

See [`docs/architecture/terrain-v1.md`](../../docs/architecture/terrain-v1.md)
for the complete runtime boundary and
[`docs/notes/terrain-v1-runtime-checkpoint.md`](../../docs/notes/terrain-v1-runtime-checkpoint.md)
for the fixed review pack and current measured baseline.

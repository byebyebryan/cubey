# Terrain

`projects/terrain` now provides a cached, fixed-focus far-backdrop product. The
control clipmap and opt-in adaptive tessellation path remain explicit review
controls. Source v2.1 preserves v2 above a 64 m
footprint while moving sub-110 m detail into bounded additive relief. The
opt-in `layered` surface-detail mode adds generated albedo-height and
normal-roughness-cavity material layers for the retained live backdrop and
midground controls. The CPU source library currently provides deterministic
world-space height and gradient queries for the shared
`mountain`, `upland`, and `plains` parameterized source.
The matching GLSL evaluator consumes the packed resolved parameters and is
checked against CPU samples through Vulkan readback. Optional local weathering
is bounded, footprint-filtered, and explicitly non-hydraulic.

The production `backdrop` path samples source v2.1 once into 48 static polar
sectors spanning 3.2-16.384 km. Cached heights, normals, material
classification, and ambient visibility feed a non-tessellated environment-lit
shader. Conservative angular and frustum culling keep the terrain-only 1440p
GPU pass below the v1 1 ms p95 budget. It performs no runtime source,
weathering, terrain-shadow, tessellation, or material-tile evaluation.

The retained control path uses a camera-centered eight-level clipmap with
explicit single-owner LOD transitions. The opt-in quality path uses a camera-centered,
world-aligned 128 by 128 tile field with shared edges and screen-driven adaptive
tessellation over the same approximate coverage. It uses shared atmosphere
transport and sky irradiance, terrain-local heightfield shadows, linear-space
procedural nonmetal materials, diagnostic views, and a surface controller whose
clearance comes from the CPU query contract.

The `backdrop` preset is a 360-degree orbit stage around a local mid-air
foreground focus. Detached mode reserves the inner 300 m for a consuming scene,
maps a deterministically selected source location under that local stage, and
solves a vertical offset that keeps lower-frame terrain at least 3.2 km away.
`backdrop-stage` adds a neutral foreground proxy for interactive validation
without changing the clean backdrop product view. Grounded mode keeps terrain
continuous as a placement diagnostic. `midground` retains the older directional
surface stress view.

This project does not own regional hydrology. The active pivot adds a
project-local baked backdrop product; it does not promote a general terrain
cache or streaming interface into engine foundation. The
previous patch, exporter, routing, and analytical landscape code lives in
`projects/terrain_hydrology_lab`.

Build and test the source contract with:

```sh
cmake --build --preset dev --target \
  cubey_project_terrain_source_tests \
  cubey_project_terrain_source_gpu_parity_tests \
  cubey_project_terrain_source_study \
  cubey_project_terrain_source_study_report \
  cubey_project_terrain_directional_backdrop_study \
  cubey_project_terrain_directional_backdrop_report
ctest --preset dev -R 'terrain_(source(_gpu_parity|_study)?|directional.*)_tests' \
  --output-on-failure

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
projects/terrain/capture_midair_stage_review.sh
projects/terrain/capture_quality_tile_review.sh
projects/terrain/capture_cached_backdrop_review.sh
projects/terrain/capture_source_model_study.sh
projects/terrain/capture_mountains_source_decision.sh
projects/terrain/capture_directional_backdrop_study.sh
projects/terrain/capture_directional_backdrop_expanded.sh
projects/terrain/capture_radial_backdrop_expanded.sh
```

The source review pack includes multi-seed shape and presentation sheets. The
rendering-refinement pack adds multi-sun clay, component diagnostics, one- and
two-meter ground controls, a TerrainEngine control, and a deterministic
eye-level traversal video under `outputs/terrain/rendering-refinement/`.
The backdrop pack adds a nine-case framing matrix, standard/coverage controls,
distance controls, a 1920 x 1080 showcase, and a moving surface diagnostic under
`outputs/terrain/backdrop-presentation/`.
The quality tile review covers six yaw directions, the supported stage
radius/elevation envelope, three native-resolution seeds, geometry diagnostics,
a profiled full orbit, and detached-stage ownership under
`outputs/terrain/quality-tile-v1/`.
The accepted cached-backdrop review covers three seeds, six relative azimuths,
the full orbit envelope, cached diagnostics, setup cost, and the terrain-only
GPU gate under `outputs/terrain/cached-backdrop-v1/`.

The isolated source-model study compares clean-room mountain operator families
through the same cached-backdrop renderer without changing the production
source. Its contract and provenance boundaries are recorded in
[`docs/notes/terrain-source-model-study.md`](../../docs/notes/terrain-source-model-study.md).
`projects/terrain_ref` remains frozen and is not linked by the study.
The original study pack remains under `outputs/terrain/source-model-study-v1/`.
The current script includes the corrected Mountains hierarchy candidate and
writes fixed-range top-field, slope, clay, and common-material contact sheets
under `outputs/terrain/source-model-study-v2/`. Run it headlessly
with `projects/terrain/capture_source_model_study.sh`; top-level `REVIEW.txt`
defines the review order and raw frames remain grouped by recipe and seed.
The focused Mountains decision pack uses only v2.1, the old signed candidate,
and the corrected hierarchy candidate, plus exact-reference scale diagnostics,
under `outputs/terrain/mountains-source-decision-v2/`.

The directional backdrop study compares the accepted hard cut against a
continuous center, source-only placement, and one-sided relief composition.
Its multi-seed pack is under
`outputs/terrain/directional-backdrop-study-v1/`; the completed decision in
[`docs/notes/terrain-directional-backdrop-study.md`](../../docs/notes/terrain-directional-backdrop-study.md)
rejects both directional lanes and leaves production defaults unchanged.

The opt-in `expanded-shaped` follow-up extends only the study product to
`32.768 km`, raises its focus `100-1000 m`, and allows a `100-1000 m` orbit.
Its surface-first and expanded-domain source pack is under
`outputs/terrain/directional-backdrop-expanded-v1/`. The pack improves
far-field composition but still exposes a directional shaping band and misses
the production GPU budget, so it remains diagnostic-only.

The companion `expanded-radial` lane uses the same expanded terrain and camera
envelope but restores broad structure over `1-24 km` and source detail over
`5-30 km` in every direction. Its current comparison pack is written to
`outputs/terrain/radial-backdrop-expanded-v2/`. The broad band stays hidden in
the tested scene views and fills the directional lane's empty headings, but its
circular low-relief basin remains explicit in diagnostics. V2 is now the
accepted macro-composition baseline, while the current cached hard-cut backdrop
remains production until radial composition passes cached integration, detail,
and the `<1 ms` runtime gate. See
[`docs/notes/terrain-radial-backdrop-macro-baseline.md`](../../docs/notes/terrain-radial-backdrop-macro-baseline.md).

Source presets are `mountain`, `upland`, and `plains`. Weathering is `off` or
`local`. Surface detail is `tile` (default) or mountain-quality-only `layered`.
Camera presets include `oblique`, `profile`, `top`, `surface`, `surface-low`,
`ground`, `backdrop`, `backdrop-stage`, and `midground`. The deterministic
source-aware backdrop planner evaluates 24 azimuth sectors and supports
unrestricted yaw within a 50-250 m orbit. Detached elevation is limited to
0-30 degrees and defaults to a 3.2 km lower-frame terrain exclusion. Grounded
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
weights, projected edges, tessellation factors, detached stage ownership,
source bands, albedo, roughness, blend height, and cavity.
`classification-normal` shows the geometry-footprint normal that owns macro
material selection, while `normal` includes optional layered source recovery.
The fixed v3 A/B pack remains under
`outputs/terrain/midground-detail-v3/`; the accepted correction pack is under
`outputs/terrain/midground-correction-v4/`. The focused source v2.1 comparison
is under `outputs/terrain/source-v2-1/`.

The general terrain default remains source v1 plus the control renderer. A
backdrop camera defaults to source v2.1, the cached `backdrop` render path, and
`high` backdrop mesh density unless those options are explicit. `low` and
`medium` densities are diagnostic controls.

See [`docs/architecture/terrain-v1.md`](../../docs/architecture/terrain-v1.md)
for the complete runtime boundary and
[`docs/notes/terrain-v1-runtime-checkpoint.md`](../../docs/notes/terrain-v1-runtime-checkpoint.md)
for the fixed review pack and current measured baseline.

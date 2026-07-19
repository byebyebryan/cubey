# Terrain

`projects/terrain` now provides a cached, fixed-focus far-backdrop product. Its
default `radial-v1` profile bakes the graduated `mountains-hierarchy-v2` source
over a 32.768 km domain, reserves a 6 km low-relief foreground footprint, and
uses stride-3 render indices. The previous `hard-cut-v1` profile remains an
explicit regression control. The control clipmap and opt-in adaptive
tessellation path remain review tools. Source v2.1 preserves v2 above a 64 m
footprint while moving sub-110 m detail into bounded additive relief. The
opt-in `layered` surface-detail mode adds generated albedo-height and
normal-roughness-cavity material layers for the retained live backdrop and
midground controls. The CPU source library currently provides deterministic
world-space height and gradient queries for the shared
`mountain`, `upland`, and `plains` parameterized source.
The matching GLSL evaluator consumes the packed resolved parameters and is
checked against CPU samples through Vulkan readback. Optional local weathering
is bounded, footprint-filtered, and explicitly non-hydraulic.

The production `backdrop` path samples its selected source once into 48 static
polar sectors. Cached heights, normals, material classification, and ambient
visibility feed a non-tessellated environment-lit shader. Conservative angular
and frustum culling bound submitted geometry, and no procedural source,
weathering, terrain-shadow, tessellation, or material-tile evaluation runs per
frame. Radial-v1 currently misses the engine's eventual `<1 ms` terrain-pass
target, so setup persistence and render optimization remain product debt.

The retained control path uses a camera-centered eight-level clipmap with
explicit single-owner LOD transitions. The opt-in quality path uses a camera-centered,
world-aligned 128 by 128 tile field with shared edges and screen-driven adaptive
tessellation over the same approximate coverage. It uses shared atmosphere
transport and sky irradiance, terrain-local heightfield shadows, linear-space
procedural nonmetal materials, diagnostic views, and a surface controller whose
clearance comes from the CPU query contract.

The radial-v1 `backdrop` preset is an unrestricted-yaw orbit around a 500 m
mid-air focus. It supports a 100-1000 m orbit and 0-30 degree elevation. The
default `continuous` center keeps the standalone product view complete;
`--terrain-backdrop-center consumer-owned` removes the center for a scene that
provides its own foreground. `backdrop-stage` adds a neutral foreground proxy
for interactive validation without changing terrain geometry. `midground`
retains the older directional surface stress view.

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
  cubey_project_terrain_external_source_study \
  cubey_project_terrain_raster_height_source_tests \
  cubey_project_terrain_directional_backdrop_study \
  cubey_project_terrain_directional_backdrop_report
ctest --preset dev -R 'terrain_(source(_gpu_parity|_study)?|directional.*)_tests' \
  --output-on-failure

./build/dev/projects/terrain/terrain \
  --terrain-seed 9012 \
  --terrain-camera-preset backdrop-stage

./build/dev/projects/terrain/terrain \
  --terrain-seed 9012 \
  --terrain-camera-preset backdrop-stage \
  --terrain-backdrop-center consumer-owned

./build/dev/projects/terrain/terrain \
  --terrain-seed 9012 \
  --terrain-backdrop-profile hard-cut-v1 \
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
projects/terrain/capture_terrain_diffusion_bakeoff.sh
projects/terrain/capture_natural_raster_stage.sh
projects/terrain/capture_natural_raster_continuous_refinement.sh
projects/terrain/capture_directional_backdrop_study.sh
projects/terrain/capture_directional_backdrop_expanded.sh
projects/terrain/capture_radial_backdrop_expanded.sh
projects/terrain/capture_radial_backdrop_product.sh
```

The source review pack includes multi-seed shape and presentation sheets. The
Terrain Diffusion bakeoff generates or reuses the pinned external fields, then
compares them with both internal controls through one renderer under
`outputs/terrain/terrain-diffusion-bakeoff-v1/`. Generated fields and model
weights remain ignored artifacts.

The rendering-refinement pack adds multi-sun clay, component diagnostics,
one- and two-meter ground controls, a TerrainEngine control, and a deterministic
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

The separate external-generator bakeoff evaluates pinned Terrain Diffusion
fields through a study-only raster adapter. It preserves elevation and climate
artifacts but renders only elevation, and it cannot change the production
source in the same batch. Its contract is recorded in
[`docs/notes/terrain-external-generator-bakeoff.md`](../../docs/notes/terrain-external-generator-bakeoff.md).
The completed comparison remains reference-only because its stronger raw
morphology did not survive the common backdrop placement and framing contract;
see the
[`bakeoff review`](../../docs/notes/terrain-external-generator-bakeoff-review.md).

The follow-up natural-raster staging study keeps those pinned fields unchanged
while selecting a lower directional stage. Its original cutout comparison is
under `outputs/terrain/terrain-diffusion-stage-v1/`. The maintained continuous
refinement pack is under
`outputs/terrain/terrain-diffusion-continuous-refinement-v1/`; it replaces the
coarse split center with a budget-neutral uniform radial allocation and retains
the `500 m` focus. See
[`terrain-natural-raster-staging.md`](../../docs/notes/terrain-natural-raster-staging.md)
for the contract and verdict. This remains a source/renderer study and does not
promote Terrain Diffusion into the runtime product.

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
circular low-relief basin remains explicit in diagnostics. V2 became the
accepted macro-composition baseline and now underpins the radial-v1 product.
See
[`docs/notes/terrain-radial-backdrop-macro-baseline.md`](../../docs/notes/terrain-radial-backdrop-macro-baseline.md).

The `cached-radial` study lane keeps that accepted macro composition and full
baked source product while reducing only its render index topology to stride 2
or 3. Run `projects/terrain/capture_cached_radial_backdrop.sh` to write the
multi-seed visual, camera-envelope, diagnostic, setup-cost, workload, and 1440p
GPU comparison to `outputs/terrain/cached-radial-v1/`. The completed pack found
stride 3 visually sufficient and measured `1.338 ms` p95 in that run. The
follow-up product pack verifies exact study parity, both center ownership modes,
three seeds, six headings, and the full camera envelope under
`outputs/terrain/radial-backdrop-product-v1/`. Its active-clock profile measured
`1.677 ms` mean, `1.517 ms` p50, and `2.552 ms` p95 on the same RTX 5070 Ti.
The current product checkpoint uses a `2 ms` mean/p50 target and retains p95 as
tail telemetry because it is sensitive to GPU power-state residency.
See
[`docs/notes/terrain-cached-radial-integration.md`](../../docs/notes/terrain-cached-radial-integration.md).

The follow-up fixed-control topology comparison is generated by
`projects/terrain/capture_radial_lod_ab.sh` under
`outputs/terrain/radial-lod-ab-v1/`. It compares stride 1, 2, and 3 against the
same cached source, seed, camera, and presentation at the 100 m stress and
400 m product distances. Stride 1 submits about 1.67 million visible triangles
instead of stride 3's 190 thousand, but the focused final surface frames differ
by only about `0.17%` normalized RMSE and show no meaningful silhouette uplift.
The apparent low-poly read is therefore dominated by source shape, normal, and
material bandwidth rather than the current index density. Keep stride 3 for the
v1 product. Projected-error LOD remains useful for a wider camera envelope or
future optimization, but it is not the next visual-quality fix.

Source presets are `mountain`, `upland`, and `plains`. Weathering is `off` or
`local`. Surface detail is `tile` (default) or mountain-quality-only `layered`.
Camera presets include `oblique`, `profile`, `top`, `surface`, `surface-low`,
`ground`, `backdrop`, `backdrop-stage`, and `midground`. The radial profile
supports unrestricted yaw within a 100-1000 m orbit and 0-30 degree elevation.
Hard-cut-v1 retains the source-aware 24-sector placement planner and its
narrower detached/grounded diagnostic envelope. Initial azimuth, radius, and
elevation remain optional controls. `midground` remains the directional 1.6 km
detail stress tier. Presentation modes are `standard` (default outside
radial-v1) and `backdrop`.

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
backdrop camera defaults to the cached radial-v1 profile, continuous center,
high mesh density, and backdrop presentation. Radial-v1 owns its source,
weathering, domain, stage, and stride contract; generic source overrides are
rejected. Select `hard-cut-v1` explicitly for historical v2.1 controls. `low`
and `medium` densities remain hard-cut diagnostic controls.

See [`docs/architecture/terrain-v1.md`](../../docs/architecture/terrain-v1.md)
for the complete runtime boundary and
[`docs/notes/terrain-v1-runtime-checkpoint.md`](../../docs/notes/terrain-v1-runtime-checkpoint.md)
for the fixed review pack and current measured baseline.

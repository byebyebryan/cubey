# Terrain

`projects/terrain` is Cubey's active fixed-focus far-field terrain backdrop and
review application. It consumes an external `cubey.terrain.heightfield.v1`
asset, selects a deterministic source placement, bakes one continuous cached
mesh, and can replace that placement at runtime while rendering with shared
atmosphere, clouds, and HDR composition.

This is deliberately not a general terrain engine. It does not provide close
terrain, traversal, streaming, hydrology, water, vegetation, deformation,
collision, or planet projection.

## Product Contract

The active path is fixed:

- regular external float heightfield with validated metadata, coverage, and
  exact payload SHA-256;
- deterministic selected placement over the unchanged source field, with
  unfiltered center and indexed-sample comparison controls;
- 200 m default foreground height, adjustable from 2-1000 m in the UI, and
  unrestricted orbit yaw;
- 50-1000 m live inspection orbit radius and unrestricted elevation; the baked
  clearance contract remains qualified only through 250 m;
- continuous seam-matched center with full radial rings, 16.384 km outer
  radius, and angular/outer render stride 3;
- cullable static sectors plus an optional foreground review sphere;
- flat and filtered planar procedural-detail material presentations;
- cached hardware-filtered directional shadows from the outer backdrop
  sectors, with texel-scaled receiver normal bias, a bounded low-sun confidence
  transition, and the continuous inner stage retained as a receiver only;
- shared physical atmosphere, a running daytime solar clock, depth-aware
  Cloud V1 composition, environment lighting, and HDR post;
- bounded 20% default terrain aerial perspective, adjustable from 0-100% for
  scene-specific distance haze without changing the shared atmosphere;
- height, slope, material, normal, edge, and ownership diagnostics.

The renderer does not modify the source shape. The center is regular terrain,
not a cutout, flattened stage, or radial source mask.

The 500 m baked stage remains the clearance-qualified far-field reference.
The 100 m lane is the explicit close stress view. Lower review heights
intentionally relax the clearance guarantee so hero and surface-level views can
expose topology, source, and material limitations.

## Generate The Asset

The canonical development field is a 2048 x 2048, 30 m seed-0 Terrain
Diffusion result. Generate it explicitly:

```sh
cmake --build --preset dev --target cubey_terrain_generate_default_asset
```

The target uses `CUBEY_TERRAIN_DIFFUSION_ROOT` when provided. Otherwise it
creates a pinned source checkout, Python environment, and data cache under the
Git-ignored worktree path `cache/terrain/tooling/v1`. Generated runtime files
are written to `cache/terrain/sources/v1/default`, shared by Debug and Release,
and are not committed. Build-clean targets do not own these paths. A complete
bundle is reused only after its manifest contract, finite values, and actual
payload SHA-256 pass validation; explicitly rebuild it with:

```sh
cmake --build --preset dev --target cubey_terrain_regenerate_default_asset
```

Normal configure, build, and test never download or generate this data. If the
default or selected asset is missing, both GUI and headless startup fail with
the generation command. There is no procedural fallback.

The dependency-free source-cache validator is part of normal CTest. The pinned
NumPy/Torch producer suite remains available through the explicit tool wrapper
when changing generation code; ordinary terrain runtime changes do not need to
initialize that environment.

The loaded source bundle is separate from the derived backdrop-product cache.
The runtime stores versioned compact CPU mesh/material products under
`cache/procedural/v1/terrain.backdrop.product`. Cache identity includes source
and optional climate SHA-256, placement, surface model, topology, stride, and
codec versions. A hit still validates and loads the source manifest and plans
placement, then decodes the product before following the normal GPU install
path. A miss or rejected entry regenerates and atomically republishes without
making cache IO a runtime requirement.

Force only the derived product to rebuild with:

```sh
rm -rf cache/procedural/v1/terrain.backdrop.product
```

This preserves the Terrain Diffusion source, checkout, environment, and model
data. Use `cubey_terrain_regenerate_default_asset` only when the external source
itself must be regenerated.

The optional climate companion for the canonical field and the five-region
cross-climate calibration pack are also explicit targets:

```sh
cmake --build --preset dev --target cubey_terrain_generate_surface_study_asset
cmake --build --preset dev \
  --target cubey_terrain_generate_climate_calibration_assets
```

The calibration pack is written under
`cache/terrain/sources/v1/climate-calibration`. It is evidence for the
experimental surface model, not a production asset or biome contract.

The committed source catalog defines one default and four optional natural
location presets:

```text
mountain-backdrop-1 (default)
alpine-range-1
mountain-valley-1
rolling-hills-1
rolling-lowland-1
```

Normal setup remains one preset and one Terrain Diffusion query. Optional
presets are generated independently, only when requested:

```sh
cmake --build --preset dev --target cubey_terrain_generate_alpine_range_1
cmake --build --preset dev --target cubey_terrain_generate_mountain_valley_1
cmake --build --preset dev --target cubey_terrain_generate_rolling_hills_1
cmake --build --preset dev --target cubey_terrain_generate_rolling_lowland_1
```

Each target queries its pinned location directly and writes an independent,
Git-ignored bundle under `cache/terrain/sources/v1/presets`. It does not rerun
the landscape scans or ranking studies. The recipe catalog, exact source
identity, and expected hashes are committed; generated raster data is not.

## Build And Run

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_project_terrain
./build/dev/projects/terrain/terrain
```

The GUI exposes runtime source selection across the startup field and available
generated climate-calibration regions, source provenance and dimensions,
runtime placement mode and raw-sample index, placement metrics,
orbit radius/elevation, foreground height and reset, foreground-sphere
visibility, flat/detail presentation, supported diagnostics, directional-shadow
state, terrain aerial-perspective strength, atmosphere controls, submitted
geometry, stable GPU timings, and shared cloud controls. Preparation diagnostics
identify cache hit/miss/rejection and report source, climate, placement,
load/decode, generation, encode, and store times independently.

Windowed startup also publishes placeholder lunar/night-sky textures while the
shared atlas runtime prepares cached payloads and uploads a complete
replacement. Headless capture waits for the same request before frame zero.
Atmosphere generation is therefore outside the first-present path without
introducing a second capture-only implementation.

Useful startup overrides:

```sh
./build/dev/projects/terrain/terrain \
  --terrain-heightfield /path/to/field-or-heightfield.json \
  --terrain-placement raw-sample \
  --terrain-placement-index 2 \
  --terrain-foreground-height 500 \
  --terrain-camera-preset backdrop \
  --terrain-surface-detail filtered-detail \
  --terrain-aerial-perspective 0.2 \
  --terrain-shadows \
  --terrain-backdrop-azimuth 90 \
  --terrain-backdrop-orbit-radius 200 \
  --terrain-backdrop-elevation 24
```

Placement choices are `selected`, `raw-center`, and `raw-sample`. CLI values set
the startup placement; the GUI can stage another mode/index and apply it while
the app remains open. CPU resampling runs asynchronously, then the completed
cached product replaces the GPU meshes atomically and resets the orbit while
preserving foreground height. Prior GPU meshes retire after the latest submitted
frame completes, without a queue- or device-idle stall. `raw-sample` uses the
independent deterministic placement index and performs no quality rejection or
retry.

The source selector stages the same complete replacement for the startup source,
optional catalog presets, or available `hot-dry`, `hot-wet`, `cool-wet`,
`cold-dry`, and `cold-wet` calibration regions. It loads paired height and
climate manifests rather than recoloring the active geometry. Optional recipes
remain visible when their data is absent and report `not generated`,
`generating`, or `incomplete`; only complete bundles are selectable. The app
never launches Terrain Diffusion itself. If a region cannot satisfy the
requested placement, the current source remains active and the UI reports the
contract error. Calibration regions remain evidence rather than named
production presets. During replacement, the source panel names both the
resident terrain and requested preset, then reports heightfield, climate,
placement, product-preparation, and GPU-upload phases with elapsed time.

`backdrop-stage` shows the foreground sphere; `backdrop` hides it. Material
choices are `flat` and `filtered-detail`. Supported `--debug-view` values are:

```text
surface height slope clay normal classification-normal material-weights
ambient-visibility material-albedo material-normal material-roughness
vegetation moisture ambient-light direct-light sun-visibility projected-edge
stage-ownership
```

`--terrain-render-stride 1|2|3` is a reference-only startup diagnostic. It
rebuilds the same cached source product with a different fixed topology so
captures can distinguish source-shape defects from draw-mesh faceting. The
product default remains stride 3; this option is not adaptive LOD and is not an
interactive quality setting.

Retired source versions, profiles, weathering, LOD, tessellation, and local
terrain camera modes are rejected by the product app.

## Review

Generate the canonical visual matrix with:

```sh
projects/terrain/capture_product_review.sh
```

The script replaces `outputs/terrain/product-v1`, writes individual 1600 x 900
captures, an index, a manifest, provenance metadata, and a contact sheet when
ImageMagick is available. It covers clean and foreground views, flat/detail,
four headings, camera-envelope endpoints, neutral/raking light, and the
supported source/material/topology diagnostics.

Generate the selected-versus-unfiltered placement control with:

```sh
projects/terrain/capture_placement_control_review.sh
```

That separate pack compares selected placement, the raw source center, and raw
sample indexes 0-2 at matched headings and foreground heights. It also records
the exact placement metrics used by the review.

Selected placement evaluates a 500 m radius around the subject, with limits of
120 m local relief and 0.275 P95 slope. This covers the 300 m stage and the
clearance-qualified 250 m orbit with margin. Wider review-app orbits are stress
views and do not expand the accepted placement contract. Mountain/open
directional composition is a preferred ranking contract, not a source-validity
gate: non-mountain presets activate at the best locally safe candidate and
report `best available` composition in the UI.

Generate the isolated lighting and material candidate pack with:

```sh
projects/terrain/capture_lighting_material_review.sh
```

It writes `outputs/terrain/lighting-material-v1` with matched shadow controls,
four material headings, diagnostics, camera controls, provenance, and steady
plus forced-update GPU profiles. On NVIDIA hosts, a missed timing gate is
retried when `nvidia-smi pmon` reports concurrent compute work.

Generate the current rendering-acceptance pack with:

```sh
projects/terrain/capture_rendering_acceptance_review.sh
```

It uses the cool/wet selected source for 100/200/500 m framing, five sun
elevations, shadow controls, topology/material/lighting diagnostics, all five
climate sources, and steady plus moving-clock profiles. It writes
`outputs/terrain/rendering-acceptance-v1`. Profile lanes wait for external GPU
compute to become idle and retry when overlap is detected.

To rebuild contact sheets and metadata from a complete retained profile set
without running the GPU lanes again, use:

```sh
PROFILE_ONLY=1 SUMMARIZE_ONLY=1 \
  projects/terrain/capture_rendering_acceptance_review.sh
```

Generate the rendering-envelope and fixed-topology decision pack with:

```sh
projects/terrain/capture_rendering_envelope_review.sh
```

It writes `outputs/terrain/rendering-envelope-v1` with deterministic clear and
fair-cloud macro views, qualified and stress camera envelopes, matched stride
1/3 surface and topology diagnostics, and mean/p50/p95 GPU profiles. The pack
is a decision gate for LOD versus source refinement; it does not expand the V1
camera contract.

Generate the cross-climate surface evidence with:

```sh
projects/terrain/capture_climate_calibration_study.sh
```

It writes `outputs/terrain/climate-calibration-v1` with five generated climate
regimes, three matched surface models, fixed-scale source previews, final
surface diagnostics, profile metrics, invariant checks, and contact sheets. It
does not tune the model or change the production default.

Generate the matched Material V2 review in three steps:

```sh
projects/terrain/capture_material_v2_review.sh control
projects/terrain/capture_material_v2_review.sh candidate
projects/terrain/capture_material_v2_review.sh finalize
```

The control must be captured before changing the material shaders. Finalization
checks the frozen heightfield, cached product, topology, stride, and material
allocation; it then writes paired qualified, raking-light, stress, cloud, and
diagnostic sheets under `outputs/terrain/material-v2`.

Generate the accepted V1 visual-closure pack with:

```sh
projects/terrain/capture_visual_closure_review.sh control
projects/terrain/capture_visual_closure_review.sh candidate
projects/terrain/capture_visual_closure_review.sh finalize
```

It uses the canonical mineral-control source, selected and raw-center
placement, four headings, the day-to-night solar envelope, 100/200/500 m
framing, fair clouds, material and lighting diagnostics, and steady plus
moving-clock profiles. Finalization preserves source/product/topology
invariants, requires a pixel-identical flat control, and enforces the accepted
`1.10 ms` mean/p50 terrain composition budget.

The subsequent bounded daylight-form pass widens only the filtered-detail
mineral range and adds restrained slope-aware sky visibility. Its five-source
review, measured GPU gate, and rejected 512/1024 source-normal texture study are
recorded in
[`docs/notes/terrain-daylight-form-v1.md`](../../docs/notes/terrain-daylight-form-v1.md).

## Tests

```sh
cmake --build --preset dev --target \
  cubey_core_tests \
  cubey_project_terrain \
  cubey_project_terrain_config_tests \
  cubey_project_terrain_raster_climate_source_tests \
  cubey_terrain_backdrop_product_tests \
  cubey_terrain_backdrop_product_cache_tests \
  cubey_terrain_backdrop_placement_tests \
  cubey_terrain_directional_placement_tests \
  cubey_project_terrain_product_adapter_tests \
  cubey_project_terrain_shadow_tests \
  cubey_project_terrain_surface_model_tests

ctest --preset dev --output-on-failure \
  -R '^(cubey_core_tests|terrain_.*_tests|terrain_backdrop_headless_writes_png(_stats)?)$'
```

The focused suite verifies the narrow runtime config, raster contract and
filtering, deterministic topology and seams, selected/raw placement, and the
placement-stage camera/coverage contract. It also checks directional-shadow
coverage and cache invalidation. Product captures use the canonical asset
deliberately; ordinary unit tests use small analytical or temporary fixtures.
The real headless terrain smoke uses the deterministic tracked raster under
`tests/assets/terrain/backdrop-smoke` and verifies a nonblank PNG without a
network or generation dependency.

## Studies And Boundaries

Historical visual controls and hydrology work live under `studies/terrain` and
are excluded from default builds. Enable them with:

```sh
cmake --preset dev-terrain-studies
cmake --build --preset dev-terrain-studies
```

`projects/planet` owns planet-scale terrain. The paused hydrology study owns
regional drainage experiments. glTF Viewer is the first external consumer, and
the backdrop API now lives in `include/cubey` and `src/cubey`. Additional
adapters remain deferred until a concrete scene requires one.

See [Terrain V1 Runtime](../../docs/architecture/terrain-v1.md),
[Terrain Product Promotion](../../docs/notes/terrain-product-promotion.md), and
[Terrain Project Map](../../docs/notes/terrain-project-map.md). The default and
optional generation boundary is documented in
[Terrain Source Preset Contract](../../docs/notes/terrain-source-preset-contract.md).
The experimental climate path is documented in
[Terrain Climate Surface Model Research](../../docs/notes/terrain-climate-surface-model-research.md)
and
[Terrain Climate Calibration V1](../../docs/notes/terrain-climate-calibration-v1.md).
The rejected broader climate response is retained only in
[the terrain archive](../../docs/archive/terrain/climate-response-v1-1-rejected.md).

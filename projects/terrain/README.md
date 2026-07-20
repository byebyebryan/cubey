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

- regular external float heightfield with validated metadata and coverage;
- deterministic selected placement over the unchanged source field, with
  unfiltered center and indexed-sample comparison controls;
- 100 m default foreground height, adjustable from 2-1000 m in the UI, and
  unrestricted orbit yaw;
- 50-1000 m live inspection orbit radius and unrestricted elevation; the baked
  clearance contract remains qualified only through 250 m;
- continuous seam-matched center, 16.384 km outer radius, and render stride 3;
- cullable static sectors plus an optional foreground review sphere;
- flat and filtered procedural-detail material presentations;
- cached directional shadows from the outer backdrop sectors, with the
  continuous inner stage retained as a receiver only;
- shared physical atmosphere, a running daytime solar clock, depth-aware
  Cloud V1 composition, environment lighting, and HDR post;
- height, slope, material, normal, edge, and ownership diagnostics.

The renderer does not modify the source shape. The center is regular terrain,
not a cutout, flattened stage, or radial source mask.

The 500 m baked stage remains the clearance-qualified far-field reference.
Lower review heights intentionally relax that clearance guarantee so hero and
surface-level views can expose topology, source, and material limitations.

## Generate The Asset

The canonical development field is a 2048 x 2048, 30 m seed-0 Terrain
Diffusion result. Generate it explicitly:

```sh
cmake --build --preset dev --target cubey_terrain_generate_default_asset
```

The target uses `CUBEY_TERRAIN_DIFFUSION_ROOT` when provided. Otherwise it
creates a pinned source checkout, Python environment, data cache, and model
cache under `build/dev/_deps`. Generated runtime files are written to
`build/dev/assets/terrain/default` and are not committed.

Normal configure, build, and test never download or generate this data. If the
default or selected asset is missing, both GUI and headless startup fail with
the generation command. There is no procedural fallback.

## Build And Run

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_project_terrain
./build/dev/projects/terrain/terrain
```

The GUI exposes source provenance and dimensions, runtime placement mode and
raw-sample index, placement metrics, orbit radius/elevation, foreground height
and reset, foreground-sphere visibility, flat/detail presentation, supported
diagnostics, directional-shadow state, atmosphere controls, submitted geometry,
stable GPU timings, and shared cloud controls.

Useful startup overrides:

```sh
./build/dev/projects/terrain/terrain \
  --terrain-heightfield /path/to/field-or-heightfield.json \
  --terrain-placement raw-sample \
  --terrain-placement-index 2 \
  --terrain-foreground-height 500 \
  --terrain-camera-preset backdrop \
  --terrain-surface-detail filtered-detail \
  --terrain-shadows \
  --terrain-backdrop-azimuth 90 \
  --terrain-backdrop-orbit-radius 200 \
  --terrain-backdrop-elevation 24
```

Placement choices are `selected`, `raw-center`, and `raw-sample`. CLI values set
the startup placement; the GUI can stage another mode/index and apply it while
the app remains open. CPU resampling runs asynchronously, then the completed
cached product replaces the GPU meshes atomically and resets the orbit while
preserving foreground height. `raw-sample` uses the independent deterministic
placement index and performs no quality rejection or retry.

`backdrop-stage` shows the foreground sphere; `backdrop` hides it. Material
choices are `flat` and `filtered-detail`. Supported `--debug-view` values are:

```text
surface height slope clay normal classification-normal material-weights
ambient-visibility material-albedo material-normal material-roughness
sun-visibility projected-edge stage-ownership
```

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

Generate the isolated lighting and material candidate pack with:

```sh
projects/terrain/capture_lighting_material_review.sh
```

It writes `outputs/terrain/lighting-material-v1` with matched shadow controls,
four material headings, diagnostics, camera controls, provenance, and steady
plus forced-update GPU profiles. On NVIDIA hosts, a missed timing gate is
retried when `nvidia-smi pmon` reports concurrent compute work.

## Tests

```sh
cmake --build --preset dev --target \
  cubey_project_terrain_config_tests \
  cubey_project_terrain_raster_height_source_tests \
  cubey_project_terrain_backdrop_product_tests \
  cubey_project_terrain_backdrop_placement_tests \
  cubey_project_terrain_directional_placement_tests \
  cubey_project_terrain_shadow_tests

ctest --preset dev -R '^terrain_.*_tests$' --output-on-failure
```

The focused suite verifies the narrow runtime config, raster contract and
filtering, deterministic topology and seams, selected/raw placement, and the
placement-stage camera/coverage contract. It also checks directional-shadow
coverage and cache invalidation. Product captures use the canonical asset
deliberately; ordinary tests use small analytical or temporary fixtures.

## Studies And Boundaries

Historical visual controls and hydrology work live under `studies/terrain` and
are excluded from default builds. Enable them with:

```sh
cmake --preset dev-terrain-studies
cmake --build --preset dev-terrain-studies
```

`projects/planet` owns planet-scale terrain. The paused hydrology study owns
regional drainage experiments. A second real consumer is required before the
backdrop API moves from this project into `include/cubey` and `src/cubey`.

See [Terrain V1 Runtime](../../docs/architecture/terrain-v1.md),
[Terrain Product Promotion](../../docs/notes/terrain-product-promotion.md), and
[Terrain Project Map](../../docs/notes/terrain-project-map.md).

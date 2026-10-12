# Terrain

`projects/terrain` is Cubey's active fixed-focus far-field terrain backdrop and
review application. It consumes an external `cubey.terrain.heightfield.v1`
asset, selects a deterministic source placement, bakes one continuous cached
mesh, and can replace that placement at runtime while rendering with shared
atmosphere, clouds, and HDR composition.

This is deliberately not a general terrain engine. The normal backdrop does not
provide close terrain, traversal, streaming, hydrology, vegetation, deformation,
collision, or planet projection. An optional captured-water patch is a separate
bounded review view, not a water layer stitched into that backdrop.

## Animated rain-fed capture (opt-in)

The shared foundation can load a single `cubey.terrain.water-capture.v1` bundle
containing source-bound bed, depth and XZ velocity. The captured patch uses the
same positive B-spline basis, 2x triangles, shadows, material masks and water
appearance as Fluid's Scenic renderer. No solver, CUDA, Python environment or
hydraulic dispatch is needed to consume it. Source elevation/placement, payload
sizes, hashes, units and finite fields are checked before rendering.

The current locally retained Mountain bundle can be reviewed with:

```sh
build/release/projects/terrain/terrain \
  --terrain-heightfield cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json \
  --terrain-water-capture cache/terrain/water/v1/mountain-rain-fed-1024-16m/water-capture.json
```

For remote capture, add `--headless --output outputs/terrain/captured-water.png`.
`--terrain-water-capture-camera overview|stream|lake` selects observation-only
framing for this Mountain state. The UI offers **Backdrop / Captured patch**,
**Water visible**, and **Animate surface (not hydrology)**. Hiding water retains
the same paired ground and captured-depth wet material; it does not dry the bed.
Surface shading and flecks animate independently of the fixed numerical fields.

The finite patch is never overlaid with radial ground or radial shadow casters.
Normal placement selection is deferred until Backdrop is actually requested.
The capture view retains Fluid's fixed shared daylight and single HDR display
transform; Terrain's normal solar/cloud controls apply to Backdrop only. Clouds
are explicitly unsupported/off in the capture view because composed water depth
is not yet provided to the depth-aware cloud pass. This does not promote a new
default or claim seamless full-terrain coverage.

An invalid requested capture fails explicitly in headless mode. The GUI instead
disables it with an error and retains the normal source picker/backdrop path.
`--terrain-water-audit` performs a one-time immutable GPU-field readback at
shutdown, outside profiling. The bounded headless transition study uses
`--headless --headless-render-only --frames 8 --terrain-water-transition-smoke`.

The excluded offline exporter is built explicitly:

```sh
cmake --build build/release --target cubey_project_fluid_25d_export_water_capture
build/release/projects/fluid/fluid_25d/cubey_project_fluid_25d_export_water_capture \
  RECORDING.json HEIGHTFIELD.json NEW_OUTPUT_DIRECTORY
```

It selects the existing last rain-on frame, reuses checked momentum-to-velocity
decoding, preserves bed/depth bit-for-bit, and refuses an existing destination.
Generated bundles stay under ignored `cache/`; reports and headless evidence
stay under `outputs/`. Rain-off, new simulations/sites and boundary stitching
remain separate work.

The first paired-surface review is at
`http://starship:8801/terrain/captured-water-paired-20261011-JK0Nmf/`.
Final 1080p render-only measurements on Starship's RTX 5070 Ti: whole-scene GPU
p95 up to 3.78 ms and CPU service up to 0.32 ms. The final lake repeat adds
2.034 ms of water cost, narrowly over the proposed 2.000 ms gate. This measured
exception was accepted for opt-in V1; the original strict-gate failure is retained.
Manual visual acceptance and seamless backdrop stitching remain deferred.

The headless report helper runs capture/performance/report phases separately:
`python3 projects/terrain/review_captured_water_v1.py OUTPUT --phase PHASE`.
It freezes binary/shader/input identity across phases and never launches a solver.
`--accept-measured-water-cost` is report-only and records an explicitly authorized
exception without rewriting the automated gate. Its pure report checks run with
`python3 projects/terrain/review_captured_water_v1_tests.py`.

## Product Contract

### Wider rain-fed captured backdrop (opt-in study)

The 2026-10-12 wider-water study reuses the same unmodified Mountain source and
external solver. Its matching 957-square / 64 m pilot and 1913-square / 32 m
candidate cover 61.184 km square; dimensions fit the existing elevation/climate
bounds instead of forcing an out-of-bounds 2k / 32 m grid. Both start dry under
continuous 512 mm/h visual-demo rain and retain sparse snapshots through twenty
physical hours. This is not ordinary weather, equilibrium or a permanent-water
classification. The larger catchment, resolution and duration can change water
levels; the original 16 m capture is kept unchanged, not spliced into the result.

The locally retained wider candidate can be consumed without any solver:

```sh
build/release/projects/terrain/terrain \
  --terrain-heightfield cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json \
  --terrain-water-capture cache/terrain/water/v1/mountain-wide-rain-fed-1913-32m-study/water-capture.json \
  --terrain-water-capture-camera landscape --terrain-water-detail fixed \
  --terrain-water-focus-x 11520 --terrain-water-focus-z 19200 \
  --terrain-water-focus-height 1342.083496 --terrain-water-span 11457.6
```

The fixed topology is built/uploaded once: paired ground, water and ground shadow
share field coordinates and indices. A 16 m fine region transitions to 32 / 64 m
geometry; aggressive 128–1024 m exterior steps were rejected after they distorted
distant wet silhouettes. The source/state remains unchanged, and a denser mesh
does not invent extra simulated detail. Existing materials, flow and whitewater
are shared; bank noise and numerical/render defaults do not change.

`--terrain-water-detail uniform` retains the old uniform 2x path (default).
`uniform-control` retains that geometry with the same fixed-region 4096 shadow
fit as `fixed`, for a fair topology comparison. `--terrain-water-detail-span`
sets the build-time fine-region size. `--terrain-water-detail-focus-x/z` pins it
independently of camera translations. Explicit `--terrain-water-focus-x/z`,
`--terrain-water-focus-height`, `--terrain-water-span`, `--terrain-water-yaw`
and `--terrain-water-pitch` override preset framing without moving the asset.
These are bounded scene controls, not a camera-following LOD/streaming system.

Wider water removes the old inner hydraulic cutoff but cannot remove the finite
outer source edge. Several headings in the full original-site orbit expose that
edge; the full orbit envelope is not visually qualified. Far shadow receivers
and close-up shores outside the fine region also remain unqualified. No masking
pretends the outside world is dry, and no default promotion or manual GUI
acceptance is implied. The normal Backdrop is still a separate unchanged product;
this is a wider optional captured surface, not an overlapping radial water layer.

Report: `http://starship:8801/terrain/wider-water-20261012-a1/`.
Both twenty-hour bakes completed within their bounds: 4.08 minutes wall for
64 m and 28.10 minutes for 32 m. The fixed 32 m consumer submits 3,901,302
triangles per surface pass versus 29,245,952 uniformly at 2x. Eight 1080p
render-only comparisons (two each for Landscape, Stream, original Lake location
and a new large-basin stress view) pass the proposed scene GPU <=8 ms / CPU
service <=4 ms gates: GPU p95 at most 3.292 ms, CPU service at most 0.317 ms.
Matched uniform Landscape controls measure 8.560 ms; incremental water costs
are recorded separately, not equated to attribution or monitor FPS. Reported
device-local heap usage peaks at 653 MiB. Twelve CPU/GPU/regression checks and
seven portable helper tests pass; original Terrain/Fluid PNG references stay
exact. The solver is stopped during consumer timings; these are not wider-live
streaming or native GUI performance measurements.

`study_wider_water_v1.py freeze|prepare|run --out OUTPUT --candidate CANDIDATE`
is an explicit offline tool using the existing optional environment and pinned
extension. Its roots are fresh owned leaves and each solver run has a 30-minute
wall bound. `review_wider_water_v1.py capture|performance|basin|report OUTPUT`
never starts a solver or GUI, preserves frozen input/runtime hashes and uses
genuine offscreen Vulkan. Generated bundles remain ignored local cache assets.
Capture/performance phases use standard Python; basin/report analysis uses the
existing optional NumPy/Matplotlib environment, without importing or starting
the solver. The installed interpreter path is recorded in the study protocol.

### World-first prepared sites (opt-in)

The finer display atlas and shared material readability follow-up are documented
in [Shared terrain readability V1](../../docs/terrain-readability-v1.md), with
matched controls at `http://starship:8801/terrain/readability-20261012-a1/`.
The reviewed footprint-filtered material is now the default for prepared sites,
captured ground and the established radial backdrop. The explicit equivalent
selectors are `--terrain-readability refined` and
`--terrain-surface-detail readable-detail`. Previous treatments remain available
as `--terrain-readability current` and `--terrain-surface-detail filtered-detail`.
Fluid's shared Macro preset also defaults to the new ground, with its validated
source-world material origin. Older Fluid material profiles remain references.
Promotion checks: `http://starship:8801/terrain/readability-defaults-20261012-a1/`.
The atlas is display-only; wider terrain still does not imply wider water.

The world/site study separates broad Terrain Diffusion coverage from saved water.
It uses one pinned seed/settings/coordinate mapping, a 262.144 km coarse survey,
and 16.384 / 32.768 / 65.536 / 131.072 km requested site boxes. Site centers are
admitted only if the complete outer backdrop plus 1 km camera movement and
480 m sampling margins fits the world. The coarse conditioning map is a survey,
not final rendered terrain. This is a bounded fixed/orbit foundation, not a
free-flight or infinite-streaming system.

With the existing landscape assets/tooling installed, explicitly prepare:

```sh
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase freeze
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase spike
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase survey
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase site-0
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase site-1
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase site-2
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase validate
python3 projects/terrain/study_terrain_world_v1.py outputs/terrain/world-sites-YYYYMMDD --phase publish
```

Each optional producer process has a 900 s wall limit; no downloads or installs
are attempted. Complete validated site packages are reused without starting the
producer. Cache publication refuses to overwrite an existing world package.
All generated data stays outside Git; normal build/test does not generate it.
The initial validated package is under
`cache/terrain/worlds/v1/` (see the report's `published.json` for the exact leaf).

Inspect a package with `--terrain-site PATH/site.json`. This prepares a sampled
terrain-only surface in memory: zero water, no fabricated hydraulic recording.
It reuses shared surface shading, filtering, fixed detail, exterior rings and
core-focused shadows. The CLI owns its source selection and resets on startup;
there is no new live GUI site picker. Far-context shadows remain unqualified.

For existing captured water, retain its original matching heightfield and add
`--terrain-water-context --terrain-water-context-source PATH/heightfield.json`.
The wider source must match producer, seed, height transform, coordinate mapping,
and every native anchor sample. The original capture hash/rounded-bed check
remains unchanged; raw water fields are not replaced. Wider ground is not wider
water coverage.

The low-frequency-only export was rejected: its p95 difference from filtered
native terrain was about 216 m. V1 instead generates native tiles for selected
backdrops, then filters them. Runtime currently retains that native raster and
its filtered mip chain; compact exported tier views are not yet sparse-resident
runtime data. The initial three backdrops each took about 99–101 s including
producer setup, with exact native tile/source/site overlaps on this machine.
Dependency/model installation and cross-hardware reproducibility are not included.

Review: `http://starship:8801/terrain/world-sites-20261012-a1/`.
All 17 selected CPU/GPU/regression tests passed; three legacy views and a
published-cache reload remained pixel-exact. Render-only 1080p GPU p95 was
0.69–0.73 ms for terrain-only sites and 3.50–3.56 ms for the expanded-ground
water scene (previous coverage: 3.48–3.52 ms); CPU service p95 was below 0.31 ms.
These are headless measurements, not native GUI/frame-delivery qualification.
Review commands use `review_terrain_world_v1.py OUTPUT --phase capture|performance|report`;
the report/maps phase needs the existing optional NumPy/Matplotlib environment.
No solver or native GUI is launched by the review tools. No default is promoted.

### Captured-water landscape context (opt-in study)

Add `--terrain-water-context --terrain-water-capture-camera landscape` to the
captured-water invocation for wider ground from the same source. The core's
saved fields and B-spline ground/water stay unchanged. Static rectangular rings
join its exact 2x perimeter, with an exterior-only 128 m reconstruction collar
and progressively coarser source sampling. There is no overlapping radial ground.

The UI can toggle the context without changing hydraulic state. `landscape`
provides a supported scenic framing; `boundary` deliberately exposes a wet edge.
`--terrain-water-ground-view normal|clay|ownership` provides ground diagnostics
that hide water. Ownership shows captured ground in orange and context in blue.

This does **not** compute water in the surroundings: streams can visibly end at
the saved rectangle, especially in the boundary view. The outer source is also
finite and asymmetric around this capture. Do not describe a broad overview as
a seamless landscape-wide hydrology product. Ground dampness fades as a visual
weight over 64 m outside the core, not as invented water depth or persistent soil
moisture. Clouds remain disabled.

Both ground products render before water into shared opaque color/depth using
the accepted material/lighting code and one HDR transform. Context-on uses a
4096 shadow with bounded up-sun caster coverage for the core; far-context shadow
receivers remain unqualified. It does not reduce the core shadow to fit the
entire 61.41 km terrain. The original no-context path stays the default.

Report helper:
`python3 projects/terrain/review_captured_water_context_v1.py OUTPUT --phase capture|performance|report`.
No solver, terrain inference or native GUI is launched by this study.

The first context report is
`http://starship:8801/terrain/captured-water-context-20261012-aAoOLi/`.
Ten foundation/Fluid tests pass; all 8,184 CPU/GPU edge comparisons are within
0.49 mm. The retained context-off and Fluid captures remain exact. Context-on
whole-scene GPU p95 is at most 4.02 ms at 1080p on Starship, with 488 MiB of
whole-app device-local usage. The boundary view still reveals water cutoffs:
context is opt-in, not seamless full-landscape coverage or a promoted default.

### Shared surface-field study (2026-10-08)

Climate loading and optional surface math now belong to `cubey::asset` and
`cubey::terrain`; project headers retain compatibility aliases. Production
defaults are unchanged. `--terrain-surface-model correlated-surface` and the
GUI's **Source-aligned masks (study)** selection use a full-source descriptor
shared with Fluid 2.5D. It is cached with the render product and independent of
consumer crop, height offset and draw-mesh spacing. The old three models keep
their historical semantics. The companion is optional for the new descriptor;
without it, bounded landform-only potentials are used.

The new bare-substrate material recipe did not pass visual review and was
removed. This option retains the data/diagnostic experiment, **not** that recipe
or a claim of improved surface fidelity. See the surface boundary in
`docs/architecture/terrain-v1.md` and
`outputs/terrain/correlated-surface-v1-20261008-b2` for the comparison evidence.

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

The rejected daylight-form study records a marginal mineral/ambient tuning pass
and an uneconomical source-normal texture experiment:
[`docs/notes/terrain-daylight-form-v1.md`](../../docs/notes/terrain-daylight-form-v1.md).
The resulting source-versus-rendering diagnosis, effort boundary, and stop
conditions are recorded in
[`docs/notes/terrain-visual-fidelity-strategy.md`](../../docs/notes/terrain-visual-fidelity-strategy.md).

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

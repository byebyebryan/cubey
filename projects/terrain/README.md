# Terrain

Cubey's fixed-focus terrain/backdrop demo and saved-water landscape viewer.
Shared foundation components supply source-bound heightfields, terrain material,
fixed-focus detail/LOD, shadows and optional captured water. It is not a general
traversable terrain engine or a hydrology simulator.

The [terrain showcase](https://dev.byebyebryan.com/cubey/demos/terrain/#terrain-rendering)
owns the visual overview; this guide covers native setup and contracts.

[Terrain Water](../terrain_water/README.md) owns live SynxFlow and recorded flow
experiments. Terrain consumes prepared snapshots without a solver. The
[glTF viewer](../gltf_viewer/README.md#showcase-highlight) is another accepted
consumer of the shared saved-water foundation; Water 3D/Pyro adoption is deferred.

## Build and run

```sh
rtk proxy cmake --preset release
rtk proxy cmake --build --preset release --target cubey_project_terrain -j 6
rtk proxy build/release/projects/terrain/terrain
```

No-argument startup retains the canonical legacy backdrop and requires its
prepared source. It does not silently select a wet landscape or generate assets.
For an existing prepared catalogue, select a landscape explicitly:

```sh
rtk proxy build/release/projects/terrain/terrain --terrain-landscape mountain-local
rtk proxy build/release/projects/terrain/terrain --terrain-landscape mountain-dry
```

Other named saved-water options are `mountain-wide`, `mountain-interior-wide`
and `valley-interior-wide`. Explicit `auto` selects an available prepared
landscape; it is not the no-argument default. Missing assets remain unavailable,
not a reason to launch a solver or invent substitute terrain.

Add `--headless --frames 1 --output outputs/terrain/FRESH.png` for a capture.
Add `--terrain-water-audit` for the immutable GPU-field check outside profiling.

## Product contract

- Normal backdrop: external raster, deterministic placement, cached mesh,
  atmosphere and existing cloud controls.
- Saved landscape: checked source/capture pair, paired B-spline ground/water,
  shared materials and flow cues, fixed-focus detail/LOD. No running simulation.
- Apply landscape: prepare/validate asynchronously, upload through the GPU owner,
  reset the camera, retain the old scene on failure. One replacement at a time.
- Water animation changes surface appearance, not the captured depth/velocity.
  Hiding water retains wet-ground shading; a terrain-only preset loads no capture.
- Elevated saved-water views omit surface-model clouds. Shared clear-sky/day-night
  is optional and render-only. No generic underwater/transparency composition.

`mountain-local` has a nominal 65.536 km water patch at 2048² / 32 m plus wider
terrain. The three wide presets have independent 2048² / 64 m storm states over
nominal 131.072 km. They are not nested water LOD or a continuous hydrological
world. Terrain owns this wider backdrop; Terrain Water shows its full-res patch only.

## Generate the asset

Preparation is explicit and can require downloaded Terrain Diffusion code/models
and an isolated environment. It never runs during ordinary build/startup.

```sh
rtk proxy cmake --build --preset release --target cubey_terrain_generate_default_asset
rtk proxy python3 projects/terrain/tools/prepare_terrain_landscapes.py --help
```

Current tools live in [tools](tools/README.md); source and landscape recipes are
[terrain_source_presets.json](terrain_source_presets.json) and
[terrain_landscape_presets.json](terrain_landscape_presets.json). Generated data
belongs under ignored `cache/terrain`; captures/reports belong under `outputs/`.
Fresh-checkout availability and byte-identical model regeneration are not promised.

## World-first prepared sites (opt-in)

Prepared worlds provide coarse outer coverage and an interior high-resolution
site. Admission checks coverage and source identity. Fixed-focus LOD is intended
for orbit cameras or bounded movement, not a wide top-down map or arbitrary traversal.
See [world/site tooling](tools/README.md) and the
[landscape study index](../../studies/terrain/landscapes/README.md) for the
coverage, wider-water and qualification records. Their measured limits remain retained.

## Tests and history

Run relevant configured CTest groups with `ctest --test-dir build/release -R terrain_`;
headless GPU checks and native-window acceptance remain separate.
Current shared contracts are documented in [Terrain v1 architecture](../../docs/architecture/terrain-v1.md).
Dated capture, calibration and landscape experiments are indexed under
[studies/terrain/landscapes](../../studies/terrain/landscapes/README.md), not mixed
with application source. Old studies do not automatically become current defaults.

<details>
<summary>Earlier README section links</summary>

- <a id="animated-rain-fed-capture-opt-in"></a>[Animated rain-fed capture (opt-in)](../../studies/terrain/landscapes/development_history.md#animated-rain-fed-capture-opt-in)
- <a id="wider-rain-fed-captured-backdrop-opt-in-study"></a>[Wider rain-fed captured backdrop (opt-in study)](../../studies/terrain/landscapes/development_history.md#wider-rain-fed-captured-backdrop-opt-in-study)
- <a id="prepared-world-wider-saved-water-opt-in"></a>[Prepared-world wider saved water (opt-in)](../../studies/terrain/landscapes/development_history.md#prepared-world-wider-saved-water-opt-in)
- <a id="named-prepared-landscapes-and-applyreset"></a>[Named prepared landscapes and Apply/reset](../../studies/terrain/landscapes/development_history.md#named-prepared-landscapes-and-applyreset)
- <a id="prepared-landscape-first-use-promotion-ready"></a>[Prepared-landscape first use (promotion-ready)](../../studies/terrain/landscapes/development_history.md#prepared-landscape-first-use-promotion-ready)
- <a id="wide-view-rendering-study-opt-in-lighting-trial"></a>[Wide-view rendering study (opt-in lighting trial)](../../studies/terrain/landscapes/development_history.md#wide-view-rendering-study-opt-in-lighting-trial)
- <a id="captured-water-landscape-context-opt-in-study"></a>[Captured-water landscape context (opt-in study)](../../studies/terrain/landscapes/development_history.md#captured-water-landscape-context-opt-in-study)
- <a id="shared-surface-field-study-2026-10-08"></a>[Shared surface-field study (2026-10-08)](../../studies/terrain/landscapes/development_history.md#shared-surface-field-study-2026-10-08)
- <a id="review"></a>[Review](../../studies/terrain/landscapes/development_history.md#review)
- <a id="studies-and-boundaries"></a>[Studies And Boundaries](../../studies/terrain/landscapes/development_history.md#studies-and-boundaries)

</details>

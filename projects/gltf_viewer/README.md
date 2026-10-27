# glTF Viewer

`gltf_viewer` imports glTF/GLB assets into Cubey's PBR renderer. It supports
generated or HDR-backed image-based lighting, animation, the shared atmosphere
and cloud environment, and optional Terrain V1 or saved-water backdrops. The
current feature-led visual overview is the
[glTF page](https://dev.byebyebryan.com/cubey/demos/gltf/#gltf-motion); this
guide owns the native importer, renderer and run contracts.

Status: the single-asset viewer milestone is closed. It covers staged CPU
preparation, bounded incremental GPU residency, atomic scene activation,
animation/deformation and the material extensions represented by Cubey's
current contract. See [glTF assets and PBR](../../docs/architecture/gltf-assets.md)
for supported semantics and the [V1 closure record](../../docs/notes/gltf-viewer-v1-closure.md)
for its source-pinned tests and motion review.

## Loading behavior

Windowed runs resolve and probe asset metadata first, then publish a
wireframe-style loading cage at authored scene bounds. The probe reads glTF
JSON (or the JSON chunk of GLB) before staged preparation opens external
buffers or the embedded BIN payload. A CPU worker prepares the asset, the GPU
owner creates resident resources, and the complete scene activates atomically
at a frame boundary. Probe failure uses a neutral unit cage and follows the
staged error path; with no input, a generated solid cube is the fallback. The
previous scene remains visible if loading fails. Headless capture follows the
same staged path and waits for completion before frame zero.

## Run

```sh
./build/dev/projects/gltf_viewer/gltf_viewer --input path/to/asset.glb
./build/dev/projects/gltf_viewer/gltf_viewer --headless --input path/to/asset.glb \
  --frames 120 --width 1280 --height 720 --output /tmp/cubey-gltf.png
```

Useful options include `--animation-index`, `--animation-speed`,
`--pause-animation`, `--pbr-environment-source static|atmosphere`,
`--capture-camera-distance-scale`, `--capture-video-orbit-degrees`,
`--capture-camera-fov-y-degrees`, `--pbr-tonemap linear|aces`,
`--terrain-heightfield`, `--terrain-surface-detail`, and `--terrain-shadows`.

The viewer preserves its established 60-degree vertical camera FOV and ACES
display transform unless the capture-only FOV override or `--pbr-tonemap` is
selected. `--pbr-tonemap linear` uses the existing linear, clamped final display
path for controlled diagnostics; it is not a general presentation policy.

`--pbr-environment-source atmosphere` is the default. It selects Cubey's
procedural sky, atmosphere SH diffuse lighting, atmosphere/cloud reflections,
procedural direct light and automatic exposure. `static` selects a coherent
generated-or-HDR IBL-only world: its visible background and diffuse/specular
lighting use that same environment, with no procedural direct light, legacy
ambient fill, atmosphere time updates, clouds or atmosphere controls. Terrain
and ocean backdrops remain atmosphere-only.

## Validation and boundary

The normal development preset covers the viewer's project-local config,
loading, renderer routing, fallback, environment, backdrop and capture paths.
The opt-in conformance preset uses the pinned Khronos Sample Assets checkout
for loader, material, animation/deformation and rendered semantic evidence:

```sh
cmake --preset dev-gltf-conformance
cmake --build --preset dev-gltf-conformance
ctest --preset dev-gltf-conformance
```

This remains a single-asset viewer, not a general scene streamer or DCC
pipeline. Reopen the asset path for a concrete need in broader compatibility,
lower total decode latency or concurrent multi-asset streaming. Exact back-face
refraction, multiple internal bounces, generalized punctual-light transmission,
transparent shadows and order-independent transparency are separate renderer
projects rather than incomplete viewer plumbing.

## Showcase highlight

The optional saved-water landscape backdrop is selected with
`--terrain-landscape mountain-local --no-clouds`. It requires a local glTF asset
and an existing prepared Terrain cache; it loads saved fields and does not
generate terrain, install SynxFlow or launch a solver. Use
`--terrain-landscape-cache-root` to select another prepared cache.

This V1 composition requires opaque/masked assets, the atmosphere environment
and final ACES output. It rejects clouds, blended/transmission
materials, `--ocean-backdrop`, `--terrain-heightfield`,
`--terrain-surface-detail`, `--terrain-render-stride`, `--terrain-shadows`,
non-final debug views and non-ACES output.
`--terrain-landscape-depth fitted` is the default;
`legacy` retains the older 8 m near-plane comparison. The optional
`--terrain-landscape-near METRES` override is geometry-checked and applies only
to fitted depth. `--terrain-landscape-shadow-strength` is a 0..1 shadow
comparison control. `--no-terrain-water-visible` hides the surface while
retaining saved fields and wet-ground shading; `--terrain-water-audit` checks
those fields at shutdown, outside profiling.

Treat this as a small-orbit backdrop for a model above the saved surface, not a
ground-level or underwater camera contract. The foreground uses shared sky
lighting and does not reflect the retained terrain or water. The detailed
[saved-water study](../../studies/gltf_viewer/water_landscape_v1.md) records the
limits and depth work; the [landscape history](docs/landscape-history.md) retains
exact commands and archive references. The old Terrain-plus-saved-water
composite remains historical evidence, not the current viewer showcase.

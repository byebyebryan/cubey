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

From the repository root, build the viewer and download the single-file Fox
sample at the same Khronos revision used by Cubey's conformance preset. The
launch selects the animated Walk clip.

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_project_gltf_viewer
mkdir -p cache/gltf-viewer
curl --fail --location --output cache/gltf-viewer/Fox.glb \
  https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf/Models/Fox/glTF-Binary/Fox.glb
./build/dev/projects/gltf_viewer/gltf_viewer --input cache/gltf-viewer/Fox.glb --animation-index 1
```

See the [Fox source and credits](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf/Models/Fox).
To capture the same sample or inspect your own asset, supply its path with
`--input`:

```sh
./build/dev/projects/gltf_viewer/gltf_viewer --headless \
  --input cache/gltf-viewer/Fox.glb --animation-index 1 \
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

## Native temporal anti-aliasing

Windowed `--anti-aliasing auto` requests native TAA for opaque, rigid draws
with static IBL or a settled clear atmosphere. This command selects static IBL.

```sh
./build/dev/projects/gltf_viewer/gltf_viewer --input path/to/opaque.glb \
  --pbr-environment-source static
```

The launcher reports the requested mode, effective mode and bypass reason on the
first submitted frame and whenever admission changes. Masked, blended,
transmissive, instanced and deformed draws bypass TAA, as do diagnostics,
animated lighting, clouds, terrain, ocean and the saved landscape’s linear
foreground export. Camera orbit and rigid transforms use the shared renderer’s
motion field. Asset activation, camera home and animation loop boundaries
restart history.

Headless Auto preserves the existing capture pixels. Pass `--anti-aliasing taa`
to request TAA explicitly. A PNG captures the first jittered sample. A
multi-frame render or video accumulates history across submissions. Profiles
record `aa.requested` (resolved producer request), `aa.effective` and
`aa.bypass_reason` on submitted frames. Admitted frames also record
`taa.history_age`, `taa.reset_reason` and pixel jitter in `taa.jitter_x/y`.

## DLSS Quality on native Linux

The optional Viewer path builds against the developer-supplied
[NVIDIA DLSS v310.9.16 SDK](https://github.com/NVIDIA/DLSS/releases/tag/v310.9.16)
on Linux x86-64. Set `CUBEY_DLSS_SDK_DIR` to the SDK root. The launcher reads
the feature library from that exact root at runtime.

```sh
cmake --preset release -B build/dlss -DCUBEY_DLSS_SDK_DIR=/path/to/dlss-sdk
cmake --build build/dlss --target cubey_project_gltf_viewer
build/dlss/projects/gltf_viewer/gltf_viewer --input path/to/opaque.glb \
  --pbr-environment-source static --upscaler dlss-quality
```

Native rendering with AA Auto is the default. The same static-IBL launch
without `--upscaler dlss-quality` uses native TAA when the interactive view
is eligible. DLSS is selected at launch.

DLSS Quality requires a supported NVIDIA adapter, static IBL, opaque rigid
geometry, final PBR, and `--anti-aliasing auto`. Explicit selection reports
an error when those requirements fail. The display extent comes from the
window or capture. NGX supplies the lower render extent. Headless video and
render-only runs can select the same mode. Profiles record the two extents,
history age and reset reason under `dlss`. Use `--upscaler native` for the
ordinary Off and TAA policy. An empty SDK path produces the SDK-free build.

DLSS Quality defaults to model preset K. Choose an experimental model with
`--dlss-preset m` or `--dlss-preset l`. The same values are available under
`gltf.dlss_preset` in a config file. The model option applies only when DLSS
Quality is selected, and all three models use the Quality render extent.
Startup logs report the requested model. M and L produced frames in the
native Vulkan trial and also triggered image-layout validation errors inside
NGX v310.9.16. Both cost more GPU time than K on the tested RTX 5070 Ti.

See the [native DLSS qualification results](../../docs/notes/dlss-super-resolution-2026-11.md).

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

# glTF Viewer

`gltf_viewer` loads glTF/GLB assets into Cubey's PBR renderer. It supports
generated or HDR-backed image-based lighting, animation, the shared procedural
atmosphere/cloud environment, and an optional Terrain V1 backdrop.

Status: the first single-asset viewer milestone is closed. It covers staged CPU
preparation, bounded incremental GPU residency, atomic scene activation, core
animation/deformation, and every material extension represented by Cubey's
current material contract. The exact supported-extension semantics live in
[glTF assets and PBR](../../docs/architecture/gltf-assets.md); the final test and
motion-review snapshot is recorded in the
[glTF Viewer V1 closure](../../docs/notes/gltf-viewer-v1-closure.md).

## Loading behavior

Windowed runs resolve and probe the requested asset's metadata first, then
publish a wireframe-style indexed loading cage at its authored scene bounds.
The probe reads only textual glTF JSON or the JSON chunk of a GLB; external
buffers and embedded BIN payloads remain unopened until staged preparation.
The full asset is prepared on a CPU worker, its resident resources are created
on the GPU owner, and the complete scene is atomically activated at a frame
boundary. A probe failure uses a neutral unit cage and follows the normal
staged error path; with no input, the generated solid cube remains the demo
fallback. The previous scene remains visible if loading fails. Headless capture
uses the same staged path but waits for it to finish before frame zero.

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
explicitly selected. `--pbr-tonemap linear` uses the existing linear, clamped
final display path; it is intended for controlled diagnostics, not a new
general presentation policy.

`--pbr-environment-source atmosphere` is the default and selects Cubey's
procedural sky, atmosphere SH diffuse lighting, atmosphere/cloud reflections,
procedural direct light, and automatic exposure. `static` selects a coherent
generated-or-HDR IBL-only world: its visible background is the IBL skybox,
diffuse and specular lighting (including transmission fallback) use that same
environment, and it has no procedural direct light, legacy ambient fill,
atmosphere time updates, clouds, or atmosphere controls. Terrain and ocean
backdrops remain atmosphere-only.

## Validation and boundary

The normal development preset covers the viewer's project-local config,
loading, renderer-routing, fallback, environment, backdrop, and capture paths.
The opt-in conformance preset additionally uses the pinned Khronos Sample Assets
checkout for loader, material, animation/deformation, and rendered semantic
evidence:

```sh
cmake --preset dev-gltf-conformance
cmake --build --preset dev-gltf-conformance
ctest --preset dev-gltf-conformance
```

The closed milestone deliberately remains a single-asset viewer rather than a
general scene streamer or DCC pipeline. Reopen the asset path when a product
demonstrates a concrete need for broader compatibility, lower total decode
latency, or concurrent multi-asset streaming. Exact back-face refraction,
multiple internal bounces, generalized punctual-light transmission,
transparent shadows, and order-independent transparency are separate renderer
projects rather than incomplete viewer plumbing.

## Showcase highlight

The existing published highlight below remains unchanged. An opt-in
[saved-water landscape audition](water_landscape_v1.md) reuses the shared
Terrain renderer and prepared captures; it is not a live Fluid simulation:

```sh
rtk proxy build/release/projects/gltf_viewer/gltf_viewer --terrain-landscape mountain-local --no-clouds --input outputs/showcase/audition-2/assets/DamagedHelmet.glb --time-of-day-mode solar --time-hours 10 --time-speed-hours-per-second 0 --capture-camera-distance-scale .70 --capture-camera-yaw 94.538 --capture-camera-pitch -6.8755 --exposure 0
```

This requires the existing prepared Terrain cache and a local asset. It never
downloads, generates terrain, installs SynxFlow, or launches a solver. Named
landscapes fail clearly if missing; `--terrain-landscape-cache-root` supports an
alternate prepared cache. `--no-terrain-water-visible` hides the surface while
retaining wet-ground shading. `--terrain-water-audit` checks immutable fields at
shutdown, outside profiling. Native GUI/layout acceptance remains deferred.

V1 is a fixed/small-orbit backdrop for **opaque/masked assets and clear-sky
daylight**, not a general scene compositor. Clouds, alpha/transmission, ocean,
legacy terrain controls, debug views and non-ACES display are rejected in this
mode; their ordinary viewer paths remain available without this opt-in.
The model stays at normal scale above the highest saved surface. A separate
8m landscape near plane preserves distant water depth precision; the foreground
depth is remapped before water shading. Do not use this recipe as a ground-level
or underwater camera. The foreground uses shared sky lighting, not reflections
of the retained terrain/water. A future gallery replacement still needs review.

[![glTF + Terrain showcase poster](../../docs/media/showcase/gltf-terrain.png)](../../docs/media/showcase/gltf-terrain.mp4)

The highlight stages the Damaged Helmet against Cubey's canonical Terrain V1
backdrop. A slow partial orbit and afternoon-to-night lighting change reveal the
imported material response first, then let the emissive details take over.

The exact capture uses full broken-cumulus clouds, atmosphere lighting,
scene-distance scale `0.70`, exposure `1.0`, IBL intensity `1.0`, and a
30-degree eased arc over 480 frames at 60 FPS:

```sh
./build/dev/projects/gltf_viewer/gltf_viewer --headless --capture video --frames 480 --fps 60 --width 1280 --height 720 --input outputs/showcase/audition-2/assets/DamagedHelmet.glb --pbr-environment-source atmosphere --time-of-day-mode solar --time-hours 14.0 --time-speed-hours-per-second 0.875 --clouds --cloud-weather-preset broken-cumulus --cloud-quality full --terrain-heightfield cache/terrain/sources/v1/default --terrain-surface-detail filtered-detail --terrain-shadows --terrain-foreground-height 200 --capture-camera-distance-scale 0.70 --capture-video-orbit-degrees 30 --exposure 1.0 --ibl-intensity 1.0 --output outputs/showcase/audition-2/gltf/gltf-damaged-helmet-terrain-30deg-60fps-source.mp4
```

The media-only license boundary is important: the MP4 and poster are
`CC-BY-NC-4.0`, attributed to ctxwing (2018 rebuild/conversion) and
theblueturtle_ (2016 earlier model), with the
[upstream Damaged Helmet source](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet).
This exception does not relicense Cubey source and the glTF media is not
permissively reusable. See the [showcase media manifest](../../docs/media/showcase/manifest.json)
for the exact publication hash and full provenance.

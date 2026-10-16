# glTF Viewer

`gltf_viewer` loads glTF/GLB assets into Cubey's PBR renderer. It supports
generated or HDR-backed image-based lighting, animation, the shared procedural
atmosphere/cloud environment, and optional Terrain V1 or saved-water landscape backdrops.

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

Saved landscapes reuse the Terrain foundation also used by
[Terrain Water](../terrain_water/README.md); this viewer runs no hydraulic solver.

The owner-selected **A: original mountain / saved water / face-on solar arc** is
the current repository highlight. Its [audition and depth-precision study](water_landscape_v1.md)
reuse the shared Terrain renderer and prepared captures; this is not a live
Fluid simulation. The landscape stays explicitly opt-in at runtime. This fixed
daylight diagnostic is useful for inspection; the timed film recipe is below:

```sh
rtk proxy build/release/projects/gltf_viewer/gltf_viewer --terrain-landscape mountain-local --no-clouds --input outputs/showcase/audition-2/assets/DamagedHelmet.glb --time-of-day-mode solar --time-hours 10 --time-speed-hours-per-second 0 --capture-camera-distance-scale .70 --capture-camera-yaw 94.538 --capture-camera-pitch -6.8755 --exposure 0
```

This requires the existing prepared Terrain cache and a local asset. It never
downloads, generates terrain, installs SynxFlow, or launches a solver. Named
landscapes fail clearly if missing; `--terrain-landscape-cache-root` supports an
alternate prepared cache. `--no-terrain-water-visible` hides the surface while
retaining wet-ground shading. `--terrain-water-audit` checks immutable fields at
shutdown, outside profiling. Native GUI/layout acceptance remains deferred.

V1 is a fixed/small-orbit backdrop for **opaque/masked assets and clear sky**,
not a general scene compositor. Daylight and the reviewed dusk/early-night film
are qualified; general night-sky quality is not. Clouds, alpha/transmission, ocean,
legacy terrain controls, debug views and non-ACES display are rejected in this
mode; their ordinary viewer paths remain available without this opt-in.
The model stays at normal scale above the highest saved surface. A separate
geometry-fitted landscape depth range preserves distant water depth precision;
the foreground has its own asset-scale range and is remapped before water shading.
`--terrain-landscape-depth legacy` retains the old 8m-near comparison.
`--terrain-landscape-near METRES` provides a geometry-checked fitted-mode study
override; `--terrain-landscape-shadow-strength 0` isolates sun shadows.
Do not use this recipe as a ground-level
or underwater camera. The foreground uses shared sky lighting, not reflections
of the retained terrain/water. Fitted depth is accepted for this bounded use;
native Cubey GUI/layout acceptance remains separate from the reviewed film.

https://github.com/user-attachments/assets/961c41fe-1bdf-4c0d-a936-1c7fad251f2f

[Committed MP4](../../docs/media/showcase/gltf-terrain.mp4) · [Poster fallback](../../docs/media/showcase/gltf-terrain.png)

The eight-second highlight starts facing the Damaged Helmet, then eases through
a 45° arc as afternoon becomes sunset and early night. The lake opens up on the
left as the camera turns. It uses a held 12-hour saved water
state from an exaggerated 512mm/h storm; it does not show lake formation,
calibrated weather, permanent water, or a live solver. Camera and surface cues
animate alongside the shared solar lighting. The normal no-landscape viewer remains unchanged.

The [native 1080p master](../../docs/media/showcase/gltf-terrain-master-1080p.mp4)
and [complete recipe](../../docs/media/showcase/gltf-terrain-recipe.json) retain
the clean source revision, renderer/input hashes, exact command and validation.
An independent repeat matches all 480 decoded frames of the approved source film.
The 720p/60 gallery copy is a Lanczos derivative, not a native 720p capture.
Its [publication receipt](../../docs/media/showcase/gltf-terrain-publication.json)
records the exact-byte anonymous download verification and
[provenance comment](https://github.com/byebyebryan/cubey/issues/1#issuecomment-6205885688).

The exact native capture uses clear-sky shared lighting, a 14:00→19:30 solar
clock, distance scale `0.70`, existing automatic exposure, fitted independent
foreground/background depth ranges, and 480 rendered frames at 60 FPS. Point
stars are off for this capture only to avoid low-resolution sky-cubemap blocks:

```sh
rtk proxy build/release/projects/gltf_viewer/gltf_viewer --headless --capture video --frames 480 --fps 60 --width 1920 --height 1080 --input outputs/showcase/audition-2/assets/DamagedHelmet.glb --terrain-landscape mountain-local --terrain-landscape-depth fitted --no-clouds --time-of-day-mode solar --time-hours 14 --time-speed-hours-per-second .6875 --capture-camera-distance-scale .70 --capture-camera-yaw 0 --capture-camera-pitch -6.8755 --capture-video-orbit-degrees 45 --star-intensity 0 --ibl-intensity 1 --require-validation --terrain-water-audit --output outputs/showcase/gltf-landscape-A-solar-source.mp4
```

The previous dry-terrain/day-to-night cut remains in Git history and at its
[original attachment](https://github.com/user-attachments/assets/a6119257-6810-440d-b910-e247274fbd02).
Its [legacy recipe](../../docs/media/showcase/gltf-terrain-legacy-v1-recipe.json)
is historical metadata, not hashes of the current files.

The [front-facing day-to-night revision](showcase_solar_v1.md) is owner-approved
and promoted. The fixed-morning saved-water cut remains recoverable from its
[historical recipe](../../docs/media/showcase/gltf-terrain-morning-v1-recipe.json),
[publication receipt](../../docs/media/showcase/gltf-terrain-morning-v1-publication.json),
and unchanged attachment. These describe previous bytes, not the current files.

The media-only license boundary is important: both MP4s and the poster are
`CC-BY-NC-4.0`, attributed to ctxwing (2018 rebuild/conversion) and
theblueturtle_ (2016 earlier model), with the
[upstream Damaged Helmet source](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet).
This exception does not relicense Cubey source and the glTF media is not
permissively reusable. See the [showcase media manifest](../../docs/media/showcase/manifest.json)
for the exact publication hash and full provenance.

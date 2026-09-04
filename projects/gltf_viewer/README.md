# glTF Viewer

`gltf_viewer` loads glTF/GLB assets into Cubey's PBR renderer. It supports
generated or HDR-backed image-based lighting, animation, the shared procedural
atmosphere/cloud environment, and an optional Terrain V1 backdrop.

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
`--terrain-heightfield`, `--terrain-surface-detail`, and `--terrain-shadows`.

## Showcase highlight

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

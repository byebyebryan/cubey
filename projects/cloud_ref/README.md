# Cloud Ref

`cloud_ref` is the active cloud reboot target. It keeps the useful cloud camera,
weather, and time controls, but replaces the legacy per-view raymarch with a
texture-backed sky/cloud product that is composited over a lightweight
atmosphere-style background.

This first checkpoint is intentionally scoped to surface and high camera review.
It uploads a deterministic 2D weather texture for broad cloud organization,
renders a fixed 512x512 cloud product each frame, and composites radiance plus
transmittance into the final image. The config records the intended 64-frame
cache cadence, but tiled/triple-buffered updates and 3D shape/erosion textures
are follow-up work after this baseline is visually useful.

Useful runs:

```sh
./build/dev/projects/cloud_ref/cloud_ref
./build/dev/projects/cloud_ref/cloud_ref --cloud-camera-mode surface
./build/dev/projects/cloud_ref/cloud_ref --cloud-camera-mode surface-up
./build/dev/projects/cloud_ref/cloud_ref --cloud-camera-mode high
./build/dev/projects/cloud_ref/cloud_ref --cloud-camera-mode high-oblique
./build/dev/projects/cloud_ref/cloud_ref --cloud-weather-preset fair-weather
./build/dev/projects/cloud_ref/cloud_ref --cloud-weather-preset broken-cumulus
./build/dev/projects/cloud_ref/cloud_ref --cloud-weather-preset storm-cells
./build/dev/projects/cloud_ref/cloud_ref --debug-view weather
./build/dev/projects/cloud_ref/cloud_ref --debug-view density
./build/dev/projects/cloud_ref/cloud_ref --debug-view transmittance
./build/dev/projects/cloud_ref/cloud_ref --debug-view lighting
./build/dev/projects/cloud_ref/cloud_ref --debug-view cloud-alpha
./build/dev/projects/cloud_ref/cloud_ref --debug-view background
./build/dev/projects/cloud_ref/cloud_ref --headless --frames 2 --cloud-camera-mode surface --output outputs/cloud-ref-surface.png
./build/dev/projects/cloud_ref/cloud_ref --headless --frames 2 --cloud-camera-mode high --output outputs/cloud-ref-high.png
projects/cloud_ref/capture_review.sh outputs/cloud-ref-review
```

Controls:

- Left-drag: rotate the camera.
- `D`: cycle final, weather, density, transmittance, lighting, cloud-alpha,
  and background debug views.
- Space: play/pause solar time.
- `R`: reset camera, time, and cloud settings.

Known limits:

- Orbit and planet-scale cloud rendering are deliberately not solved here yet.
- The cache product currently full-refreshes every frame; tile updates and
  triple-buffer blending are the next architectural step.
- The density model has one uploaded weather texture plus procedural detail.
  Full Perlin-Worley and Worley erosion textures are not baked yet.

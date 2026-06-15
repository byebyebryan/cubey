# Cloud Ref 2

`cloud_ref_2` is a second volumetric cloud reference focused on the Godot v2
cached-sky architecture. It intentionally stays separate from both:

- `cloud_ref`, the TerrainEngine-style per-view raymarch reference;
- the future production `cloud` project, which can borrow pieces after the
  references are understood.

The important difference is that `cloud_ref_2` does not raymarch a screen-sized
cloud product every frame. It keeps three persistent `rgba16f` octahedral sky
textures:

- one texture is updated a tile at a time by compute;
- two complete textures are sampled by the fullscreen composite;
- after a full update cycle, the textures rotate and the composite blends from
  the previous complete sky to the newest complete sky.

This project is an architecture-validation reference, not a faithful Godot
visual port. It reuses Cubey-generated Perlin-Worley, Worley, and weather
textures instead of importing the Godot `.bmp`/`.tga` assets, and it uses a
standalone sky/background approximation instead of Godot's sky/transmittance
LUTs. `cloud_ref` remains the stronger density/shape visual reference.

Useful runs:

```sh
./build/dev/projects/cloud_ref_2/cloud_ref_2
./build/dev/projects/cloud_ref_2/cloud_ref_2 --cloud-camera-mode surface
./build/dev/projects/cloud_ref_2/cloud_ref_2 --cloud-camera-mode surface-up
./build/dev/projects/cloud_ref_2/cloud_ref_2 --cloud-camera-mode high-oblique
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view raw-cloud-product
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view blend-from
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view blend-to
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view update-region
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view oct-uv
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view cache-direction
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view cache-horizon
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view cache-checker
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view cache-alpha
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view density
./build/dev/projects/cloud_ref_2/cloud_ref_2 --headless --frames 70 --cloud-cache-frames 16 --cloud-cache-texture-size 1024 --debug-view cache-checker --output outputs/cloud-ref-2-cache-checker.png
./build/dev/projects/cloud_ref_2/cloud_ref_2 --headless --frames 70 --cloud-camera-mode surface-up --output outputs/cloud-ref-2-surface-up.png
projects/cloud_ref_2/capture_review.sh outputs/cloud-ref-2-review
CACHE_MATRIX=1 projects/cloud_ref_2/capture_review.sh outputs/cloud-ref-2-cache-matrix
```

For PNG/headless captures, `--frames` controls how many cache tile updates are
recorded before the still is composited. Use at least one full cache cadence
plus a small margin: for example `70` for the default `--cloud-cache-frames 64`.
`--cloud-cache-frames` accepts `4`, `16`, `64`, and `256`; cache texture size is
limited to `256`, `512`, `768`, or `1024`.

Cache validation views:

- `cache-direction`: decoded cache direction, useful for vertical or fold-line
  distortion.
- `cache-horizon`: synthetic horizon gradient, useful for horizon seam checks.
- `cache-checker`: checker plus tile-grid overlay, useful for tile/update seams.
- `cache-alpha`: synthetic transmittance channel check.

The cached sky is an upper-hemisphere product. Below-horizon view rays are kept
transparent in the composite path instead of sampling the clamped horizon cache.

Controls:

- Left-drag: rotate the camera.
- `D`: cycle diagnostic views.
- Space: play/pause solar time.
- `R`: reset camera, time, cloud settings, and cache state.

Known limits:

- This is still a reference, not the production cloud renderer.
- The Godot source uses imported noise/weather textures and sky/transmittance
  LUTs; this port currently reuses Cubey-generated noise and Cubey's standalone
  sky/background approximation.
- Synthetic cache diagnostics are the primary acceptance check for the cached
  sky architecture. If they are clean but final cloud views still repeat, treat
  the remaining issue as density/noise/weather data rather than cache mechanics.
- Orbit mode is a preview angle, not planet-scale cloud integration.

Attribution: `godot-volumetric-cloud-demo-v2` is MIT licensed. The shader also
follows common Schneider/Nubis volumetric cloud techniques cited by that
project.

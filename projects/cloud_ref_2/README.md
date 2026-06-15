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

The initial port reuses Cubey-generated Perlin-Worley, Worley, and weather
textures instead of importing the Godot `.bmp`/`.tga` assets. The shader ports
the Godot v2 structure: octahedral sky cache, lower Earth-scale cloud shell,
Schneider-style height gradients, weather-driven coverage/type, detail erosion,
stacked HG phase, six light samples, and one distant light sample.

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
./build/dev/projects/cloud_ref_2/cloud_ref_2 --debug-view density
./build/dev/projects/cloud_ref_2/cloud_ref_2 --headless --frames 70 --cloud-camera-mode surface-up --output outputs/cloud-ref-2-surface-up.png
projects/cloud_ref_2/capture_review.sh outputs/cloud-ref-2-review
```

For PNG/headless captures, `--frames` controls how many cache tile updates are
recorded before the still is composited. Use at least `70` with the default
64-frame cache cadence when you want a filled cache in the output image.

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
- The current tuning proves the cache path and removes the old streaking
  failure mode, but the surface views are still too smeared/overcast compared
  with the stronger Godot screenshots.
- Orbit mode is a preview angle, not planet-scale cloud integration.

Attribution: `godot-volumetric-cloud-demo-v2` is MIT licensed. The shader also
follows common Schneider/Nubis volumetric cloud techniques cited by that
project.

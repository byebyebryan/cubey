# Cloud Ref

`cloud_ref` is the active reference target for volumetric clouds. Its default
path should stay deliberately close to the MIT-licensed TerrainEngine-OpenGL
volumetric cloud renderer, even when that makes it less Cubey-native:

- one-time generated 128^3 Perlin-Worley base noise
- one-time generated 32^3 Worley erosion/detail noise
- generated 1024^2 weather coverage/type map
- spherical shell cloud raymarch with height gradients, detail erosion, Beer
  transmittance, Bayer jitter, and a short cone light march
- fullscreen composite over a source-like sky/background

The intent is to keep a known, concrete volumetric reference running inside
Cubey before building a production `cloud` project. Cubey-specific weather
systems, ocean/planet integration, temporal cache updates, and shared atmosphere
adaptation belong in that later project unless they are required to reproduce
the TerrainEngine reference.

Attribution: TerrainEngine-OpenGL is MIT licensed, copyright Federico Vaccaro.
The TerrainEngine shaders also cite Sebastian Hillaire/Nubis-style tileable
volume noise and NadirRoGue-style volumetric cloud references; preserve those
comments when touching the ported shader code.

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
./build/dev/projects/cloud_ref/cloud_ref --debug-view base-density
./build/dev/projects/cloud_ref/cloud_ref --debug-view detail-density
./build/dev/projects/cloud_ref/cloud_ref --debug-view density
./build/dev/projects/cloud_ref/cloud_ref --debug-view transmittance
./build/dev/projects/cloud_ref/cloud_ref --debug-view lighting
./build/dev/projects/cloud_ref/cloud_ref --debug-view shadow
./build/dev/projects/cloud_ref/cloud_ref --debug-view cloud-alpha
./build/dev/projects/cloud_ref/cloud_ref --debug-view distance
./build/dev/projects/cloud_ref/cloud_ref --debug-view steps
./build/dev/projects/cloud_ref/cloud_ref --debug-view background
./build/dev/projects/cloud_ref/cloud_ref --debug-view raw-final
./build/dev/projects/cloud_ref/cloud_ref --headless --frames 2 --cloud-camera-mode surface --output outputs/cloud-ref-surface.png
./build/dev/projects/cloud_ref/cloud_ref --headless --frames 2 --cloud-camera-mode high --output outputs/cloud-ref-high.png
projects/cloud_ref/capture_review.sh outputs/cloud-ref-review
```

Reference captures from the original TerrainEngine app are kept in
`outputs/terrainengine-ref-capture/`. Use `contact-sheet.png`, `frame-8s.png`,
and `frame-14s.png` as the current visual baseline: chunky coherent cumulus
masses, strong sky/water interaction, visible post/resolution artifacts, and a
source UI overlay.

Current Cubey port checkpoint:

- generated 3D base/detail noise now uses mip chains like the OpenGL source;
- Perlin-Worley, Worley, and weather texture generation use source-aligned
  formulas;
- the default cloud shell uses the TerrainEngine coordinate model: 600 km
  planet radius, camera height above the local ground plane, 5 km cloud base,
  and 17 km cloud thickness;
- cloud density, coverage, erosion, Beer transmittance, cone light march, and
  source-style fixed sun direction are now closer to TerrainEngine than to the
  earlier Cubey cloud prototype.

Latest local comparison captures are in `outputs/cloud-ref-faithful-port/`.
`surface.png`, `high.png`, and `high-oblique.png` show coherent source-like
cloud masses without the old local-volume streaking failure, but they also show
the current limitations: gray source-style lighting, single-frame Bayer/dither
grain, and Cubey's placeholder sky/ground composite instead of TerrainEngine's
water, skybox, bloom, god rays, and post pipeline.

The presentation checkpoint in `outputs/cloud-ref-presentation-review/` adds a
shader-only sky/water horizon context, a final-view cloud resolve, and mild
source-style post shaping. `surface-up.png` is the best surface cloud framing;
`surface.png` is intentionally a straight horizon review angle. `raw-final.png`
shows the same composition before final resolve/post.

Controls:

- Left-drag: rotate the camera.
- `D`: cycle the reference diagnostic views.
- Space: play/pause solar time.
- `R`: reset camera, time, and cloud settings.

Known limits:

- `cloud_ref` is allowed to inherit TerrainEngine visual rough edges while it is
  serving as a fidelity baseline.
- TerrainEngine water/skybox/post effects are approximated with shader-only
  presentation context; real water, bloom FBOs, god rays, and cloud-distance
  outputs are not ported yet.
- Orbit mode is a diagnostic preview, not a finished planet-scale weather
  system.

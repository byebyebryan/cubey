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
./build/dev/projects/cloud_ref/cloud_ref --cloud-view-steps 96
./build/dev/projects/cloud_ref/cloud_ref --cloud-view-samples 2
./build/dev/projects/cloud_ref/cloud_ref --cloud-view-samples 1 --cloud-resolve-mode metadata-bilateral --cloud-resolve-radius-px 1.5
./build/dev/projects/cloud_ref/cloud_ref --debug-view weather
./build/dev/projects/cloud_ref/cloud_ref --debug-view base-density
./build/dev/projects/cloud_ref/cloud_ref --debug-view detail-density
./build/dev/projects/cloud_ref/cloud_ref --debug-view density
./build/dev/projects/cloud_ref/cloud_ref --debug-view transmittance
./build/dev/projects/cloud_ref/cloud_ref --debug-view lighting
./build/dev/projects/cloud_ref/cloud_ref --debug-view ambient-light
./build/dev/projects/cloud_ref/cloud_ref --debug-view direct-light
./build/dev/projects/cloud_ref/cloud_ref --debug-view phase-light
./build/dev/projects/cloud_ref/cloud_ref --debug-view shadow
./build/dev/projects/cloud_ref/cloud_ref --debug-view cloud-alpha
./build/dev/projects/cloud_ref/cloud_ref --debug-view distance
./build/dev/projects/cloud_ref/cloud_ref --debug-view steps
./build/dev/projects/cloud_ref/cloud_ref --debug-view background
./build/dev/projects/cloud_ref/cloud_ref --debug-view raw-final
./build/dev/projects/cloud_ref/cloud_ref --headless --frames 2 --cloud-camera-mode surface --output outputs/cloud-ref-surface.png
./build/dev/projects/cloud_ref/cloud_ref --headless --frames 2 --cloud-camera-mode high --output outputs/cloud-ref-high.png
projects/cloud_ref/capture_review.sh outputs/cloud-ref-review
projects/cloud_ref/capture_sampling_compare.sh outputs/cloud-ref-sampling-compare
projects/cloud_ref/capture_lighting_compare.sh outputs/cloud-ref-lighting-compare
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
  and a shorter 11 km broken-cumulus thickness;
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

The lighting checkpoint in `outputs/cloud-ref-lighting-review/` splits cloud
lighting diagnostics into `ambient-light`, `direct-light`, and `phase-light`,
then retunes the standalone march for less ambient wash, warmer direct light,
and a slightly more contrast-preserving final pass. This is a cloud-only pass:
it does not pursue TerrainEngine water, terrain, bloom, or god-ray context, so
the final views still depend heavily on the placeholder horizon scene.

The sampling/coverage checkpoint in
`outputs/cloud-ref-sampling-coverage-review-20260630/` changes final output to a
true cloud layer: march writes premultiplied cloud radiance plus continuous
alpha, and the composite pass resolves that layer before applying it over the
sky/background. It also adds explicit `--cloud-view-steps` and
`--cloud-view-samples` controls. The review captures show this is a better
diagnostic/reconstruction contract. Follow-up inspection selected
`--cloud-view-steps 64 --cloud-view-samples 2` with the default resolve radius as
the near-term visual target for edge stability. Treat that as a reference point:
production cloud rendering should approximate it with cheaper temporal or cached
coverage reconstruction instead of promoting brute-force 2x full-screen march
work by default.

`cloud_ref` also exposes an experimental single-frame spatial reconstruction
path through `--cloud-resolve-mode metadata-bilateral`. In this reference target
the mode is alpha/coverage-aware rather than true metadata-aware: it keeps
`--cloud-view-samples 1`, uses the existing deterministic Bayer phase variation
between neighboring pixels, then resolves premultiplied cloud color and coverage
mostly at transitional cloud edges. It is a diagnostic candidate, not a proven
replacement for brute-force `s2`. Use
`projects/cloud_ref/capture_sampling_compare.sh` to compare `s1 terrain-post`,
`s1 metadata-bilateral`, and brute-force `s2 terrain-post` from the same views.

The default and broken-cumulus ceiling is intentionally below the earlier 22 km
value but still in a practical mid/high cloud range. The current reference does
not model true towering cumulus structure, and an overly tall ordinary volume
stretched clouds vertically enough to make edge/far-field sampling artifacts
more visible. Storm and cirrus presets keep higher ceilings because those
presets explicitly represent taller or higher-altitude cloud types.

Rendering test-bed direction:

- `cloud_ref` is still the stable local-density and sampling baseline, but cloud
  lighting is allowed to diverge from TerrainEngine.
- The target lighting equation is additive: Beer view extinction, cone-marched
  sun transmittance, dual/stacked HG phase, optional powder/multi-scatter boost,
  and separate sky/ground ambient.
- Final output should remain a cloud layer containing linear radiance plus
  transmittance/alpha; final resolve/composite should be isolated from raw
  lighting diagnostics.
- Powder is a scalar boost and defaults off. It now modifies direct sun response
  on top of Beer shadowing instead of acting as the ambient/direct mix gate.
- `capture_lighting_compare.sh` captures final, isolated ambient/direct/phase,
  powder, shadow, and debug-view comparisons for `surface-up` and
  `high-oblique`.
- Ref alignment: TerrainEngine guards density/march parity; Godot v2 and
  UnityVolumetricCloudsURP are stronger lighting/product references; diharaw,
  Meteoros, Project-Marshmallow, and ShaderToy examples are comparison and
  look-dev references.

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
- The latest lighting pass improves inspection and reduces flat grey lift, but
  richer final shots still need better scene context and/or a production cloud
  renderer rather than more one-off `cloud_ref` constants.
- Orbit mode is a diagnostic preview, not a finished planet-scale weather
  system.

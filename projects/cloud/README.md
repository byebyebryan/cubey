# Cloud

`cloud` is the production volumetric cloud project. It starts from the
texture-backed density path proven in `projects/cloud_ref`, while leaving
`cloud_ref`, `cloud_ref_2`, and `clouds_legacy` intact as reference projects.

Current V1 scope:

- generated 128^3 Perlin-Worley base noise;
- generated 32^3 Worley erosion/detail noise;
- generated 1024^2 weather map with authored coverage, cloud type,
  edge-breakup, and local-scatter channels that bias the local 3D density field;
- generated artifact descriptors for the materialized base density volume,
  detail erosion volume, and weather map, using the shared procedural metadata
  contract;
- spherical shell raymarch with height gradients, detail erosion, Beer
  transmittance, powder response, and a short light march;
- world-scale weather/type sampling with separate opt-in vertical shear control;
- sphere-continuous orbit weather coverage/detail/hull, generated from
  planet-normal procedural fields instead of projecting the local weather map;
- separate cloud product and composite passes declared through
  `RenderGraphBuilder`;
- shared `clouds.*` `RunConfig` options and hand-authored ImGui controls;
- tunable ambient/direct/phase lighting, final contrast/saturation, sun glare,
  horizon glow, and alpha-aware final resolve strength;
- configurable static ray-start sampling (`bayer`, `interleaved`, or `off`)
  with jitter-strength control. The default matches the reference Bayer
  ray-start pattern to break up march-step banding;
- temporal cloud product/metadata history for the final view, with
  metadata-aware reprojection and current-neighborhood clamping to reduce
  residual raymarch banding;
- standalone background modes: atmosphere-only by default, plus an opt-in
  `water-context` proxy for ocean-adjacent inspection shots;
- explicit distance regimes: surface/local volumetric marching, broad
  high/orbit shell evaluation, and debug views for the local-vs-orbit blend;
- diagnostics for weather, weather edge, weather bias, base/detail density,
  density, transmittance, cloud type, visible density/cloud type, lighting,
  shadow, cloud alpha, distance, distance regime, local/orbit alpha, orbit
  coverage/detail/hull, metadata distance/alpha/confidence, metadata density,
  steps, and background.

The first target is cloud shape: raw density and final captures should show
coherent cloud masses without relying on cache, temporal reconstruction, or
final blur. Validate against `projects/cloud_ref`; do not tune this project
toward `cloud_ref_2` visuals.

Useful runs:

```sh
./build/dev/projects/cloud/cloud
./build/dev/projects/cloud/cloud --cloud-camera-mode surface-up
./build/dev/projects/cloud/cloud --cloud-camera-mode high-oblique
./build/dev/projects/cloud/cloud --cloud-weather-preset clear
./build/dev/projects/cloud/cloud --cloud-weather-preset fair-weather
./build/dev/projects/cloud/cloud --cloud-weather-preset broken-cumulus
./build/dev/projects/cloud/cloud --cloud-weather-preset storm-cells
./build/dev/projects/cloud/cloud --debug-view raw-final
./build/dev/projects/cloud/cloud --debug-view weather
./build/dev/projects/cloud/cloud --debug-view base-density
./build/dev/projects/cloud/cloud --debug-view detail-density
./build/dev/projects/cloud/cloud --debug-view cloud-type
./build/dev/projects/cloud/cloud --debug-view weather-edge
./build/dev/projects/cloud/cloud --debug-view weather-bias
./build/dev/projects/cloud/cloud --debug-view density
./build/dev/projects/cloud/cloud --debug-view visible-density
./build/dev/projects/cloud/cloud --debug-view visible-cloud-type
./build/dev/projects/cloud/cloud --debug-view transmittance
./build/dev/projects/cloud/cloud --debug-view lighting
./build/dev/projects/cloud/cloud --debug-view cloud-alpha
./build/dev/projects/cloud/cloud --debug-view distance-regime
./build/dev/projects/cloud/cloud --debug-view local-alpha
./build/dev/projects/cloud/cloud --debug-view orbit-alpha
./build/dev/projects/cloud/cloud --debug-view orbit-coverage
./build/dev/projects/cloud/cloud --debug-view orbit-detail
./build/dev/projects/cloud/cloud --debug-view orbit-hull
./build/dev/projects/cloud/cloud --debug-view distance
./build/dev/projects/cloud/cloud --debug-view metadata-distance
./build/dev/projects/cloud/cloud --debug-view metadata-alpha
./build/dev/projects/cloud/cloud --debug-view metadata-confidence
./build/dev/projects/cloud/cloud --debug-view metadata-density
./build/dev/projects/cloud/cloud --debug-view steps
./build/dev/projects/cloud/cloud --cloud-ambient-strength 0.85 --cloud-direct-strength 1.25
./build/dev/projects/cloud/cloud --cloud-final-contrast 1.15 --cloud-resolve-strength 0.45
./build/dev/projects/cloud/cloud --cloud-weather-scale-km 85
./build/dev/projects/cloud/cloud --cloud-weather-softness 0.22
./build/dev/projects/cloud/cloud --cloud-weather-influence 0
./build/dev/projects/cloud/cloud --cloud-weather-influence 0.35
./build/dev/projects/cloud/cloud --cloud-weather-influence 1
./build/dev/projects/cloud/cloud --cloud-vertical-shear-fraction 0.14
./build/dev/projects/cloud/cloud --cloud-sampling-mode bayer
./build/dev/projects/cloud/cloud --cloud-sampling-mode off
./build/dev/projects/cloud/cloud --cloud-sampling-mode interleaved --cloud-jitter-strength 0.5
./build/dev/projects/cloud/cloud --cloud-distance-mode local
./build/dev/projects/cloud/cloud --cloud-distance-mode orbit-shell
./build/dev/projects/cloud/cloud --cloud-camera-mode high-oblique --debug-view distance-regime
./build/dev/projects/cloud/cloud --cloud-camera-mode orbit --debug-view orbit-coverage
./build/dev/projects/cloud/cloud --headless --frames 2 --cloud-camera-mode surface-up --output outputs/cloud-v1-surface-up.png
./build/dev/projects/cloud/cloud --headless --frames 2 --cloud-camera-mode high-oblique --output outputs/cloud-v1-high-oblique.png
projects/cloud/capture_review.sh outputs/cloud-v1-review
DEEP=1 projects/cloud/capture_review.sh outputs/cloud-v1-review-deep
```

`capture_review.sh` defaults to a focused shape/regime review: final camera
views, local/orbit alpha, distance-regime checks, orbit procedural
coverage/detail/hull, and a small surface-local density set. `DEEP=1` adds secondary tuning captures
such as sampling comparisons, metadata, lighting breakdowns, weather-influence
sweeps, and explicitly named `orbit-local-weather` diagnostics for the old
surface-local weather projection. The script also writes
`diagnostic-crops/center-feature-contact.png` with resolution-scaled center
crops for the active review set.

Controls:

- Left-drag: rotate the camera.
- `D`: cycle diagnostic views.
- Space: play/pause solar time.
- `R`: reset camera, time, and cloud settings.
- `Weather scale`: approximate broad weather feature size in kilometers.
- `Shape / Density`: base shape scale, vertical shear, detail erosion, and
  powder response.
- `Weather softness`: damps broad weather contrast before local density shaping.
- `Weather influence`: controls how strongly the broad weather map biases local
  shape thresholds, cloud type, and edge erosion. The default is `0`, preserving
  the local noise-scattered baseline while authored weather remains opt-in.
- `Sampling`: ray-start sampling mode and jitter amount.
- `Distance Regime`: local/orbit mode, altitude and ray-distance transition
  ranges, broad-shell detail strength, and broad-shell density scale.
  Orbit shell diagnostics sample a planet-space coverage/detail/hull model
  rather than the surface-local planar density field.
- `Lighting`: ambient, direct sun, phase/rim, absorption, shadow, and horizon
  fill controls.
- `Final Resolve`: alpha-aware smoothing amount plus final contrast,
  saturation, horizon glow, sun glare, and metadata-aware neighborhood resolve.

Known deferrals:

- No cached octahedral hemisphere path yet. `cloud_ref_2` remains the cache
  architecture reference for a later pass.
- Temporal reconstruction exists for final-view cleanup, but raw diagnostics
  remain the source of truth when judging cloud shape.
- Static sampling controls are diagnostic and deterministic. A blue-noise or
  spatiotemporal blue-noise sampling path remains deferred until the direct
  local/orbit regimes are stable.
- Cloud type is exposed as raw and visible diagnostics, and drives the current
  height gradient model. The surface/local path still uses planar weather
  projection; the orbit shell now uses a separate planet-space hull.
- Orbit final output uses the first planet-space coverage/detail/hull path. It
  is still a direct shell renderer, not a finished cached sky product or
  asset-backed global weather map.
- No ocean, planet, terrain, or PBR integration yet. Future consumers should
  sample cloud outputs rather than owning cloud raymarch code.
- No promoted shared cloud renderer API yet. Textures, descriptors, materials,
  and synchronization remain project-owned in V1.
- Generated artifact metadata is descriptor-only for now. GPU content hashes,
  readback/export metadata, and shader-only orbit coverage/detail/hull artifact
  descriptors remain deferred.

See
[`docs/architecture/cloud-rendering.md`](../../docs/architecture/cloud-rendering.md)
for the production cloud direction and promotion criteria.

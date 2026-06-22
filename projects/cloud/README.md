# Cloud

`cloud` is the production cloud/weather project. It keeps the local volumetric
density path proven in `projects/cloud_ref`, while using a filtered
`surface-shell` path as the default orbit representation for satellite-style
full-disk review.

Current V1 scope:

- generated 128^3 Perlin-Worley base noise;
- generated 32^3 Worley erosion/detail noise;
- generated 1024^2 weather map with authored coverage, cloud type,
  and edge-breakup channels for authored weather influence;
- generated artifact descriptors for the materialized base density volume,
  detail erosion volume, and weather map, using the shared procedural
  metadata contract;
- spherical shell raymarch with height gradients, detail erosion, Beer
  transmittance, powder response, and a short light march;
- world-scale local weather/type sampling with broad coverage, clear slots,
  fronts, cells, streaks, and micro scatter, plus separate opt-in vertical shear
  control;
- sphere-continuous orbit weather coverage/detail/hull, evaluated from direct
  planet-space procedural fields with regional dry slots, storm tracks, fronts,
  cells, and streaks instead of projecting the local weather map;
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
- explicit distance regimes: surface/local volumetric marching, default
  high/orbit shell evaluation, and debug views for the local-vs-orbit blend;
- footprint- and grazing-aware orbit shell filtering so high-frequency weather
  detail is retained on the disk but damped near the shell edge;
- diagnostics for authored weather, local scatter/clear/structure/edge detail,
  weather edge, coverage bias, base/detail density, density, transmittance,
  cloud type, visible density/cloud type, lighting, shadow, cloud alpha,
  distance, distance regime, local/orbit alpha, ray-sampled orbit
  coverage/detail/hull, shell alpha/height/normal/shadow, metadata
  distance/alpha/confidence, metadata density, steps, and background.

The first target is cloud shape. Surface/local captures should preserve coherent
volumetric cloud masses; orbit captures should be judged against satellite and
full-disk Earth imagery, not against the volume raymarch comparison path. Orbit
should show broken regional systems with visible clear windows, fronts,
streaks, cells, and filtered cloud-top detail. Fuller orbit coverage should
come from the same weather fields filling empty regions, not from a smooth
planet-wide procedural cap.

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
./build/dev/projects/cloud/cloud --debug-view authored-weather
./build/dev/projects/cloud/cloud --debug-view local-scatter
./build/dev/projects/cloud/cloud --debug-view local-clear
./build/dev/projects/cloud/cloud --debug-view local-structure
./build/dev/projects/cloud/cloud --debug-view local-edge-detail
./build/dev/projects/cloud/cloud --debug-view base-density
./build/dev/projects/cloud/cloud --debug-view detail-density
./build/dev/projects/cloud/cloud --debug-view cloud-type
./build/dev/projects/cloud/cloud --debug-view weather-edge
./build/dev/projects/cloud/cloud --debug-view coverage-bias
./build/dev/projects/cloud/cloud --debug-view density
./build/dev/projects/cloud/cloud --debug-view visible-density
./build/dev/projects/cloud/cloud --debug-view visible-cloud-type
./build/dev/projects/cloud/cloud --debug-view transmittance
./build/dev/projects/cloud/cloud --debug-view lighting
./build/dev/projects/cloud/cloud --debug-view cloud-alpha
./build/dev/projects/cloud/cloud --debug-view distance-regime
./build/dev/projects/cloud/cloud --debug-view transition-weights
./build/dev/projects/cloud/cloud --debug-view local-alpha
./build/dev/projects/cloud/cloud --debug-view far-shell-alpha
./build/dev/projects/cloud/cloud --debug-view local-with-shell-alpha
./build/dev/projects/cloud/cloud --debug-view orbit-alpha
./build/dev/projects/cloud/cloud --debug-view orbit-coverage
./build/dev/projects/cloud/cloud --debug-view orbit-detail
./build/dev/projects/cloud/cloud --debug-view orbit-hull
./build/dev/projects/cloud/cloud --debug-view orbit-shell-alpha
./build/dev/projects/cloud/cloud --debug-view orbit-shell-height
./build/dev/projects/cloud/cloud --debug-view orbit-shell-normal
./build/dev/projects/cloud/cloud --debug-view orbit-shell-shadow
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
./build/dev/projects/cloud/cloud --cloud-camera-mode orbit --cloud-orbit-representation volume
./build/dev/projects/cloud/cloud --cloud-camera-mode orbit --cloud-orbit-representation surface-shell
./build/dev/projects/cloud/cloud --cloud-camera-mode orbit --cloud-orbit-fill 0.5
./build/dev/projects/cloud/cloud --cloud-camera-mode orbit --cloud-orbit-fill 1.5
./build/dev/projects/cloud/cloud --cloud-camera-mode high-oblique --cloud-far-shell-strength 0
./build/dev/projects/cloud/cloud --cloud-camera-mode high-oblique --cloud-far-shell-strength 1.5
./build/dev/projects/cloud/cloud --cloud-camera-mode high-oblique --debug-view distance-regime
./build/dev/projects/cloud/cloud --cloud-camera-mode high-oblique --debug-view transition-weights
./build/dev/projects/cloud/cloud --cloud-camera-mode high-oblique --debug-view far-shell-alpha
./build/dev/projects/cloud/cloud --cloud-camera-mode orbit --debug-view orbit-coverage
./build/dev/projects/cloud/cloud --headless --frames 2 --cloud-camera-mode surface-up --output outputs/cloud-v1-surface-up.png
./build/dev/projects/cloud/cloud --headless --frames 2 --cloud-camera-mode high-oblique --output outputs/cloud-v1-high-oblique.png
projects/cloud/capture_review.sh outputs/cloud-v1-review
DEEP=1 projects/cloud/capture_review.sh outputs/cloud-v1-review-deep
```

`capture_review.sh` defaults to a focused shape/regime review: final camera
views, satellite-named orbit captures, high-oblique transition, volume
comparison, local/far-shell/local-plus-shell/orbit alpha, transition weights,
distance-regime checks, ray-sampled orbit procedural coverage/detail/hull,
shell-specific alpha/height/normal/shadow, and
a small surface-local density set. The default set includes high-oblique
far-shell strength comparisons and orbit-fill comparisons. `DEEP=1` adds
secondary tuning captures such as sampling comparisons, metadata, lighting
breakdowns, weather-influence sweeps, satellite orbit motion, and explicitly named
`orbit-local-weather` / `orbit-local-coverage-bias` diagnostics for the old
surface-local weather projection.
The script also writes
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
  the layered procedural local weather baseline while authored weather remains
  opt-in. `local-scatter`, `local-clear`, `local-structure`, and
  `local-edge-detail` show the procedural local fields before final density
  shaping.
- `Sampling`: ray-start sampling mode and jitter amount.
- `Distance Regime`: local/orbit mode, altitude and ray-distance transition
  ranges, broad-shell detail strength, and broad-shell density scale.
  `transition-weights` displays local-branch availability in red, effective
  far-shell contribution in green, and full orbit takeover in blue. `far-shell-alpha`
  and `local-with-shell-alpha` isolate the horizon continuity branch from the
  local volume and full orbit replacement.
  Orbit shell diagnostics sample a direct planet-space coverage/detail/hull
  model rather than the surface-local planar density field. The broad orbit
  weather frequencies derive from `Weather scale`, with regional storm/dry
  masks owning the planet-scale layout and fine detail constrained to fronts,
  cells, streaks, edge breakup, and hull erosion. The orbit shell should read as
  broken regional weather with large clear windows and fewer totally empty
  regions, not as a smooth planet-wide cap. `Orbit fill` biases that empty-space
  fill while preserving the same weather/detail fields. `Far shell strength`
  controls how much broad shell cloud contributes behind local volume in
  high-oblique views.
- `Lighting`: ambient, direct sun, phase/rim, absorption, shadow, and horizon
  fill controls.
- `Final Resolve`: alpha-aware smoothing amount plus final contrast,
  saturation, horizon glow, sun glare, and metadata-aware neighborhood resolve.

Known deferrals:

- No cached octahedral hemisphere path yet. `cloud_ref_2` remains the cache
  architecture reference for a later pass.
- High-oblique is the current regime bridge target. Surface/local volume should
  remain the foreground shape reference, while a broad far-shell contribution
  should carry cloud continuity toward the horizon before full orbit-shell
  replacement takes over.
- Temporal reconstruction exists for final-view cleanup, but raw diagnostics
  remain the source of truth when judging cloud shape.
- Static sampling controls are diagnostic and deterministic. A blue-noise or
  spatiotemporal blue-noise sampling path remains deferred until the direct
  local/orbit regimes are stable.
- Cloud type is exposed as raw and visible diagnostics, and drives the current
  height gradient model. The surface/local path still uses planar weather
  projection; the orbit shell now uses a separate planet-space hull.
- Orbit final output uses direct planet-space procedural coverage/detail/hull
  fields. A generated 2D orbit-weather product was tried and removed after it
  reintroduced projection/blocking artifacts without enough detail. The current
  target remains believable regional systems and orbit-visible detail. Remaining
  issues are art/model tuning, high-oblique transition polish, and motion
  shimmer checks, not a finished cached sky product or asset-backed global
  weather map.
- No ocean, planet, terrain, or PBR integration yet. Future consumers should
  sample cloud outputs rather than owning cloud raymarch code.
- No promoted shared cloud renderer API yet. Textures, descriptors, materials,
  and synchronization remain project-owned in V1.
- Generated artifact metadata is descriptor-only for now. GPU content hashes,
  readback/export metadata, and cached sky/cloud products remain deferred.

See
[`docs/architecture/cloud-rendering.md`](../../docs/architecture/cloud-rendering.md)
for the production cloud direction and promotion criteria.

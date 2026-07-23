# Ocean

`ocean` is Cubey's active horizon-scale open-water renderer. It combines a
GodotOceanWaves-derived spectral FFT core with camera-relative clipmap geometry,
shared atmosphere and cloud lighting, persistent whitecaps, local curvature,
and explicit LOD diagnostics.

The accepted scope is a surface and curved-local ocean. Global ocean topology,
aerial/orbit water, shorelines, bathymetry, refraction, spray, wakes, and
shallow-water flow remain separate integration work.

GodotOceanWaves is MIT licensed; its notice is in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Run

```sh
./build/dev/projects/ocean/ocean
./build/dev/projects/ocean/ocean --headless --frames 120 \
  --width 1280 --height 720 --output /tmp/cubey-ocean.png
```

The default is the `Windy` sea state with a 512 map, half-precision wave fields,
two active cascade slots, shared atmosphere/clouds, planar cloud reflection, and
cached environment fallback.

```sh
./build/dev/projects/ocean/ocean --ocean-sea-state calm
./build/dev/projects/ocean/ocean --ocean-sea-state windy
./build/dev/projects/ocean/ocean --ocean-sea-state stormy
```

Sea state changes wave energy, whitecap response, foam, roughness, self-shadow,
and far-field material response. It does not change field quality, active work
count, camera, environment, seeds, or cascade directions. Manual edits to
preset-owned settings appear as `Custom` in the GUI.

## Controls

Playback, sea state, and camera preset are always visible. The remaining
controls are collapsed into six domains:

- **Waves**: field quality, depth, anti-repeat, presentation, and C0-C4 slots.
- **Surface**: water material and foam.
- **Lighting**: cloud reflections, cloud shadows, and wave self-shadowing.
- **Scale & LOD**: clipmap mesh, curved horizon, far field, and distance fades.
- **Environment**: shared atmosphere/cloud controls and optional terrain fields.
- **Diagnostics**: render views, cascade inspection, wireframe, scale pillar,
  runtime state, and performance counters.

Each cascade owns its enabled state, map size, update interval, and spectral
parameters. C0 and C1 are active in the three sea-state presets; C2-C4 are
neutral opt-in slots rather than special macro/detail roles.

Config files are the preferred way to persist complete runs. Useful CLI
overrides include:

```text
--ocean-sea-state calm|windy|stormy
--ocean-map-size 128|256|512|1024
--ocean-field-precision half|full
--ocean-camera-preset default|low|mid|high|close|overhead|wide
--ocean-mesh-cells 32..512
--ocean-mesh-lod-levels 1..6
--ocean-horizon-target-near-cell-m 0.25..16
--ocean-self-shadow-strength 0..1
--ocean-self-shadow-steps 1..24
--ocean-shape-anti-repeat-strength 0..1
--ocean-detail-anti-repeat-strength 0..1
--ocean-detail-filter adaptive|bilinear|bicubic
--ocean-size-reference | --no-ocean-size-reference
--ocean-cascade all|0|1|2|3|4
--ocean-surface-mode flat|curved-far
--ocean-cloud-reflection-source cached|planar
--cloud-quality quarter|half|full
--no-clouds
```

## Rendering

The water material is an ocean-specific dielectric surface rather than the
general forward-PBR material. It consumes the shared environment's sun, moon,
sky radiance, exposure, and cloud products.

Cloud lighting has three bounded products:

- a local projected transmittance map for direct-light shadows;
- a reflected planar cloud view for detailed visible-ocean reflection;
- a roughness-filtered cached cloud environment for broad fallback coverage.

The cached product uses the shared `CloudEnvironmentRuntime` lifecycle and
generation state. Ocean retains only its water-specific cached/planar selection
and descriptor wiring.

The planar source is the production default. Cached-only mode is useful for
general coverage and diagnostics. The rejected current-view and hybrid sources
have been removed.

Far water is intentionally reflection-led. Resolved displacement and normal
detail fade when distance and mesh footprint can no longer represent them;
unresolved energy feeds roughness, broad reflection variation, and a bounded sun
glitter corridor. Removed moment-pyramid, synthetic far-normal, and filtered
far-whitecap experiments are available only through git history.

The mesh remains a camera-relative `ClipmapGrid2D`. Automatic horizon extent and
altitude-aware cell sizing reduce near-grid cost in high views, while patch
culling keeps submitted triangles separate from generated clipmap triangles.
The shared adaptive planet patch planner is not used by this local renderer.

## Diagnostics

Common debug views:

```sh
./build/dev/projects/ocean/ocean --debug-view displacement
./build/dev/projects/ocean/ocean --debug-view normal
./build/dev/projects/ocean/ocean --debug-view foam
./build/dev/projects/ocean/ocean --debug-view foam-source
./build/dev/projects/ocean/ocean --debug-view foam-history
./build/dev/projects/ocean/ocean --debug-view lod
./build/dev/projects/ocean/ocean --debug-view footprint
./build/dev/projects/ocean/ocean --debug-view energy-lod
./build/dev/projects/ocean/ocean --debug-view far-field
./build/dev/projects/ocean/ocean --debug-view cloud-shadow
./build/dev/projects/ocean/ocean --debug-view cloud-reflection
./build/dev/projects/ocean/ocean --debug-view cloud-reflection-validity
./build/dev/projects/ocean/ocean --debug-view reflection
./build/dev/projects/ocean/ocean --debug-view specular
./build/dev/projects/ocean/ocean --debug-view background
```

The Diagnostics panel reports effective horizon coverage, clipmap and submitted
triangle counts, cascade support bands, cloud-product state, and GPU spans. The
50 m sea-level-centered pillar provides a scale reference and a simple analytic
shadow caster.

## Review

Use the single canonical closure harness after changing waves, foam, LOD,
lighting, reflection, or environment integration:

```sh
MOTION=0 projects/ocean/capture_ocean_review.sh outputs/ocean-review
MOTION=1 projects/ocean/capture_ocean_review.sh outputs/ocean-review-motion
```

It records the source commit, emits a manifest and index, compares all three sea
states at low/mid/high scales, covers cloudy noon and dawn/dusk/night lighting,
and includes focused foam, reflection, shadow, specular, LOD, and far-field
diagnostics. Use `profile_ocean_baseline.sh` for the whole-renderer 256/512/1024,
half/full, camera-scale, and cloud-composition GPU matrix.
Use `profile_scene_ocean_ablation.sh` for attributed surface, mesh, shadow,
filtering, anti-repeat, composed-cloud, and resolution comparisons. Set
`LANE_FILTER` for a focused rerun or `SUMMARIZE_ONLY=1` to rebuild summaries from
retained artifacts. It keeps the production scene pass intact, uses matched
background-only runs for surface attribution, and rejects unstable wave timings.
`profile_cloud_reflections.sh` remains the focused cached-versus-planar
reflection harness.

Current decisions and boundaries are summarized in
[Surface Ocean V1](../../docs/notes/ocean-surface-v1.md). Architecture and
review guidance live in
[Ocean rendering](../../docs/architecture/ocean-rendering.md) and
[Ocean visual captures](../../docs/notes/ocean-visual-captures.md).

## Deferred Work

- Consume real terrain bathymetry, shore distance, water datum, and scene depth.
- Add shoreline/surf behavior, seafloor visibility, and robust underwater view.
- Add local wakes, interaction ripples, spray, and particle whitewater.
- Define a planet adapter for global ocean topology and aerial/orbit views.
- Decide whether lakes can reuse this renderer after terrain water-body products
  exist; rivers and shallow flow require a different model.

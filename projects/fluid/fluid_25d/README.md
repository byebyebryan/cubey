# Fluid 2.5D — rain, runoff and collection

A macro-scale terrain-water demo: rain supplies an unmodified generated heightfield,
water forms downhill streams, gathers in basins, and retreats when supply stops.
Cubey renders the changing water surface, wet banks, rain and flow cues.

The preferred demo runs **SynxFlow in a separate CUDA worker**, streaming native
depth and momentum into Cubey's Vulkan renderer. The simpler builtin solvers and
recorded playback remain explicit alternatives; they are not silent fallbacks.

## Demo at a glance

- Default prepared site: Mountain, 1024² cells at 16 m, a 16.384 km-square patch.
- Appearance: Scenic Macro, shared source-aligned terrain material, rough water,
  B-spline 2x banks, scene-fitted shadows, visible rain and dots/trails.
- Startup: genuinely dry and paused. Start/Space defaults to the measured guided
  rain response on the qualified Mountain bed; Manual keeps ordinary simulation.
- Guide: 512 mm/h at requested 60x, rain off at two simulated hours, explanation
  through six hours. Finishing the guide does **not** pause the solver.
- Composition: only the selected full-resolution simulation patch. Wider terrain
  and outer LOD belong to the shared Terrain foundation, not this viewer.

This is deliberately exaggerated demonstration rainfall, not calibrated weather,
surveyed terrain or research-grade hydrology. The elevation source is 30 m;
a finer simulation grid interpolates it rather than inventing terrain detail.

## Build and prerequisites

Build the renderer normally:

```sh
rtk proxy cmake --preset release
rtk proxy cmake --build --preset release --target cubey_project_fluid_25d -j 6
```

The preferred live demo additionally needs already prepared terrain/climate
assets, native case inputs, and the separately prepared SynxFlow worker environment
and qualified service extension. These large/generated artifacts are **not**
committed here. Normal configure, build, test and launch do not install packages,
download models, generate terrain, build CUDA, or substitute another solver.

See [explicit setup and preparation](demo_setup_v1.md). A fresh checkout alone
does not supply the external runtime or captured terrain. Paths can be passed
explicitly; the native extension must still match the qualified hash.

## Launch

After explicit preparation, preflight without starting a solver or opening a window:

```sh
rtk proxy python3 projects/fluid/fluid_25d/run_terrain_rain_demo.py \
  --study outputs/fluid/demo-local --print-command
```

Then launch locally using the same prepared directory:

```sh
rtk proxy python3 projects/fluid/fluid_25d/run_terrain_rain_demo.py \
  --study outputs/fluid/demo-local
```

Existing maintainer evidence can also be selected explicitly, for example
`--study outputs/fluid/site-expansion-20261011-jtIA3s`. That dated directory is
not a repository dependency or an assumed fresh-checkout asset.

Explicit worker path overrides:

```sh
rtk proxy python3 projects/fluid/fluid_25d/run_terrain_rain_demo.py \
  --study outputs/fluid/demo-local \
  --native-python /path/to/worker-env/bin/python \
  --extension /path/to/qualified/flood.cpython-311-x86_64-linux-gnu.so
```

The interpreter path is retained so a venv symlink does not lose its environment.
A path override does not relax the extension hash guard. A different build needs
separate qualification.

## Controls and how to read the scene

| Control / observation | Meaning |
| --- | --- |
| Start demo / Space | Runs the qualified rain-response guide from dry/paused water. |
| Manual | Uses your current rain, pace and camera with no scheduled rain shutoff. |
| Pause / Space while running | Pauses the actual worker; manual solver input exits the guide. |
| Rain Off | Stops water input, not downhill motion or delayed collection. |
| Reset water / R | Creates a fresh dry/paused session; retains chosen mode and applied rain/pace. |
| Site/grid selection + Apply & restart | Replaces the complete session; does not regrid existing water. |
| Whole patch / Downhill runoff / Basin collection | Overview and fixed observation cameras, not source/drain locations. |
| Dots, trails and whitewater | Approximate flow cues from native velocity; not conserved dye or native particles. |
| Water isolation / Flow inspection / raw maps | Diagnostic readings; Scenic color is not a depth palette. |

Rain supplies the entire patch. Streams follow the bed rather than an authored
source-to-drain channel. The perimeter is open/fall boundary, not a hidden outlet
marker. Basin water may still grow after Rain Off because upstream water arrives
later; remaining water is not automatically a certified calm or permanent lake.

Manual rain/pace or camera selection exits the guide and selects Manual.
Orbit/zoom during an active guide does the same. Guided Start explicitly restores
512 mm/h; after a rain-off reset, Manual Start needs a rain preset to wet up.
The prepared 20-hour solver horizon remains finite and explicit.

See [managed lifecycle and UI](terrain_demo_ux_v1.md) and
[measured rain-response evidence](rain_response_v1.md).

## Backends and boundaries

| Backend | What runs | Role |
| --- | --- | --- |
| External | Separately licensed SynxFlow CUDA worker; Vulkan viewer imports fields | Preferred terrain-rain demo with acknowledged solver controls. |
| Recording | Saved native fields; no solver | Repeatable viewing, presentation and headless captures. |
| Builtin | Cubey's existing GPU virtual-pipes or finite-volume implementation | Simpler standalone alternative and diagnostic scenarios; not equivalent mountain results. |

`--fluid25d-backend builtin|external|recording` makes the producer explicit.
No water-state transfer or silent fallback occurs between these backends.
The retained stock-file stream bridge is viewing-only, unlike the controlled
external service. See [backend contracts and retained implementation evidence](README_HISTORY.md#product-boundary).

Replay explicitly, without CUDA computation:

```sh
rtk proxy python3 projects/fluid/fluid_25d/run_terrain_rain_demo.py replay \
  --study outputs/fluid/demo-local --recording /path/to/recording.json
```

This launcher initially holds the final saved state. R rewinds; playback controls
change viewing time, not recorded hydraulics. Presentation films use actual saved
states held between saves; a 60 FPS rendered video does not imply 60 Hz hydraulic
updates, real-time solver speed or native display/scanout FPS.

The builtin alternative needs neither SynxFlow nor Terrain Diffusion:

```sh
rtk proxy build/release/projects/fluid/fluid_25d/fluid_25d --fluid25d-backend builtin
```

Its authored diagnostic scenario is retained, not the featured mountain-rain scene.

## Terrain, lighting and performance

The prepared catalogue includes multiple existing Terrain Diffusion landscapes
and power-of-two grid presets. Missing or changed inputs remain disabled.
The default grid/presentation is deliberately bounded; requesting a fine 2k grid
does not guarantee the worker will sustain 60x.

Fixed daylight is the normal entry. Optional shared clear-sky day/night is
render-only and does not change rainfall or solver time. Clouds are excluded:
the current shared surface cloud model is not an aerial/cloud-top presentation.

See [terrain presentation](terrain_presentation_v1.md),
[cached sites](site_expansion_v1.md), [shared environment](shared_environment_v1.md),
[headless performance](offscreen_perf_v2.md), and [patch-only scope](patch_only_v1.md).
Hardware-offscreen timing, native GUI interaction and human visual acceptance
are distinct evidence. Cinematic recording/encoding is not a live performance trial.

Saved water also has an opt-in consumer in the [shared Terrain foundation](../../terrain/README.md).
That backdrop work retains its own wider terrain/LOD ownership.

## Development and evidence

Current guide and presentation work builds on retained studies; it does not
rewrite their outcomes. The [development record](README_HISTORY.md) preserves
the previous full README, numerical experiments, rejected treatments and dated
reproduction commands. Defaults described there are historical snapshots.

The broader design map is [Fluid Simulation Direction](../../../docs/architecture/fluid-simulation.md).
Cubey's renderer/client remains MIT; the external worker/build slice is separately
marked GPL-3.0-only in [its license](external_synxflow/LICENSE). It is not compiled
into the Vulkan application. See the setup document for dependency provenance.

<details>
<summary>Historical section links (previous README anchors)</summary>

These links preserve earlier deep-link entry points while directing development
history to its indexed record. They are not today's default setup instructions.

- <a id="status"></a>[Status](README_HISTORY.md#status)
- <a id="terrain-rain-demo-start-here"></a>[Terrain Rain Demo: start here](README_HISTORY.md#terrain-rain-demo-start-here)
- <a id="shared-clear-sky-daynight-optional"></a>[Shared clear-sky day/night (optional)](README_HISTORY.md#shared-clear-sky-daynight-optional)
- <a id="prepared-world-collection-explicit"></a>[Prepared-world collection (explicit)](README_HISTORY.md#prepared-world-collection-explicit)
- <a id="historical-mountain-rain-demo-v1"></a>[Historical Mountain Rain Demo V1](README_HISTORY.md#historical-mountain-rain-demo-v1)
- <a id="october-7-results-and-verdict"></a>[October 7 results and verdict](README_HISTORY.md#october-7-results-and-verdict)
- <a id="retained-numerical-and-integration-evidence"></a>[Retained numerical and integration evidence](README_HISTORY.md#retained-numerical-and-integration-evidence)
- <a id="product-boundary"></a>[Product boundary](README_HISTORY.md#product-boundary)
- <a id="backend-selection"></a>[Backend selection](README_HISTORY.md#backend-selection)
- <a id="project-local-backend-contract"></a>[Project-local backend contract](README_HISTORY.md#project-local-backend-contract)
- <a id="bounded-external-service-transport-and-viewer"></a>[Bounded external-service transport and viewer](README_HISTORY.md#bounded-external-service-transport-and-viewer)
- <a id="optional-source-build-audit"></a>[Optional source-build audit](README_HISTORY.md#optional-source-build-audit)
- <a id="optional-gpl-external-service-worker"></a>[Optional GPL external service worker](README_HISTORY.md#optional-gpl-external-service-worker)
- <a id="continuous-service-acceptance-and-limits"></a>[Continuous-service acceptance and limits](README_HISTORY.md#continuous-service-acceptance-and-limits)
- <a id="opt-in-live-external-synxflow-viewing-v1"></a>[Opt-in live external SynxFlow viewing V1](README_HISTORY.md#opt-in-live-external-synxflow-viewing-v1)
- <a id="recorded-playback-contract"></a>[Recorded playback contract](README_HISTORY.md#recorded-playback-contract)
- <a id="native-presentation-review-v1"></a>[Native presentation review V1](README_HISTORY.md#native-presentation-review-v1)
- <a id="native-waterbank-display-v1"></a>[Native water/bank display V1](README_HISTORY.md#native-waterbank-display-v1)
- <a id="native-matched-surface-study-v2-experimental"></a>[Native matched-surface study V2 (experimental)](README_HISTORY.md#native-matched-surface-study-v2-experimental)
- <a id="independent-native-surface-highlights-and-quiet-bank-review-v4"></a>[Independent native surface highlights and quiet bank review V4](README_HISTORY.md#independent-native-surface-highlights-and-quiet-bank-review-v4)
- <a id="native-rain-intensity-comparison"></a>[Native rain-intensity comparison](README_HISTORY.md#native-rain-intensity-comparison)
- <a id="river-v0"></a>[River V0](README_HISTORY.md#river-v0)
- <a id="sourcedrain-cell-highlights"></a>[Source/drain cell highlights](README_HISTORY.md#sourcedrain-cell-highlights)
- <a id="opt-in-source-to-outlet-scene"></a>[Opt-in source-to-outlet scene](README_HISTORY.md#opt-in-source-to-outlet-scene)
- <a id="opt-in-sustained-headwaters-control"></a>[Opt-in sustained-headwaters control](README_HISTORY.md#opt-in-sustained-headwaters-control)
- <a id="opt-in-immutable-mountain-sourceoutlet-scene"></a>[Opt-in immutable mountain source/outlet scene](README_HISTORY.md#opt-in-immutable-mountain-sourceoutlet-scene)
- <a id="numerical-contract"></a>[Numerical contract](README_HISTORY.md#numerical-contract)
- <a id="opt-in-finite-volume-comparison"></a>[Opt-in finite-volume comparison](README_HISTORY.md#opt-in-finite-volume-comparison)
- <a id="deterministic-fixtures"></a>[Deterministic fixtures](README_HISTORY.md#deterministic-fixtures)
- <a id="configuration"></a>[Configuration](README_HISTORY.md#configuration)
- <a id="continuous-mountain-rain-study"></a>[Continuous mountain rain study](README_HISTORY.md#continuous-mountain-rain-study)
- <a id="source-layout"></a>[Source layout](README_HISTORY.md#source-layout)
- <a id="next-implementation-gate"></a>[Next implementation gate](README_HISTORY.md#next-implementation-gate)
- <a id="experimental-display-only-bank-coverage"></a>[Experimental display-only bank coverage](README_HISTORY.md#experimental-display-only-bank-coverage)
- <a id="recorded-three-mode-bank-comparison"></a>[Recorded three-mode bank comparison](README_HISTORY.md#recorded-three-mode-bank-comparison)
- <a id="pre-existing-mountain-lake-experiment"></a>[Pre-existing mountain lake experiment](README_HISTORY.md#pre-existing-mountain-lake-experiment)
- <a id="render-only-visible-rainfall"></a>[Render-only visible rainfall](README_HISTORY.md#render-only-visible-rainfall)
- <a id="dry-start-rain-formed-collections"></a>[Dry-start rain-formed collections](README_HISTORY.md#dry-start-rain-formed-collections)
- <a id="equal-total-faster-rainfall-comparison"></a>[Equal-total faster rainfall comparison](README_HISTORY.md#equal-total-faster-rainfall-comparison)
- <a id="power-of-two-rainfall-study"></a>[Power-of-two rainfall study](README_HISTORY.md#power-of-two-rainfall-study)

</details>

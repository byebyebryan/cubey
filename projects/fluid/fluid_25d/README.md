# Fluid 2.5D — builtin shallow water

Cubey's standalone heightfield-water experiments, using our own Vulkan
virtual-pipes and opt-in finite-volume solvers. The retained River V0 and
source/outlet, lake-at-rest, drainage and transport fixtures are the starting
point for further bounded builtin work—not a claim of equivalent generated
mountain results.

The featured external terrain experiment is now the separate
[Terrain Water project](../../terrain_water/README.md). It owns the managed
SynxFlow demo and recorded presentation. Both frontends reuse existing source
components; no numerical method, material or hydraulic state is copied.

## Build and run

```sh
rtk proxy cmake --preset release
rtk proxy cmake --build --preset release --target cubey_project_fluid_25d -j 6
rtk proxy build/release/projects/fluid/fluid_25d/fluid_25d
```

No SynxFlow, CUDA worker, Python environment or Terrain Diffusion cache is
required for the authored builtin scene. Bare startup retains virtual-pipes,
River V0 and the established Original/legacy presentation. Source and outlet
highlights identify cell regions, not point boundaries. Diagnostic depth,
velocity, quiver and transport views remain available.

`--fluid25d-solver finite-volume` explicitly selects the retained finite-volume
alternative. Numerical oracles and CFL rejection checks remain authoritative for
their bounded fixtures; neither solver is promoted as a drop-in equivalent to
the external terrain setup. This project split adds no new solver research or
physics. Future work should choose a readable builtin case first, rather than
reopening the mountain-solver project implicitly.

For headless checks, add `--headless --frames 120 --output outputs/fluid/FRESH.png`.
Use the dedicated Terrain Water launcher for managed external computation.
Existing `fluid_25d --fluid25d-recording` / external-session commands remain
labelled compatibility entries, not silent fallbacks. No producer switch
transfers existing water.

## Terrain Water and historical showcase

The previously published clip belongs to Terrain Water, not this builtin
solver demonstration:

https://github.com/user-attachments/assets/453158a4-156c-4031-85ec-f6481780ea08

Its Joining valleys sunset film keeps the historical `fluid-25d` media names
and frozen recipe. No new film, solver equivalence, or publication is implied.
See [Terrain Water's quickstart](../../terrain_water/README.md),
[current behavior](../../terrain_water/current_reference.md) and
[explicit setup](../../terrain_water/demo_setup_v1.md).

## Development and retained evidence

The implementation and numerical tests remain under
`projects/fluid/sim/fluid_25d`. Shared terrain/water rendering lives in the
Cubey foundation. The dedicated [Terrain demo](../../terrain/README.md) consumes
saved landscapes without a running solver; it remains separate from live Terrain Water.

The [decision map](development_map.md) explains the earlier river-to-rain and
external-solver pivots; it is not a new builtin backlog.
[README_HISTORY.md](README_HISTORY.md) preserves the prior full development
record, and [presentation history](presentation_history.md) retains old recipes.
The compatibility [behavior reference](current_reference.md) redirects to the
current Terrain Water owner. The
[fluid architecture map](../../../docs/architecture/fluid-simulation.md) covers
the broader simulation family.

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

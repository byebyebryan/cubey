# Terrain Water — current behavior and boundaries

This is the Terrain Water reference, reconciled against the project split on 2026-10-15.
The builtin shallow-water demo remains in Fluid 2.5D; this project's preferred
producer is external SynxFlow. Legacy protocol/flag names remain compatible.
Start with the [quickstart](README.md); use the [decision map](../fluid/fluid_25d/development_map.md)
to understand why earlier studies describe different defaults. Dated reports are
evidence for their recorded build and inputs, not automatic current authority.

## Three entry points, three different defaults

| Entry | Producer and scene | Startup / purpose |
| --- | --- | --- |
| Prepared terrain launcher | Live external SynxFlow; Mountain, `patch-1024-16m` | Dry/paused; guided rain response on the qualified bed; preferred interactive demo. |
| Bare application, no input flags (Fluid 2.5D) | Builtin virtual-pipes solver, authored diagnostic scene | Standalone alternative; not the featured mountain result or the launcher's visual defaults. |
| Published highlight | Recorded Joining valleys, 1024² / 16 m, continuous rain | Eight-second dry-to-streams-to-lake sunset edit; no running solver. |

The dedicated `terrain_water` native frontend requires a recording/session or an
explicit builtin diagnostic selection; no-argument startup never silently picks
a producer. The old `fluid_25d` external/replay entry is compatibility-only.
The canonical launcher is `projects/terrain_water/run_terrain_rain_demo.py`.

The launcher explicitly selects Scenic Macro, correlated terrain, B-spline 2x
banks, scene-fitted shadows, visible rain, dots/trails and a 60 Hz render cap.
The bare application's retained defaults are Original presentation and legacy
terrain/shadow settings; selecting Scenic directly starts its refined material.
Do not infer application-wide promotions from launcher choices.

## Reading and controlling the simulation

Rain supplies every simulation cell. Gravity and the unchanged bed determine
runoff and collection. There is no point-source marker or authored drain in the
featured scene; the perimeter uses the native open/fall boundary. The terrain is
generated, not surveyed, and 512 mm/h is exaggerated demonstration forcing.

Start/Space on the qualified dry Mountain bed requests 512 mm/h at 60x, follows
runoff/collection cameras, and turns rain off after two physical hours. Guidance
ends at six hours **without pausing or resetting**. Manual mode has no scheduled
rain shutoff. Water can continue gathering after Rain Off as upstream runoff
arrives; retained water is not proof of a permanent or fully settled lake.

Manual rain/pace changes, camera selection or orbit/zoom exit the guide while
retaining the water. Reset creates dry/paused water and retains the selected mode
and applied rain/pace. Guided Start restores rain; Manual Start after a rain-off
reset needs a positive rain preset. Other sites/grids use ordinary Manual start,
not an unqualified copy of the Mountain script. The live worker defaults to
continuous mode; the prepared 20-hour input horizon applies to explicit `--finite`
diagnostics, not a live hard stop. Continuous still has finite float-clock precision.

Three clocks must stay distinct:

| Setting / signal | What it changes | What it does not mean |
| --- | --- | --- |
| Rain Off / 128 / 256 / 512 / 1024 mm/h | Actual water input to the external worker | Not playback speed or cosmetic streak density. |
| Requested simulation pace, normally 60x | Target physical seconds per wall second | Not guaranteed throughput; fine grids can fall behind. |
| Render cap, normally 60 Hz | Viewer wall-time pacing and shared-GPU contention | Not 60 hydraulic updates per second or measured monitor FPS. |

Recorded playback has a separate viewing clock. A saved state is held between
native saves, not physically interpolated. Rain, foam, dots and surface detail
are approximate visual motion cues, not additional hydraulic states, conserved
dye or native particles. Shared solar time is also render-only; the cinematic
sunset is authored independently of the hydraulic timeline.

If the UI says **recorded SynxFlow**, that viewer is replaying fields, not running
the solver. Use the launcher's live mode for actual computation; **Backend details**
shows the selected adapter/input identity. A recorded native result and a live
stream can share the same renderer without being the same execution mode.

## Sites, spacing and replacement

The default [curated catalogue](../fluid/fluid_25d/terrain_water_sites.json) offers six sites and
five grid presets. Missing or mismatched prepared cases remain disabled; setup
does not silently generate terrain or prepare a complete batch.

| Configuration | Cells | Spacing | Physical square |
| --- | ---: | ---: | ---: |
| `patch-512-32m` | 512² | 32 m | 16.384 km |
| `patch-1024-16m` | 1024² | 16 m | 16.384 km |
| `patch-2048-8m` | 2048² | 8 m | 16.384 km |
| `small-512-16m` | 512² | 16 m | 8.192 km |
| `large-2048-16m` | 2048² | 16 m | 32.768 km |

Apply & restart replaces the worker/session identity and starts dry/paused. It is
selection plus reset, not instantaneous regridding, hot-swapping water between
solvers, or editing terrain beneath existing water. Elevation is sourced at
30 m; finer cells improve sampling/simulation/display, not source detail.
The explicit [prepared-world collection](../fluid/fluid_25d/world_water_v1.md) is separate from
the default curated catalogue and retains its own source/coverage admission.

## Solver contract and ownership

| Backend | Available role | Important boundary |
| --- | --- | --- |
| External service | Real producer pause/resume/reset/rain/pace, acknowledged through the local protocol | Separate SynxFlow CUDA process and qualified native artifact; Cubey remains a Vulkan client. |
| Recording | Playback pause/rate/seek/restart | Changes viewing time, never the saved hydraulics. |
| Builtin | Existing GPU virtual-pipes and opt-in finite-volume solver; diagnostic scenarios | Retained simpler alternative, not equivalent to tested mountain SynxFlow results; generic numeric rain/single-step capabilities are not advertised. |
| Legacy stock-file bridge | External field viewing | Not the controlled service; does not acquire its solver controls. |

The [backend contract](../fluid/sim/fluid_25d/fluid_25d_backend_contract.h) separates
producer controls from playback capabilities, with versioned grid/input identity,
generation, frame/time and command acknowledgement checks. There is one controller
and at most one outstanding command. Stale generations, regressing frames and
duplicate acknowledgements are rejected. Conflicting backend/input flags fail;
there is no silent fallback or water-state transfer.

The service downloads fields to bounded local binary slots (h, qx, qz), publishes
atomically, and Cubey validates/stages/uploads them to Vulkan. This is intentional
CPU-staged transport, **not Vulkan–CUDA allocation interop**. It avoids importing
or porting the external solver into Cubey. Managed launcher shutdown reaps its
owned worker; closing a separately attached standalone viewer only detaches it.
See [setup and licensing boundaries](demo_setup_v1.md),
[managed lifecycle](../fluid/fluid_25d/terrain_demo_ux_v1.md) and the
[retained implementation evidence](../fluid/fluid_25d/README_HISTORY.md#project-local-backend-contract).

## Accepted rendering versus retained options

| Prepared-demo default | What it achieves / retained alternative |
| --- | --- |
| Shared correlated terrain material | Source-aligned landform/climate organization and footprint-filtered detail. Climate is long-term substrate potential, not current weather or a biome simulation. |
| Paired B-spline 2x ground/water display | Smooths cell-scale banks, including continuous wet-ground coverage. Does not change native bed/water or create finer physical resolution. |
| Scene-fitted 2048² whole-patch shadows | Corrected steep-receiver bias and stable caster coverage; not cascades or unlimited close-up detail. |
| Rough rain/flow water, depth-limited shallow absorption | Quieter highlights and darker small streams without globally blackening deep water. Artistic optics, not sediment simulation. |
| Rapid flecks and subtle, irregular gentler foam | Motion cues, not resolved rapids, overturning water, waterfalls or spray physics. |
| Bank noise off | B-spline alone is the accepted baseline; fixed/moving bank irregularity remains an optional visual tradeoff. |
| Fixed daylight, no clouds | Optional shared clear-sky/day-night remains render-only. The surface cloud model is not suitable for this elevated view. |

Reference banks and bounded recorded marching-squares coverage remain comparison
options, not universally better shorelines. Marching squares uses a prepared
recording cache, not live contour reconstruction. Readable, flow inspection and
raw depth/velocity/wet-dry views remain useful diagnostics; Scenic color is not a
depth palette. Rain streak strength/fall speed affect appearance, not water input.
The cinematic soft-rain preset is not a global interactive default.

See [terrain presentation](../fluid/fluid_25d/terrain_presentation_v1.md),
[water/material lineage](../fluid/fluid_25d/scenic_rendering_v1.md) and the
[bank-edge record](../../docs/notes/fluid25d-bank-edge-v1.md).

## Performance: what was actually established

The corrected [offscreen V2 study](../fluid/fluid_25d/offscreen_perf_v2.md) measured real Vulkan
render-only operation without X11/Xvfb, image readback, encoding or presentation.
Frozen Release/RTX 5070 Ti, 1080p, 60 Hz-cap Mountain measurements included:

| Case / bounded window | Achieved physical pace | Viewer CPU service p95 | Publication age p95 |
| --- | ---: | ---: | ---: |
| 1k / 16 m, 45 s | 59.97x | 2.80 ms | 0.140 s |
| 2k / 8 m, 45 s | 3.88x | 15.16 ms | 0.315 s |
| 1k / 16 m, ten-minute soak | 59.99x | 2.70 ms | 0.143 s |

The soak's GPU presentation p95 was 5.29 ms. Unpaced rendering can starve the
worker; the 60 Hz cap is purposeful. These are historical, workload-specific
measurements, not current-release benchmarks or universal 60 FPS guarantees.
Native Wayland interaction/scanout, hardware-offscreen timing, capture/encode and
human visual acceptance are separate evidence. Earlier V1 Xvfb frame timings
must not be quoted as native desktop FPS.

<a id="fluid-versus-the-shared-terrain-foundation"></a>
## Terrain Water versus the shared Terrain foundation

Terrain Water shows only its selected full-resolution simulation patch: no exterior
ground, outer water splice or world overview. Terrain owns wider backdrop/LOD
composition and its separate opt-in captured-water consumer. A validated rain-on
capture can render there without a running solver or CUDA, using matching paired
ground/water reconstruction. Wider ground is not automatically wider water.
See [Terrain's current guide](../terrain/README.md) for that consumer's actual
coverage and setup; this does not promote water into every terrain-only view.

This remains a macro rendering demo: no erosion, channel carving, groundwater,
snowmelt model, calibrated catchment hydrology, vegetation or ground-level detail.
Keep the external solver integration rather than reopening a solver port or
river-generation project by implication.

## Source owners when behavior changes

- [Launcher defaults and admission](run_terrain_rain_demo.py),
  [native producer selection](terrain_water_config.h) and
  [shared application options](../fluid/fluid_25d/fluid_25d_project_config.h).
- [Guide/control state machine](../fluid/sim/fluid_25d/fluid_25d_terrain_demo.cpp).
- [Backend capabilities](../fluid/sim/fluid_25d/fluid_25d_backend_contract.h) and
  [builtin numerical configuration](../fluid/sim/fluid_25d/fluid_25d_config.h).
- [Shared terrain/water Macro material](../../src/cubey/render/terrain_surface_patch.cpp),
  reused by Fluid and Terrain's captured-water consumer.
- [Published recipe](../../docs/media/showcase/fluid-25d-recipe.json) and
  [separate publication receipt](../../docs/media/showcase/fluid-25d-publication.json).

Update this reference and the quickstart when these contracts change. Keep dated
study outcomes and immutable media provenance intact rather than relabelling
earlier failures or pending-review snapshots as current results.

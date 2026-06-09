# Planet

`planet` is the foundation project for planet-scale rendering experiments. The
current version opens a window or headless capture path, renders a cube-sphere
planet surface through a procedural terrain-field contract, owns the local
sky/celestial state, and provides the target project boundary for future
terrain, ocean, clouds, and streaming integration.

Run it with:

```sh
./build/dev/projects/planet/planet
./build/dev/projects/planet/planet --headless --frames 120 --output outputs/planet.png
./build/dev/projects/planet/planet --planet-scale-preset earthlike
./build/dev/projects/planet/planet --planet-scale-preset mini
./build/dev/projects/planet/planet --debug-view lod-level
./build/dev/projects/planet/planet --debug-view cell-edge
./build/dev/projects/planet/planet --debug-view terrain-height
./build/dev/projects/planet/planet --debug-view terrain-band-base
./build/dev/projects/planet/planet --debug-view terrain-band-relief
./build/dev/projects/planet/planet --debug-view terrain-band-detail
./build/dev/projects/planet/planet --debug-view terrain-slope
./build/dev/projects/planet/planet --debug-view terrain-material
./build/dev/projects/planet/planet --debug-view lod-transition
./build/dev/projects/planet/planet --debug-view bathymetry
./build/dev/projects/planet/planet --debug-view shoreline
./build/dev/projects/planet/planet --debug-view land-mask
./build/dev/projects/planet/planet --debug-view moisture
./build/dev/projects/planet/planet --debug-view temperature
./build/dev/projects/planet/planet --debug-view roughness
./build/dev/projects/planet/planet --debug-view wireframe
./build/dev/projects/planet/planet --debug-view celestial-planes
./build/dev/projects/planet/planet --debug-view local-detail-wireframe
./build/dev/projects/planet/planet --debug-view local-detail-blend
./build/dev/projects/planet/planet --debug-view local-detail-lod
./build/dev/projects/planet/planet --debug-view local-detail-height
./build/dev/projects/planet/planet --debug-view local-detail-features
./build/dev/projects/planet/planet --debug-view local-detail-final
./build/dev/projects/planet/planet --debug-view local-detail-horizon
./build/dev/projects/planet/planet --debug-view seams
./build/dev/projects/planet/planet --planet-atmosphere-mode physical
./build/dev/projects/planet/planet --planet-atmosphere-haze-strength 0.18 --planet-atmosphere-aerial-strength 0.35
./build/dev/projects/planet/planet --planet-max-lod-level 7 --planet-lod-target-edge-px 8
./build/dev/projects/planet/planet --planet-max-lod-level 12 --planet-patch-resolution 128 --planet-lod-target-edge-px 6
./build/dev/projects/planet/planet --planet-local-detail-lod-levels 6 --planet-local-detail-cells 128 --planet-local-detail-outer-extent-m 8192
./build/dev/projects/planet/planet --planet-local-detail-height-m 220 --planet-local-detail-scale-m 180
./build/dev/projects/planet/planet --no-planet-local-detail
./build/dev/projects/planet/planet --planet-terrain-mid-detail-strength 0.58 --planet-terrain-fine-detail-strength 0.12 --planet-terrain-fine-detail-scale 12
./build/dev/projects/planet/planet --planet-sea-level-m 0 --planet-shoreline-width-m 1500
```

For repeatable atmosphere and celestial-body captures, keep the solar clock
paused and pin both time and camera mode:

```sh
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 5.5 --planet-camera-mode surface --output outputs/planet-surface-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 12.0 --planet-camera-mode surface --output outputs/planet-surface-day.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 0.0 --planet-camera-mode surface --output outputs/planet-surface-night.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 5.5 --planet-camera-mode orbit --output outputs/planet-orbit-dawn.png
./build/dev/projects/planet/planet --headless --frames 2 --width 1280 --height 720 --planet-pause-time --planet-day-of-year 80 --planet-time-hours 18.0 --planet-camera-mode orbit --output outputs/planet-orbit-backlit.png
```

The broader manual capture matrix is tracked in
[`docs/notes/planet-visual-captures.md`](../../docs/notes/planet-visual-captures.md).

## Status

| Area | State |
| --- | --- |
| Planet frame/camera | Done as v1: double-precision camera position, camera-relative GPU rendering, datum height, terrain height, and terrain-relative clearance are explicit. |
| Surface LOD | Done as v1: coverage-first cube-sphere patches, live instance-buffer uploads, hysteresis, single-step neighbor repair, terrain-aware screen-error bounds, and wire/debug diagnostics. |
| Terrain field | Active procedural contract: CPU/shader sampling share height, named terrain bands, normal, water depth, shoreline, material, climate, roughness, and tile-summary vocabulary. It is not final art direction or streamed data. |
| Local detail clipmap | Near-field surface layer: altitude-gated bounded local detail contributes to `final` surface view and can be inspected in local-detail and terrain-field views, with `local-detail-horizon` reserved for horizon-scale/full-range inspection. Local/global morphing, persistent topology, streaming, and ocean payloads remain deferred. |
| Sky/celestial/atmosphere | Done as v1: shared mean solar clock/celestial mechanics, shared sky frame with night-sky atlas sampling, depth-tested moon body geometry, project-local physical atmosphere preview, HDR post, and view-aware exposure. Full LUT/transmittance atmosphere and true ephemeris remain deferred. |
| Streaming/cache | Deferred: current patch replans and lazy uploads are not an out-of-core streamer. Parent coverage remains renderable while future child/tile data is prepared. |
| Ocean integration | Deferred: `projects/ocean` stays local-water focused until planet frame, LOD, terrain, and local-detail contracts are ready to host it as one surface layer. |
| Config ownership | Deferred cleanup: planet still consumes shared `RunConfig`; a project-owned CLI/config facade should be extracted when the next project repeats this pressure. |

Supported debug views are `final`, `face-id`, `patch-id`, `lod-level`,
`screen-error`, `lod-transition`, `seams`, `cell-edge`, `terrain-height`,
`terrain-band-base`, `terrain-band-relief`, `terrain-band-detail`,
`terrain-slope`, `terrain-material`, `bathymetry`, `shoreline`, `land-mask`,
`moisture`, `temperature`, `roughness`, `wireframe`, `celestial-planes`,
`local-detail-wireframe`, `local-detail-blend`, `local-detail-lod`,
`local-detail-height`, `local-detail-features`, `local-detail-final`, and
`local-detail-horizon`.
`celestial-planes` colors the equator, ecliptic, and lunar orbit great circles
plus sub-solar/sub-lunar markers for validating the mean celestial model.
Windowed controls are applied live where possible. Left drag orbits the planet,
right drag looks around in surface mode, scroll changes camera distance, and
WASD moves the surface camera. Orbit dragging clamps just short of the poles to
avoid north/south direction flips. The control panel is split into a
project-local UI adapter (`PlanetUi`) so runtime state, surface planning, and
ImGui layout do not keep growing in the app shell.
The CPU LOD planner selects camera-relative cube-sphere patch instances by
projected edge size and reports patch, LOD, refinement cull, screen-error,
transition pressure, edge-length, per-LOD cell-size, budget fallback,
hysteresis, and skirt ranges in the UI. The live renderer draws those selected
patches with one reusable GPU
patch grid plus per-frame-slot instance buffers carrying `face/level/x/y`
identity. Live instanced rendering supports up to LOD 12 and patch resolution
128, defaults to LOD 8, patch resolution 32, and a 6 px target edge, and falls
back to coarser patch coverage when interactive settings would exceed the live
patch-instance budget. The previous LOD 12 / 64 grid default was useful for
stress testing but made near-surface navigation GPU-bound on midrange hardware.
Surface-mode planning now raises the base globe target toward 14 px so close
navigation uses the global cube-sphere for continuous coverage rather than
trying to make it the meter-scale detail layer. Orbit and high-altitude views
still use the configured target.
The CPU mesh builder has a stricter vertex cap because it materializes every
selected patch for diagnostics.
The live surface path is now mediated by `PlanetSurfaceRuntime`, which owns the
current patch plan, previous-selection hysteresis history, render-origin
validity checks, diagnostics, and lazy per-frame instance-buffer uploads.

The default target scale is Earth-like. A smaller mini-planet preset remains a
debug option for quickly inspecting curvature, LOD colors, and celestial motion,
but scale-sensitive work should be checked against the Earth-like preset. The
global cube-sphere patch tree is responsible for coverage and stable
`face/level/x/y` identity. Meter-scale terrain detail and future ocean waves are
expected to come through a viewer-centered local-detail clipmap rather than by
forcing the global patch tree to carry every wave or interaction feature. The
local-detail layer is centered on `PlanetFrame.local_frame`, defaults to six
clipmap levels, 128 cells per axis, an 8192 m outer half extent, and a 4 m near
cell. Runtime planning selects only the levels whose cell size is large enough
to matter in the current view; orbit-scale cameras allocate no local-detail
mesh, mid-altitude views start from a coarser center patch, and close surface
views can activate the full fine range. In v1 it renders as an explicit bounded
diagnostic/inspection path and reports active level range, patch, vertex,
triangle, cell-size, projected-cell, surface weight, and blend diagnostics in
the UI. Local-detail debug views and terrain-field debug views can use the local
detail surface when terrain, local detail, and near-surface camera blending are
active. The default `final` view now consumes the bounded local-detail surface
near the ground so surface view has visible terrain features instead of relying
only on the global cube-sphere field. Persistent topology, a real local/global
morph, cache, streaming, and ocean payloads are still deferred.
The local displacement is now a semantic residual over the global terrain field:
ridge uplift, channel cuts, and plain undulation are gated by the same
continent, relief, mountain, valley, plain, and land signals that shape the
global terrain instead of by an unrelated local noise stack.
`local-detail-wireframe` shows the near-field clipmap ownership, `local-detail-blend`
shows the active ownership/cutout mask, and `local-detail-height` isolates the
added detail displacement. `local-detail-features` colors the semantic local
ridge, channel, and plain residuals that feed that displacement.
`local-detail-final` renders the same shaded material path as `final` while
forcing the bounded local-detail surface as an inspection view.
`local-detail-horizon` is the full-range diagnostic: it expands the clipmap out
to the horizon-scale inspection extent and uses the global cutout to make local
coverage and ownership boundaries obvious.

Planet surface LOD is coverage-first. Root patches provide guaranteed coarse
coverage for every planet domain, and view/horizon culling only stops
refinement; it does not remove the fallback surface. When a patch refines, it
hands off its full area to child subtrees, so the renderer never draws a parent
and child for the same domain at the same time. This keeps camera rotation from
revealing empty holes while patch replans are deferred during dragging.
Previous patch selections feed a small split/merge hysteresis deadband so
camera-driven replans do not churn at the exact LOD threshold; `lod-transition`
plus the UI counters show where patches are near or held around that boundary.

Patch identity is explicit: each selected surface instance has a `face/level/x/y`
address, and UV bounds are derived from that address plus the root
`patches_per_face` setting. This keeps LOD addressing independent of mesh
construction and creates stable keys for later terrain, bathymetry, cache, or
streaming work.

The terrain controls are procedural contract pressure, not the final terrain
system or final art direction. Terrain now goes through a project-local
surface-field contract: CPU and shader paths sample deterministic height, world
position, normal, height above sea level, water depth, normalized bathymetry,
shoreline mask, land mask, normalized elevation, normalized slope, moisture,
temperature, roughness, simple material bands, and named terrain frequency
bands for base shape, broad relief, mid-detail ridges/valleys, and fine residual
detail. The live renderer displaces the reusable grid in the vertex shader with
deterministic multi-band terrain: domain-warped continent/ocean structure,
lowland breakup, ridge belts, valley cuts, and land/relief-gated fine detail.
Normals are recomputed from a patch-cell-scaled sample step so
higher LOD reveals smaller terrain features instead of only smoothing the mesh.
The CPU mesh builder and tile payload path remain as diagnostic/test paths for
the same patch contracts.

The current material bands are intentionally simple: water, lowland, highland,
and snow. Water is classified from explicit sea level rather than a normalized
elevation threshold. The bathymetry and shoreline fields are diagnostic
contracts for future terrain/ocean handoff; they are not yet streamed data,
seafloor rendering, surf, biome masks, or final art direction.

Surface tile payload summaries now include terrain height ranges, sea-level
height ranges, water depth, shoreline mask, land/water/shoreline coverage,
averaged height, averaged height above sea level, averaged moisture,
temperature, roughness, normalized slope, dominant material, and material
counts. The procedural generator revision is derived from terrain-relevant
config rather than only the seed, so future cache invalidation has a stable
boundary.

The terrain v2 direction is documented in
[`docs/notes/planet-terrain-field-v2.md`](../../docs/notes/planet-terrain-field-v2.md).
It keeps the source procedural and project-local while making the sample and
tile summary vocabulary explicit enough for later ocean, biome, cache, and
streaming work.

`planet` is now the first consumer of the shared sky/celestial foundation. The
mean solar clock, Earth-like sun/moon mechanics, exposure helpers, fullscreen sky
frame, and celestial-body frame live under `cubey::render`; `projects/planet`
keeps only the project adapters for run config, UI, terrain frame data, and the
planet-specific atmosphere mode enum. That shared state resolves sun and moon
directions, physical radii, angular radii, direct lighting, ambient lighting, and
the sky pass.
The clock is a mean Earth-like model: UI time is a 24h mean solar day, internal
planet spin uses a 23.9345h sidereal rotation, the seasonal year is 365.2422d,
and the moon uses a 27.321661d sidereal orbit with derived 29.53d phase
cycling. The lunar orbit has an explicit phase epoch offset because the demo
`day_of_year` clock is seasonal, not a dated real ephemeris; the default starts
the spring dawn preset near full moon instead of keeping the moon close to the
sun in daylight. Eccentricity, equation of time, lunar apsidal/nodal precession,
and true Earth/Moon barycentric motion are deferred until the planet project
needs that fidelity.

The current shared sky pass renders dark space, generated night-sky/Milky Way
atlas content plus sparse procedural stars, a sun disk/glow, and a local planet
limb. The default `physical` atmosphere mode uses a small project-local
single-scattering model with Rayleigh/Mie vocabulary, sun transmittance, and
surface aerial perspective. The older `analytic` mode remains selectable for
comparison and debugging. The moon is now a
depth-tested sphere rendered from the same local celestial state on a
camera-relative shell that preserves its apparent angular size. Phase and
terminator shape therefore come from body lighting against the modeled sun
direction instead of a sky-disk mask. The body pass uses premultiplied blending
for phase coverage and daylight sky washout, but placement and planet
occlusion remain geometric/depth-tested rather than sky-sprite ownership.
The sky pass masks procedural stars behind the full rendered moon disk, so the
unlit half can blend into smooth sky without letting stars shine through it.
Night-side terrain receives a small phase-scaled secondary moonlight term. True
node-aware lunar eclipses remain deferred. The surface shader receives frame
data through a descriptor-backed uniform instead of push constants, and
composes final terrain through atmosphere before post. The scene renders into a
linear HDR scene color target and uses the shared fullscreen post pass for tone
mapping and output encoding before writing the swapchain or headless target.
Unless `--exposure` is set explicitly or `--no-auto-exposure` is used, planet
resolves display exposure from the visible disk light fraction in orbit mode,
the local sun elevation in surface mode, and blends between those references
through the camera transition. This is a stable v1 proxy, not a true
view-luminance histogram. Daylight, twilight, and night exposure targets remain
separately tunable in the UI.
Surface haze is intentionally exposed as live planet config: `Surface Haze`,
`Haze Start`, and `Haze End` tune the analytic distance-haze fallback, while
`Aerial Strength` blends the physical aerial perspective path. These are
art-direction controls over the current v1 atmosphere approximation, not full
Rayleigh/Mie density or LUT parameters.

This is not yet a real async streamer. Camera-driven patch replans refresh CPU
patch data and lazily upload each frame slot's instance buffer the next time it
is rendered, so ordinary navigation no longer blocks on `vkDeviceWaitIdle`.
Full configuration rebuilds still synchronize because patch grid topology can
change. Future streaming should keep the same contract: parent patches remain
renderable until all child coverage needed for a refinement is built and
uploaded.

The test suite includes planet headless PNG smoke coverage for baseline
headless output, surface dawn/day/night, orbit lit/terminator, daytime moon,
wireframe LOD views, one terrain-field diagnostic (`moisture`), and local-detail
diagnostic views. These tests are intentionally coarse image-stat checks, not
golden-image comparisons, but they keep the main visual paths from silently
going black or empty.

This project should stay focused on planet-scale contracts first. Ocean scale
work remains in `projects/ocean` until the planet frame, LOD, and world-space
contracts are stable enough to port it cleanly. The local-detail terrain pass is
the first consumer of the planet-local clipmap; future ocean, wake, shoreline,
and interaction data should attach to the same local frame instead of creating a
second planet-scale mesh system.

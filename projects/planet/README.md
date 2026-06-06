# Planet

`planet` is the foundation project for planet-scale rendering experiments. The
current version is intentionally small: it opens a window or headless capture
path, renders a cube-sphere planet surface with placeholder terrain, draws the
project-owned local sky behind it, and provides the target project boundary
for future terrain, ocean, clouds, celestial bodies, and streaming integration.

Run it with:

```sh
./build/dev/projects/planet/planet
./build/dev/projects/planet/planet --headless --frames 120 --output outputs/planet.png
./build/dev/projects/planet/planet --planet-radius-m 600000 --planet-camera-altitude-m 240000
./build/dev/projects/planet/planet --debug-view lod-level
./build/dev/projects/planet/planet --debug-view cell-edge
./build/dev/projects/planet/planet --debug-view terrain-height
./build/dev/projects/planet/planet --debug-view terrain-slope
./build/dev/projects/planet/planet --debug-view terrain-material
./build/dev/projects/planet/planet --debug-view lod-transition
./build/dev/projects/planet/planet --debug-view bathymetry
./build/dev/projects/planet/planet --debug-view shoreline
./build/dev/projects/planet/planet --debug-view wireframe
./build/dev/projects/planet/planet --debug-view celestial-planes
./build/dev/projects/planet/planet --debug-view seams
./build/dev/projects/planet/planet --planet-atmosphere-mode physical
./build/dev/projects/planet/planet --planet-max-lod-level 7 --planet-lod-target-edge-px 8
./build/dev/projects/planet/planet --planet-max-lod-level 9 --planet-patch-resolution 128 --planet-lod-target-edge-px 6
./build/dev/projects/planet/planet --planet-terrain-mid-detail-strength 0.45 --planet-terrain-fine-detail-strength 0.16 --planet-terrain-fine-detail-scale 12
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

Supported debug views are `final`, `face-id`, `patch-id`, `lod-level`,
`screen-error`, `lod-transition`, `seams`, `cell-edge`, `terrain-height`,
`terrain-slope`, `terrain-material`, `bathymetry`, `shoreline`, `wireframe`,
and `celestial-planes`.
`celestial-planes` colors the equator, ecliptic, and lunar orbit great circles
plus sub-solar/sub-lunar markers for validating the mean celestial model.
The CPU LOD planner selects camera-relative cube-sphere patch instances by
projected edge size and reports patch, LOD, refinement cull, screen-error,
transition pressure, edge-length, per-LOD cell-size, budget fallback,
hysteresis, and skirt ranges in the UI. The live renderer draws those selected
patches with one reusable GPU
patch grid plus per-frame-slot instance buffers carrying `face/level/x/y`
identity. Live instanced rendering supports up to LOD 9 and patch resolution
128, defaults to LOD 8, patch resolution 64, and a 6 px target edge, and falls
back to coarser patch coverage when interactive settings would exceed the live
patch-instance budget. The CPU mesh builder has a stricter vertex cap because
it materializes every selected patch for diagnostics.

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

The terrain controls are placeholders for contract pressure, not the final
terrain system. Terrain now goes through a project-local surface-field contract:
CPU and shader paths sample deterministic height, world position, normal,
height above sea level, water depth, normalized bathymetry, shoreline mask,
normalized elevation, normalized slope, and a simple material band. The live
renderer displaces the reusable grid in the vertex shader with deterministic
multi-band terrain: broad shape, mid ridges, and fine detail. Normals are
recomputed from a patch-cell-scaled sample step so higher LOD reveals smaller
terrain features instead of only smoothing the mesh. The CPU mesh builder
remains as a diagnostic/test path for the same patch contracts.

The current material bands are intentionally simple: water, lowland, highland,
and snow. Water is classified from explicit sea level rather than a normalized
elevation threshold. The bathymetry and shoreline fields are diagnostic
contracts for future terrain/ocean handoff; they are not yet streamed data,
seafloor rendering, surf, biome masks, or final art direction.

`planet` now owns its sky and celestial state locally. The shared atmosphere
background/runtime is no longer used by this project because its demo-oriented
sun/moon disk and clock assumptions were fighting the planet-viewer contract.
A project-local solar clock drives planet orbit, planet self-rotation, and moon
orbit. That state resolves sun and moon directions, physical radii, angular
radii, direct lighting, ambient lighting, and the planet-owned sky pass.
The clock is a mean Earth-like model: UI time is a 24h mean solar day, internal
planet spin uses a 23.9345h sidereal rotation, the seasonal year is 365.2422d,
and the moon uses a 27.321661d sidereal orbit with derived 29.53d phase
cycling. The lunar orbit has an explicit phase epoch offset because the demo
`day_of_year` clock is seasonal, not a dated real ephemeris; the default starts
the spring dawn preset near full moon instead of keeping the moon close to the
sun in daylight. Eccentricity, equation of time, lunar apsidal/nodal precession,
and true Earth/Moon barycentric motion are deferred until the planet project
needs that fidelity.

The current sky pass renders dark space, sparse procedural stars, a sun
disk/glow, and a local planet limb. The default `physical` atmosphere mode uses
a small project-local single-scattering model with Rayleigh/Mie vocabulary, sun
transmittance, and surface aerial perspective. The older `analytic` mode
remains selectable for comparison and debugging. The moon is now an opaque,
depth-tested sphere rendered from the same local celestial state on a
camera-relative shell that preserves its apparent angular size. Phase and
terminator shape therefore come from body lighting against the modeled sun
direction instead of a sky-disk mask. The atmosphere can wash out the moon's
contrast in daylight, but it does not make the moon transparent; night-side
terrain receives a small phase-scaled secondary moonlight term. True
node-aware lunar eclipses remain deferred. The surface shader receives frame
data through a descriptor-backed uniform instead of push constants, and
composes final terrain through atmosphere before post. The scene renders into a
linear HDR scene color target and uses the shared fullscreen post pass for tone
mapping and output encoding before writing the swapchain or headless target.
Unless `--exposure` is set explicitly or `--no-auto-exposure` is used, planet
resolves display exposure from the visible disk light fraction in orbit mode,
the local sun elevation in surface mode, and blends between those references
through the camera transition. Daylight, twilight, and night exposure targets
remain separately tunable in the UI.

This is not yet a real async streamer. Camera-driven patch replans refresh CPU
patch data and lazily upload each frame slot's instance buffer the next time it
is rendered, so ordinary navigation no longer blocks on `vkDeviceWaitIdle`.
Full configuration rebuilds still synchronize because patch grid topology can
change. Future streaming should keep the same contract: parent patches remain
renderable until all child coverage needed for a refinement is built and
uploaded.

This project should stay focused on planet-scale contracts first. Ocean scale
work remains in `projects/ocean` until the planet frame, LOD, and world-space
contracts are stable enough to port it cleanly.

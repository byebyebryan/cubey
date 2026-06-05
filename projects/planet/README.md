# Planet

`planet` is the foundation project for planet-scale rendering experiments. The
current version is intentionally small: it opens a window or headless capture
path, renders a cube-sphere planet surface with placeholder terrain, draws the
shared procedural atmosphere behind it, and provides the target project boundary
for future terrain, ocean, clouds, and streaming integration.

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
./build/dev/projects/planet/planet --debug-view seams
./build/dev/projects/planet/planet --planet-max-lod-level 7 --planet-lod-target-edge-px 8
./build/dev/projects/planet/planet --planet-max-lod-level 9 --planet-patch-resolution 128 --planet-lod-target-edge-px 6
./build/dev/projects/planet/planet --planet-sea-level-m 0 --planet-shoreline-width-m 1500
```

Supported debug views are `final`, `face-id`, `patch-id`, `lod-level`,
`screen-error`, `lod-transition`, `seams`, `cell-edge`, `terrain-height`,
`terrain-slope`, `terrain-material`, `bathymetry`, `shoreline`, and `wireframe`.
The CPU LOD planner selects camera-relative cube-sphere patch instances by
projected edge size and reports patch, LOD, refinement cull, screen-error,
transition pressure, edge-length, per-LOD cell-size, budget fallback,
hysteresis, and skirt ranges in the UI. The live renderer draws those selected
patches with one reusable GPU
patch grid plus per-frame-slot instance buffers carrying `face/level/x/y`
identity. Live instanced rendering supports up to LOD 9 and patch resolution
128, defaults to LOD 7, patch resolution 64, and an 8 px target edge, and falls
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

Atmosphere is now part of the planet frame instead of a separate visual spike.
The project uses the shared atmosphere run state, generated moon/night-sky atlas
textures, and background renderer. Planet radius, atmosphere height, camera
altitude, horizon distance, sun direction, and surface lighting are resolved
from the same frame. The UI exposes the shared atmosphere controls collapsed by
default plus read-only diagnostics for time, sun position, camera altitude,
horizon, and generated atlas status. The surface shader receives frame data
through a descriptor-backed uniform instead of push constants, and blends final
terrain toward atmosphere-tinted haze near the horizon. The scene renders into
a linear HDR scene color target and uses the shared fullscreen post pass for
exposure, tone mapping, and output encoding before writing the swapchain or
headless target.

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

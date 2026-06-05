# Planet

`planet` is the foundation project for planet-scale rendering experiments. The
current version is intentionally small: it opens a window or headless capture
path, renders a cube-sphere debug planet surface, and provides the target
project boundary for future terrain, atmosphere, and ocean integration.

Run it with:

```sh
./build/dev/projects/planet/planet
./build/dev/projects/planet/planet --headless --frames 120 --output outputs/planet.png
./build/dev/projects/planet/planet --planet-radius-m 600000 --planet-camera-altitude-m 240000
./build/dev/projects/planet/planet --debug-view lod-level
./build/dev/projects/planet/planet --debug-view cell-edge
./build/dev/projects/planet/planet --debug-view terrain-height
./build/dev/projects/planet/planet --debug-view seams
./build/dev/projects/planet/planet --planet-max-lod-level 5 --planet-lod-target-edge-px 10
```

Supported debug views are `final`, `face-id`, `patch-id`, `lod-level`,
`screen-error`, `seams`, `cell-edge`, and `terrain-height`. The CPU LOD planner
selects camera-relative cube-sphere patch instances by projected edge size and
reports patch, LOD, refinement cull, screen-error, edge-length, per-LOD
cell-size, and skirt ranges in the UI. The live renderer draws those selected
patches with one reusable GPU patch grid plus per-frame-slot instance buffers
carrying `face/level/x/y` identity. Live instanced rendering supports up to LOD
6, defaults to LOD 5 with a 10 px target edge, and rejects selections above the
live patch-instance budget. The CPU mesh builder has a stricter vertex cap
because it materializes every selected patch for diagnostics.

Planet surface LOD is coverage-first. Root patches provide guaranteed coarse
coverage for every planet domain, and view/horizon culling only stops
refinement; it does not remove the fallback surface. When a patch refines, it
hands off its full area to child subtrees, so the renderer never draws a parent
and child for the same domain at the same time. This keeps camera rotation from
revealing empty holes while patch replans are deferred during dragging.

Patch identity is explicit: each selected surface instance has a `face/level/x/y`
address, and UV bounds are derived from that address plus the root
`patches_per_face` setting. This keeps LOD addressing independent of mesh
construction and creates stable keys for later terrain, bathymetry, cache, or
streaming work.

The terrain controls are placeholders for contract pressure, not the final
terrain system. The live renderer displaces the reusable grid in the vertex
shader with deterministic multi-band terrain: broad shape, mid ridges, and fine
detail. Normals are recomputed from a patch-cell-scaled sample step so higher
LOD reveals smaller terrain features instead of only smoothing the mesh. The CPU
mesh builder remains as a diagnostic/test path for the same patch contracts.

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

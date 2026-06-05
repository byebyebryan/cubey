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
./build/dev/projects/planet/planet --debug-view seams
```

Supported debug views are `final`, `face-id`, `patch-id`, `lod-level`,
`screen-error`, and `seams`. The CPU LOD path is a first diagnostic
implementation: it plans camera-relative cube-sphere patch instances by
projected edge size, adds patch skirts for seam coverage, and reports patch,
LOD, refinement cull, screen-error, edge-length, per-LOD cell-size, and skirt
ranges in the UI.

Planet surface LOD is coverage-first. Root patches provide guaranteed coarse
coverage for every planet domain, and view/horizon culling only stops
refinement; it does not remove the fallback surface. When a patch refines, it
hands off its full area to child subtrees, so the renderer never draws a parent
and child for the same domain at the same time. This keeps camera rotation from
revealing empty holes while synchronous mesh rebuilds are deferred during
dragging.

Patch identity is now explicit: each selected surface instance has a
`face/level/x/y` address, and UV bounds are derived from that address plus the
root `patches_per_face` setting. This keeps the CPU mesh builder out of LOD
addressing policy and creates stable keys for later terrain, bathymetry, cache,
or streaming work.

The terrain controls are placeholders for contract pressure, not the final
terrain system. They add deterministic CPU displacement along the sphere normal
and recompute mesh normals so patch identity, seams, and LOD diagnostics can be
tested against non-spherical geometry before ocean or real terrain data is
ported in.

This is not yet a real async streamer. Future streaming should keep the same
contract: parent patches remain renderable until all child coverage needed for a
refinement is built and uploaded.

This project should stay focused on planet-scale contracts first. Ocean scale
work remains in `projects/ocean` until the planet frame, LOD, and world-space
contracts are stable enough to port it cleanly.

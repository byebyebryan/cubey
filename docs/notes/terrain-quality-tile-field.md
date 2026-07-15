# Terrain Quality Tile Field

Date: 2026-07-15

Status: implemented far-field v1 correction.

## Trigger

The interactive `backdrop-stage` view exposed visibly coarse distant facets and
silhouette holes after the orbit constraints were loosened. The terrain source
and procedural material were continuous; the failure came from the quality
geometry path. It combined nominal clipmap parent/child levels, patch-local
snapping, adaptive hardware tessellation, and stage-specific fixed factors.
The fixed factors hid some transition slits but made 256-1024 m patches visibly
coarse, while the overlapping levels still did not provide one continuous
tessellated surface.

The detached 300 m ownership cutout was not the cause. A dedicated
`stage-ownership` diagnostic renders the normally excluded zone and its boundary
instead of discarding it, separating stage ownership from geometry continuity.

## Reference Read

The local `TerrainEngine-OpenGL` reference starts from uniform world-space tiles
and applies distance-sensitive tessellation. That is a useful known-good
conceptual midpoint for Cubey's finite far-field use case. It does not by itself
solve streaming or planet-scale terrain.

A complete geometry clipmap is a different topology. The GPU Gems construction
uses nested regular grids plus transition blending, trim regions, fixups, and
degenerate outer edges. Those pieces must be designed together; retaining only
overlapping rings and height snapping does not reproduce the algorithm. See
[Terrain Rendering Using GPU-Based Geometry Clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry).

## Decision

The opt-in quality renderer uses a camera-centered field of adjacent,
world-aligned quad tiles:

- 128 by 128 patches and 16,384 patches total;
- 256 m maximum patch span and 16.384 km half extent;
- whole-tile origin recentering;
- exact shared control edges;
- shared-edge, power-of-two tessellation factors from 1 through 64;
- one continuous screen-derived source footprint with no nominal LOD snap.

The existing eight-level clipmap remains the control renderer. The quality tile
field is intentionally finite and camera-local. It is sufficient for the v1
background stage and rendering stress cases, but it does not claim unbounded
streaming, content residency, spherical mapping, or planet-scale LOD. A future
general terrain renderer should choose and validate its streaming topology as a
separate product rather than growing implicit parent levels back into this mesh.

Detached stage ownership is unchanged. Normal rendering excludes the inner
300 m owned by the consuming scene; shadows continue to query the underlying
continuous source. The `stage-ownership` diagnostic colors the consumer zone,
terrain zone, and boundary independently.

## Evidence

Run:

```sh
projects/terrain/capture_quality_tile_review.sh
```

The pack under `outputs/terrain/quality-tile-v1/` contains:

- paired clean and sphere-only stage captures at six azimuths;
- minimum, default, and maximum orbit-envelope cases;
- seeds `0`, `9012`, and `12345` at 1920 x 1080;
- clay, tessellation-factor, projected-edge, and ownership diagnostics;
- a 120-frame full-orbit video and GPU profile;
- planner JSON and machine-readable review metadata.

The accepted run kept the detached lower-frame distance at or above 1.5 km and
measured a 9.3638 ms incremental frame interval at 960 x 540 on the review
machine. Visual inspection found no holes, exposed parent slabs, or tile-edge
discontinuities through the tested yaw and orbit envelope. Source shape,
weathering, and materials were not changed by this correction.

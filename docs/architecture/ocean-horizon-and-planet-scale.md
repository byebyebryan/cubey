# Ocean Horizon And Planet Scale Direction

This note captures the product and architecture direction for moving
`projects/ocean` from a large visible water patch toward horizon-scale and
eventually planet-scale rendering.

The current renderer is good enough as a wave and material testbed, but it still
reads as a large square surface when viewed from some angles or distances. The
next product question is not only "make the square bigger"; it is how to make
the renderer feel continuous to the horizon without making near-term work
incompatible with later planet-scale goals.

## Direction

Do not jump directly to a full planet renderer. Build the next ocean slices as a
local tangent patch of a future planet:

- get immediate visual value from horizon-scale local ocean rendering;
- make coordinate, mesh, atmosphere, and terrain contracts compatible with
  curved far-field and planet-scale work;
- keep wave simulation in local tangent space until a concrete feature needs
  global weather, bathymetry, or streaming;
- avoid baking an infinite flat `Y = 0` plane into shared renderer contracts.

This makes Tier 1 and Tier 2 natural stepping stones rather than throwaway
implementations.

## Product Tiers

### Tier 1: True-To-Horizon Local Ocean

Goal: the ocean fills the visible view to the horizon with no readable square
bounds.

The world can still be treated as locally flat. At common ocean-camera heights,
the visible horizon is modest enough that a camera-relative local patch is
practical:

```text
horizon_distance ~= sqrt(2 * planet_radius * camera_height)
```

Approximate Earth-scale distances:

| Camera height | Horizon distance |
| ---: | ---: |
| 2 m | 5 km |
| 20 m | 16 km |
| 100 m | 36 km |
| 1 km | 113 km |

Practical implementation shape:

- derive required far extent from camera height and a safety margin;
- use a camera-relative clipmap or radial/annular mesh that extends beyond the
  computed horizon;
- hide mesh boundaries through horizon fog and atmospheric aerial perspective;
- fade geometric displacement with distance and mesh cell size;
- keep far normal, foam, and reflection detail as material contribution where
  geometry is too coarse;
- add diagnostics for camera height, computed horizon distance, ocean mesh
  extent, and displacement/detail fade bands.

Tier 1 should solve the current square-bound presentation problem.

### Tier 1.5: Planet-Compatible Contracts

Goal: make the flat local ocean renderer use the same vocabulary that a curved
or planet-scale renderer would need.

Contracts to introduce before changing visual behavior too much:

- a local tangent frame derived from a larger world frame;
- camera-relative render coordinates generated from that local frame;
- an ocean surface mapping abstraction with a flat implementation first;
- explicit water datum and up direction instead of an implicit global plane;
- terrain/bathymetry sampling coordinates that are world-frame aware;
- atmosphere inputs that include planet radius, camera altitude, and sun
  direction;
- debug readouts that report local frame origin, render origin, camera altitude,
  and far extent.

This is the key step that prevents Tier 1 from becoming a dead end.

### Tier 2: Curved Horizon / Over-Horizon Rendering

Goal: the far ocean follows planetary curvature so the horizon and distant
occlusion read correctly.

The near field can still use the current FFT wave data in a local tangent
frame. Curvature is primarily a surface mapping and shading problem:

- keep near wave sampling in local tangent coordinates;
- bend far vertices onto a spherical or ellipsoidal ocean datum;
- preserve camera-relative GPU precision;
- use double-precision or high-precision CPU world coordinates for large camera
  motion;
- make atmosphere/aerial perspective responsible for the far blend into sky;
- support distant object occlusion against the curved water datum when needed.

Tier 2 is worthwhile for high-altitude views, ships disappearing over the
horizon, or flight-scale scenes. It should not require rewriting the FFT ocean
core if the local-frame and surface-mapping boundary is in place.

### Tier 3: Planet Scale

Goal: render and navigate ocean, terrain, atmosphere, and weather at planetary
scale.

This is a separate product tier, not just a bigger ocean mesh. It likely needs:

- a planet coordinate model and floating-origin system;
- camera-relative rendering throughout the engine;
- spherical terrain/ocean patching, such as cube-sphere or quadtree patches;
- streamed terrain, bathymetry, shoreline, and material fields;
- planet-aware atmosphere and cloud layers;
- wind/weather fields that drive local ocean spectra;
- local interaction patches for wakes, boats, shorelines, and surf;
- strict precision and LOD diagnostics.

Tier 3 should wait until the horizon-scale renderer, atmosphere, and
terrain/bathymetry contracts have enough pressure to justify the infrastructure.

## Existing Foundation

Useful pieces already exist:

- `projects/ocean` has a camera-relative clipmap mesh and mesh-cell-aware LOD
  diagnostics;
- the ocean shader already separates displacement, normal, foam, atmosphere,
  and terrain-field debug paths;
- `AtmosphereEnvironmentRuntime` provides shared sky, reflection, sun, and
  lighting data;
- `TerrainOceanFieldView` defines a first terrain/ocean field contract for
  height, water depth, shoreline signed distance, and slope;
- ocean performance work now keeps FFT maps, field precision, and active
  cascade work explicit.

The missing foundation is not primarily wave compute. It is the world-frame,
surface-mapping, far-mesh, and horizon-composition contract.

## Research Decisions

The first horizon-scale implementation should stay with a viewer-centered
geometry clipmap, similar in spirit to Crest-style ocean LODs and geometry
clipmaps. The mesh should remain separate from the wave data: FFT cascades
provide local displacement, normals, and foam, while the clipmap decides where
the surface exists and how dense it is near the camera.

Projected grids and radial meshes are useful references for open-water horizon
rendering, but they are not the primary T1 path. They are harder to align with
later shoreline, bathymetry, terrain, and planet-frame contracts. A square
clipmap with stable snapping, enough far extent, explicit LOD diagnostics, and
horizon/aerial-perspective composition is the most direct path from the current
renderer.

T1 should target deck, ship-mast, and cinematic drone views first. Aircraft and
space-scale views are diagnostic only until the curved surface mapping and
planet-scale terrain/ocean patching work exists.

## Design Constraints

Build toward planet scale without paying for full planet infrastructure too
early:

- keep GPU positions camera-relative;
- keep CPU/world positions ready for double precision or floating origin;
- keep wave simulation local and deterministic;
- make surface mapping an explicit dependency of mesh generation;
- make atmosphere own horizon color and far extinction;
- keep terrain/bathymetry as data fields sampled by ocean, not logic owned by
  the ocean renderer;
- make diagnostics visible for every new scale assumption.

Avoid:

- simply increasing the square mesh extent until the edge is harder to notice;
- hiding all scale problems with fog before the mesh and coordinate contracts
  are clear;
- making the active ocean project own planet terrain, weather, or streaming;
- coupling FFT cascade domains directly to global planet coordinates;
- assuming a single global up vector in shared render contracts.

## Suggested Implementation Sequence

1. Add horizon-distance and far-extent diagnostics to `projects/ocean`.
2. Make the ocean mesh extent derive from camera height plus an explicit safety
   margin.
3. Improve far mesh layout so square bounds are not visible in normal viewing
   conditions.
4. Add horizon-aware atmosphere/aerial-perspective blending for the far ocean.
5. Introduce local-frame and surface-mapping vocabulary while keeping the
   implementation flat.
6. Add a curved far-ocean mapping path after the flat horizon renderer is
   visually stable.
7. Only then evaluate planet-scale terrain/ocean patching and streaming.

This keeps the next slice product-visible while leaving a clean path from local
ocean to curved horizon and later planet scale.

## Open Questions

- What camera-height range should the ocean project target first: deck/person
  height, ship mast, cinematic drone, or aircraft?
- Should the first far mesh be clipmap-based, radial/annular, or a hybrid?
- Should curved far ocean be a separate far pass or a single mesh mapping mode?
- How much of the atmosphere path needs true aerial perspective before Tier 1
  hides the horizon boundary convincingly?
- What world-frame convention should terrain, ocean, atmosphere, and future
  planet work share?

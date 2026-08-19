# Ocean Horizon And Curved-Local Direction

This note captures the product and architecture direction for moving
`projects/ocean` from a large visible water patch toward horizon-scale and
curved-local rendering. Full planet scale is now treated as a separate
future planet-surface direction, not the next responsibility of the ocean
project or the active orbital-only `projects/planet` executable.

The current renderer is good enough as a wave and material testbed, but it still
reads as a large square surface when viewed from some angles or distances. The
next product question is not only "make the square bigger"; it is how to make
the renderer feel continuous to the horizon without making near-term work
incompatible with later planet integration.

## Direction

Do not turn `projects/ocean` into a full planet renderer. Build ocean as a local
tangent patch that can later be hosted by a separately scoped planet-surface
product:

- get immediate visual value from horizon-scale local ocean rendering;
- make coordinate, mesh, atmosphere, and terrain contracts compatible with
  curved far-field and later planet handoff;
- keep wave simulation in local tangent space until a concrete feature needs
  global weather, bathymetry, or streaming;
- avoid baking an infinite flat `Y = 0` plane into shared renderer contracts.

This makes Tier 1 and Tier 2 useful endpoints for ocean, not throwaway
implementations on the way to a larger planet platform.

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

Current implementation status: `projects/ocean` has the local tangent frame,
curved-far surface mode, automatic horizon-driven mesh extent, Mid/High/Wide
camera presets, headless camera-preset capture support, and diagnostics for the
effective mesh plus cascade contribution weights. It deliberately still uses
`ClipmapGrid2D`; adopting the shared adaptive patch planner is deferred until a
future planet-surface or shoreline consumer creates a real address-space need.

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
- atmosphere inputs that include planet radius, camera altitude, and
  celestial-derived sun direction/radiance;
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

The first T2 implementation should be a single-pass surface-mapping extension
of the current clipmap mesh, not a separate planet renderer. The near field
stays visually flat and continues to sample FFT cascades in local XZ space. The
far field smoothly bends the base water datum onto an Earth-radius spherical
surface, with displacement riding on top of that mapped surface. This keeps the
renderer debuggable while proving the T1.5 frame contract under real curvature
pressure.

### Future Planet-Surface Handoff

Goal: stop ocean scale work at a clean handoff boundary.

Rendering and navigating ocean, terrain, atmosphere, and weather at planetary
a future planet-surface product, which likely needs:

- a planet coordinate model and floating-origin system;
- camera-relative rendering throughout the engine;
- spherical terrain/ocean patching, such as cube-sphere or quadtree patches;
- streamed terrain, bathymetry, shoreline, and material fields;
- planet-aware atmosphere and cloud layers;
- wind/weather fields that drive local ocean spectra;
- local interaction patches for wakes, boats, shorelines, and surf;
- strict precision and LOD diagnostics.

That infrastructure should be built in an empty planet-first project with its
own LOD and precision diagnostics. Ocean should be ported into that project once
the planet frame is stable enough to host a local water layer.

## Existing Foundation

Useful pieces already exist:

- `projects/ocean` has a camera-relative clipmap mesh and mesh-cell-aware LOD
  diagnostics;
- `LocalTangentFrame` in `cubey::render` provides shared local-patch frame
  vocabulary with double-precision world origin, basis axes, planet radius, and
  water datum;
- `AdaptivePatchLod` in `cubey::render` provides the reusable quadtree selection,
  hysteresis, budget fallback, neighbor repair, edge mask, and diagnostic
  mechanics used by the planet global surface patch tree;
- `OceanSurfaceFrame` derives the active ocean mesh config, horizon
  diagnostics, projection far plane, and surface-frame metadata from the camera
  each frame;
- the ocean shader already separates displacement, normal, foam, atmosphere,
  and terrain-field debug paths;
- `AtmosphereEnvironmentRuntime` provides shared sky, reflection, sun, and
  lighting data;
- `TerrainOceanFieldView` defines a first terrain/ocean field contract for
  height, water depth, shoreline signed distance, and slope;
- ocean performance work now keeps FFT maps, field precision, and active
  cascade work explicit.

The remaining ocean foundation is not primarily wave compute. It is far-water
atmosphere composition, curved-local terrain/bathymetry sampling, and a clean
handoff contract for future planet integration.

## Current T1 Status

The active ocean renderer now has a first T1 implementation slice:

- horizon distance is derived from camera altitude and atmosphere planet radius;
- the mesh has an automatic horizon mode that expands effective half extent
  beyond the manual minimum and raises LOD count up to the supported limit;
- projection far plane is derived from the effective horizon mesh so far
  patches are not clipped by the old fixed distance;
- the UI reports camera altitude, horizon distance, required extent, effective
  mesh extent, near/far cell size, LOD count, patches, and coverage ratio;
- the vertex shader routes flat `Y = datum` world mapping through named
  surface-mapping helpers while keeping FFT sampling in local XZ space;
- surface-frame uniforms publish water datum, planet radius, camera altitude,
  and horizon distance to the surface shaders;
- the fragment shader composes the far ocean through a named
  aerial-perspective placeholder:
  `water * transmittance + sky_radiance * inscatter_weight`.

This T1 slice established horizon extent and far composition. The active
renderer has since moved beyond this flat baseline with the T2 curved
far-surface path described below.

## Current T1.5 Status

The flat ocean now uses the first version of the frame vocabulary that curved
far-ocean and later planet integration will need:

- `LocalTangentFrame`: shared render-space vocabulary for a local patch of a
  larger world, with double-precision world origin, right/up/forward basis,
  planet radius, and water datum.
- `OceanSurfaceFrame`: ocean runtime state derived each frame from
  `LocalTangentFrame`, camera transform, atmosphere settings, and ocean config.
  It owns the effective mesh config, horizon diagnostics, projection far plane,
  and metadata sent to shaders.

For T1.5 the basis is still the current world axes and the water datum remains
`0 m`. The current implementation routes projection, mesh, terrain diagnostic
sea level, shader surface metadata, and background-atmosphere camera altitude
through that frame. The point is not curvature yet; it is to stop passing loose
horizon, mesh, atmosphere, and water-plane assumptions through unrelated app and
shader slots.

The shader contract now follows this shape:

- push constants stay small and per-patch;
- per-frame surface metadata belongs in a uniform;
- FFT sampling remains in local XZ space;
- world/render position generation goes through named surface-mapping helpers;
- atmosphere and ocean agree on camera altitude derived from the same frame.

## Current T2 Status

T2 now makes curvature visible by default while keeping Flat mode as an A/B and
regression path. The implementation remains a viewer-centered local tangent
patch:

- surface mode is a resolved frame/config choice: `flat` or `curved-far`;
- curvature starts after the near field and reaches full strength before the
  horizon, using ratios of the computed horizon distance;
- spherical drop is measured relative to the local tangent water datum, with
  the planet center below the local frame by `planet_radius_m`;
- FFT sample coordinates remain unchanged in local XZ, so the wave compute core
  stays local and deterministic;
- shaders generate world/render positions through surface-mapping helpers, and
  material normals start from the mapped surface up vector.
- surface-frame uniforms publish both datum/radius/altitude/horizon metadata and
  curved-surface mode/start/end/strength metadata;
- the UI and CLI expose surface mode, curvature start/end ratios, curvature
  strength, and a `curvature` debug view.

Still deferred inside ocean:

- true curved-horizon occlusion for distant objects;
- world-frame-aware terrain/bathymetry sampling beyond the diagnostic local
  field;
- shoreline interaction and local bathymetry-driven surf inputs;
- true atmospheric LUT aerial perspective for far-water extinction.

Deferred to a future planet-surface product:

- floating origin or large-world camera state outside the ocean project;
- planet-scale patch LOD and streaming;
- global weather and terrain/bathymetry/material fields.
- volumetric cloud raymarching; ocean should later consume cloud sky,
  reflection, and shadow outputs from the planet/weather stack.

The ocean project should stop at the current local horizon-scale/curved-local
boundary until a separately scoped product provides a stronger host.
Planet-scale ocean is not a bigger square FFT mesh; it is a local water layer
attached to a planet frame. The future handoff should be:

- the future planet-surface product owns the Earth-like radius, local tangent
  frame, render origin, sea datum, bathymetry, shoreline SDF, and global tile
  identity;
- ocean owns local wave displacement, normals, foam, material response, and
  interaction textures;
- a viewer-centered local detail clipmap bridges the two so mesh density and FFT
  wave bands can be chosen independently of global cube-sphere tile depth.

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

T1 and T2 should target deck, ship-mast, and cinematic drone views first.
Aircraft and space-scale views are diagnostic only until a planet-surface
product owns the surface LOD and world-frame contracts.

## Design Constraints

Build toward planet compatibility without paying for full planet infrastructure
inside ocean:

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

1. Done: add horizon-distance and far-extent diagnostics to `projects/ocean`.
2. Done: make the ocean mesh extent derive from camera height plus an explicit
   safety margin.
3. Done enough for now: horizon-derived extent, curved far-surface mapping, and
   aerial perspective keep square bounds out of normal ocean views. Deeper mesh
   topology work should wait for a separately scoped planet-surface consumer;
   the active orbital Planet is not a local-detail host.
4. Done as a placeholder: add horizon-aware atmosphere/aerial-perspective
   blending for the far ocean.
5. Done as a flat seam: introduce local-frame, surface-frame, datum, projection,
   terrain diagnostic, atmosphere altitude, and shader metadata vocabulary while
   keeping the implementation flat.
6. Done: add a single-pass curved far-ocean mapping path with Flat/Curved-Far
   surface modes, horizon-ratio blend controls, and curvature diagnostics.
7. Done enough for now: ocean has reached the horizon-scale/curved-local
   endpoint and should remain a water renderer/testbed.
8. Deferred to a separately approved planet-surface product: define local/global
   morphing, persistent topology, cache/streaming contracts, render order, and
   ocean payload attachment before porting any ocean waves.

This keeps ocean product-visible without expanding the active orbital Planet or
committing Cubey to a planet-surface implementation now.

## Open Questions

- What camera-height range should the ocean project target first: deck/person
  height, ship mast, cinematic drone, or aircraft?
- Should the first far mesh be clipmap-based, radial/annular, or a hybrid?
- How much of the atmosphere path needs true aerial perspective before Tier 1
  hides the horizon boundary convincingly?
- What exact frame contract should a future planet-surface product use when it
  consumes ocean as a local water layer?

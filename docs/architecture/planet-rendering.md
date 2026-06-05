# Planet Rendering Direction

This note captures the empty-planet-first direction for future planet-scale
work. The immediate goal is not to make `projects/ocean` larger. It is to build
a small, inspectable planet project that owns scale, navigation, LOD, and
coordinate policy before ocean is attached as one surface layer.

## Decision

Start `projects/planet` as a visible planet foundation project:

- configurable planet radius, including small Kerbal-style planets;
- camera/world position in double precision with camera-relative GPU rendering;
- local tangent frames derived from the active planet frame;
- atmosphere altitude, horizon, and projection derived from the same frame;
- planet surface LOD with wireframe and patch diagnostics;
- a simple shaded or debug surface before terrain or ocean complexity arrives.

Ocean should be ported into `planet` when the planet frame and LOD contract are
stable enough to host it. `projects/ocean` remains the focused local-water
renderer and should not own full planet terrain, weather, or streaming.

## Why Empty Planet First

Planet scale is a world problem before it is an ocean problem. The hard
contracts are:

- global coordinate model and floating-origin policy;
- surface patch hierarchy and LOD selection;
- seam handling and LOD morphing;
- camera-relative GPU precision;
- atmosphere, horizon, and altitude agreement;
- terrain, bathymetry, water, clouds, and debug overlays sharing the same frame.

Those are easier to see on a neutral planet surface than through FFT waves,
foam, self-shadowing, ocean material response, and far-water fog. The first
planet milestone should therefore be visually plain but highly diagnostic:
surface patch colors, wireframe, horizon, altitude, radius, screen error,
triangle counts, and local-frame readouts.

## Ocean Handoff Boundary

`projects/ocean` is currently in a reasonable stopping state for scale work: it
fills horizon-scale views, supports a curved-far local surface, exposes mesh and
curvature diagnostics, and keeps FFT sampling local. That is useful as a water
renderer and as a future donor.

The planet project should consume ocean through a narrow contract:

- planet supplies local tangent frame, radius, datum, altitude, sun/atmosphere,
  and render origin;
- ocean supplies local wave displacement, normals, foam, material response, and
  optional local interaction data;
- planet owns global terrain, bathymetry, shoreline streaming, weather fields,
  and cross-layer render order.

This keeps ocean from becoming a hidden planet platform while still preserving
the ocean work for later integration.

## Research Baseline

Use established terrain and globe terminology rather than inventing a custom
LOD vocabulary:

- Geometry clipmaps are the closest match to current ocean and terrain
  diagnostics: nested viewer-centered grids, stable rates, transition regions,
  and GPU-side detail synthesis. See GPU Gems 2, chapter 2:
  <https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry>.
- CDLOD is a strong reference for terrain-style patch selection: quadtree
  nodes, distance-dependent LOD, fixed grid meshes, and vertex morphing for
  seam-free transitions. See Filip Strugar, "Continuous Distance-Dependent
  Level of Detail for Rendering Heightmaps":
  <https://www.tandfonline.com/doi/abs/10.1080/2151237X.2009.10129287>.
- Cesium 3D Tiles is the right terminology reference for streamed spatial data:
  hierarchical LOD, geometric error, screen-space error, refinement, tile
  transforms, and bounding volumes:
  <https://github.com/CesiumGS/3d-tiles/blob/main/specification/README.adoc>.
- Planet-scale terrain papers such as P-BDAM and ellipsoidal clipmaps reinforce
  the same lesson: planet rendering needs explicit tiling, precision handling,
  out-of-core data, and view-dependent LOD. These are later targets, not the
  first empty-planet slice. References:
  <https://vcg.isti.cnr.it/publication/2003/CGGMPS03/> and
  <https://www.sciencedirect.com/science/article/pii/S0097849315000916>.
- Crest remains a useful ocean reference: its ocean data LODs are
  viewer-centered cascaded textures, separate from the rendered geometry. That
  is a good model for how ocean can later attach to planet without owning the
  planet LOD tree:
  <https://crest.readthedocs.io/en/4.16/user/technical-information.html>.

## Existing Cubey Foundation

Useful pieces already exist and should be reused:

- `cubey::render::LocalTangentFrame`: double-precision world origin, basis,
  planet radius, datum, and world/local conversion;
- `cubey::render::ClipmapGrid2D`: flat 2D clipmap planning, patch counts,
  transition widths, cell sizes, and triangle diagnostics;
- `cubey::render::TerrainOceanFieldView`: the first height/depth/shore/slope
  field contract used by terrain and ocean;
- `cubey::engine::AtmosphereEnvironmentRuntime`: shared sky/background,
  reflection probe, direct light, and environment-lighting bridge;
- shared HDR post and performance UI contracts.

Do not promote ocean-specific classes wholesale yet. `OceanSurfaceFrame`,
`OceanHorizonDiagnostics`, and ocean shader surface mapping contain useful
ideas, but they are tied to `OceanConfig`, water datum, FFT sampling, horizon
fog, cascade LOD, and terrain-foam controls.

## LOD Direction

LOD should be established before ocean is ported.

Recommended first approach:

- start with a cube-sphere or six-face quadtree patch model;
- draw a neutral planet surface with per-level coloring and wireframe;
- select patches from screen-space error or a simple distance/altitude metric;
- identify each surface patch by stable `face/level/x/y` coordinates and derive
  local bounds from that address;
- keep each patch rendered from a reusable fixed grid;
- add skirts or morph bands before adding visual layers;
- report patch count, visible levels, near/far cell size, triangle count,
  screen error, altitude, and horizon distance every frame.

Geometry clipmaps can remain useful for local viewer-centered data and for ocean
surface payloads. Planet terrain itself should get a more general patch tree
because terrain, bathymetry, cloud shadows, and streamed material fields need a
stable global address.

## First Contracts

Add these in the planet project first, then promote only when a second project
uses the same contract:

- `PlanetConfig`: radius, atmosphere height, datum/sea level, scale preset, and
  diagnostic toggles.
- `PlanetFrame`: planet center, camera world position, altitude, up/right/forward
  basis, local tangent frame, render origin, horizon distance, and far plane.
- `PlanetSurfacePatch`: face id, quadtree coordinates, level, local bounds,
  geometric error, bounding volume, and render-grid parameters.
- `PlanetLodDiagnostics`: selected patch counts, visible levels, screen-error
  ranges, cell-size ranges, triangle totals, and seam/morph status.
- `PlanetSurfaceMapping`: CPU and shader vocabulary for mapping a patch sample
  to sphere/ellipsoid position, local up, render position, and sample
  coordinates.

Current implementation notes:

- Patch addresses are explicit `face/level/x/y` ids. Selected patch instances
  carry that id plus screen-error metrics. The live renderer draws those
  instances through one reusable patch grid, and both CPU diagnostics and shader
  mapping derive UV bounds from the id instead of owning LOD addressing.
- Live LOD selection and CPU mesh diagnostics intentionally have different
  limits. The instanced renderer currently accepts LOD 0-6 and defaults to LOD
  5 with a 10 px target edge; the CPU mesh path rejects configurations that
  would materialize too many vertices. Live planning also has a fixed patch
  instance budget so pathological interactive settings fail early. Use the live
  renderer for interactive LOD pressure and the CPU mesh path for bounded
  validation.
- Camera-driven LOD replans no longer rebuild GPU instance buffers immediately
  or wait for the device. The planner marks patch instances dirty, and each
  render frame lazily uploads the current instance list into that frame slot's
  buffer when its generation is stale. Full config rebuilds still drain and
  recreate resources because the reusable patch grid itself can change.
- LOD is coverage-first. View and horizon culling stop refinement, but the
  parent patch remains selected so rotating while rebuilds are deferred does not
  reveal holes.
- Skirts are the active transition policy. Morph bands remain a later quality
  pass once terrain and ocean layers put more pressure on parent/child seams.
- Placeholder planet terrain is project-local shader displacement along the
  sphere normal, with the CPU mesh builder retained for diagnostics and tests.
  CPU and shader paths now go through a named project-local surface-field
  contract: height, world position, normal, normalized elevation, normalized
  slope, and a simple material band. It now has broad, mid-ridge, and
  fine-detail bands plus patch-cell-scaled normal sampling. It exists to
  pressure patch identity, normals, LOD diagnostics, seams, and material
  vocabulary before connecting real procedural terrain or ocean data.
- Current debug views cover patch identity, LOD level, screen error, seam
  skirts, approximate metric cell edge, normalized terrain height, normalized
  terrain slope, and terrain material bands. These are diagnostic tools, not
  final planet visualization.

Deferred surface-field work:

- bathymetry and sea-level-aware sampling;
- streamed field tiles and cache residency;
- material textures, biome masks, and erosion/weathering inputs;
- shoreline masks and terrain/ocean render ordering;
- ocean attached as a planet surface layer once these contracts are stable.

## Suggested Sequence

1. Add this design boundary and resync ocean docs.
2. Add `projects/planet` as an empty-planet viewer with atmosphere background,
   radius/altitude controls, and frame diagnostics.
3. Add a debug planet surface with cube-sphere or quadtree patch IDs.
4. Add LOD selection, wireframe, and patch diagnostics.
5. Add seam handling through skirts or morph bands.
6. Add placeholder terrain/bathymetry/material fields.
7. Port ocean as a local water layer once the planet frame and LOD contracts are
   stable.

Non-goals for the first planet pass:

- real-world GIS ingestion;
- full out-of-core streaming;
- global weather simulation;
- replacing the current FFT ocean core;
- moving existing ocean quality work behind planet infrastructure.

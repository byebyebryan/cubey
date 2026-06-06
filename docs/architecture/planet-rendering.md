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
- planet-owned sky/celestial rendering before ocean complexity arrives;
- planet-owned celestial bodies for sun and moon, with any future atmosphere
  consuming derived scattering inputs rather than owning those bodies;
- shared HDR scene color and fullscreen post so planet uses the same display
  path as the PBR/ocean renderers.

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

- planet supplies local tangent frame, radius, datum, altitude, celestial body
  state, atmosphere inputs, and render origin;
- ocean supplies local wave displacement, normals, foam, material response, and
  optional local interaction data;
- planet owns global terrain, bathymetry, shoreline streaming, weather fields,
  celestial bodies, and cross-layer render order.

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
- `cubey::engine::AtmosphereEnvironmentRuntime`: useful reference/runtime for
  ocean and atmosphere demos, but no longer the active planet sky owner;
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
  ranges, transition pressure, cell-size ranges, triangle totals, and seam/morph
  status.
- `PlanetSurfaceMapping`: CPU and shader vocabulary for mapping a patch sample
  to sphere/ellipsoid position, local up, render position, and sample
  coordinates.

Current implementation notes:

- Patch addresses are explicit `face/level/x/y` ids. Selected patch instances
  carry that id plus screen-error metrics. The live renderer draws those
  instances through one reusable patch grid, and both CPU diagnostics and shader
  mapping derive UV bounds from the id instead of owning LOD addressing.
- Live LOD selection and CPU mesh diagnostics intentionally have different
  limits. The instanced renderer currently accepts LOD 0-9, patch resolutions
  up to 128, and defaults to LOD 8 with a 64x64 reusable grid and a 6 px target
  edge. The CPU mesh path still rejects configurations that would materialize
  too many vertices. Live planning has a fixed patch instance budget and falls
  back to coarser parent coverage when aggressive interactive settings would
  exceed it. Use the live renderer for interactive LOD pressure and the CPU mesh
  path for bounded validation.
- Camera-driven LOD replans no longer rebuild GPU instance buffers immediately
  or wait for the device. The planner marks patch instances dirty, and each
  render frame lazily uploads the current instance list into that frame slot's
  buffer when its generation is stale. Full config rebuilds still drain and
  recreate resources because the reusable patch grid itself can change.
- LOD is coverage-first. View and horizon culling stop refinement, but the
  parent patch remains selected so rotating while rebuilds are deferred does not
  reveal holes.
- Skirts plus selection hysteresis are the active transition policy. The planner
  consumes the previous selected patch ids and applies a split/merge deadband so
  parent patches do not immediately split, and refined child coverage does not
  immediately merge, at the exact screen-error threshold. Morph bands remain a
  later quality pass once terrain and ocean layers put more pressure on
  parent/child seams. The `lod-transition` debug view, transition-pressure
  diagnostics, budget fallback counters, and hysteresis delayed split/merge
  counters make the threshold pressure visible before adding a morph
  implementation.
- Placeholder planet terrain is project-local shader displacement along the
  sphere normal, with the CPU mesh builder retained for diagnostics and tests.
  CPU and shader paths now go through a named project-local surface-field
  contract: height, world position, normal, height above sea level, water depth,
  normalized bathymetry, shoreline mask, normalized elevation, normalized slope,
  and a simple material band. It now has broad, mid-ridge, and fine-detail bands
  plus patch-cell-scaled normal sampling. Water classification is based on
  explicit sea level; bathymetry and shoreline are diagnostic fields, not yet a
  separate ocean layer. This exists to pressure patch identity, normals, LOD
  diagnostics, seams, and material vocabulary before connecting real procedural
  terrain or ocean data.
- Current debug views cover patch identity, LOD level, screen error, seam
  skirts, approximate metric cell edge, normalized terrain height, normalized
  terrain slope, terrain material bands, bathymetry, shoreline, wireframe grid,
  and LOD
  transition pressure. These are diagnostic tools, not final planet
  visualization.
- Planet rendering has moved away from the shared atmosphere background/runtime
  for now. The shared path was useful for ocean and atmosphere demos, but its
  demo-oriented sky clock and inline celestial disks were the wrong source of
  truth for planet orbit views. `projects/planet` now owns a local solar clock,
  explicit sun/moon state, local sky pass, local limb glow, and derived surface
  lighting. Repeatable headless captures can pin the local solar day, hour,
  clock pause state, and startup camera mode through descriptor-backed
  `RunConfig` options.
- The planet surface frame has moved out of push constants. Per-frame uniform
  data now carries view/projection, render origin, radius, terrain options,
  sea-level/bathymetry/shoreline options, camera render position, horizon
  distance, local sky light, and haze fields. Patch identity and screen-error
  remain per-instance data. This keeps room for planet-scale frame contracts
  without repeatedly repacking the 128-byte push constant budget.
- Planet rendering now writes a project-owned sky/celestial pass and the surface
  pass into an HDR transient scene color target, then uses the shared post pass
  for exposure, tone mapping, and output encoding. This aligns planet with the
  renderer-wide linear HDR contract while keeping ocean integration deferred.
- The current atmosphere shader path is deliberately planet-local. The old
  analytic mode remains a debug fallback, while the next stable path is a
  direct single-scattering model with spherical atmosphere intersections,
  Rayleigh/Mie vocabulary, sun transmittance, and surface aerial perspective.
  Sky background and surface haze should consume the same project-local
  `planet_atmosphere.glsl` helpers so surface view, orbit view, moon
  visibility, and future ocean/cloud layers do not invent separate formulas.
- Planet now models a minimal solar-system state directly: planet orbit around
  the sun, planet self-rotation, and moon orbit around the planet. The sun is
  still rendered by the local sky pass as a distant disk/glow, while the moon
  is rendered as a depth-tested sphere on a camera-relative shell that
  preserves apparent angular size.
- The current solar-system math is deliberately mean and Earth-like rather than
  ephemeris-driven. `PlanetSolarTime` is a 24h mean solar clock. The default
  config uses a 23.9345h sidereal planet spin, 365.2422d tropical/seasonal
  year, 27.321661d lunar sidereal orbit, and a signed synodic phase cycle of
  about 29.53d. This keeps day/year/moon behavior sensible while leaving
  eccentricity, equation of time, lunar nodal/apsidal precession, and
  Earth-Moon barycenter work for later.

## Current Gap-Closure Status

The latest planet foundation pass closed several previously loose contracts:

- `PlanetConfig` and descriptor-backed `RunConfig` now expose the live terrain
  detail controls, atmosphere mode, sea level, bathymetry depth, and shoreline
  width. The local tangent frame uses sea level as its datum so terrain, camera
  height, and future ocean handoff use the same vertical reference.
- `PlanetSurfaceTileKey`, `PlanetSurfaceTilePayload`, and
  `PlanetSurfaceTileSource` make the current procedural surface provider look
  like a tile source. That is deliberately small, but it gives later terrain,
  bathymetry, biome, and cache work a stable `face/level/x/y` payload boundary.
- LOD diagnostics now report neighbor mismatch pressure, boundary edges, and
  maximum neighbor delta. Selected patch instances also carry an edge transition
  mask so shader sampling can treat borders against coarser neighbors
  differently from ordinary patch interiors.
- Camera state stores planet-scale position in double precision. Conversion to
  float happens at render-frame/upload boundaries, which keeps interactive
  navigation and future streaming decisions from depending on truncated GPU
  coordinates.
- Config application distinguishes dynamic replans from topology rebuilds.
  Dynamic changes update config, frame data, and patch instances without
  resetting the camera or waiting for the device. Patch-grid topology changes
  still rebuild the reusable grid and synchronize explicitly.
- `PlanetAtmosphereInputs` is derived from the planet-owned celestial model and
  can be adapted into the shared atmosphere runtime for future comparisons. The
  active planet renderer keeps a project-local atmosphere shader path; `analytic`
  is the fallback/debug mode and `physical` is the default v1 scattering path.
- Moon rendering remains explicit body geometry. The body pass now receives
  camera-relative shading inputs, applies procedural lunar albedo variation,
  fades the moon through the lower daytime atmosphere, and packs a first
  planet-shadow/eclipse factor for future refinement.
- Repeatable visual capture recipes live in
  [`docs/notes/planet-visual-captures.md`](../notes/planet-visual-captures.md)
  and cover orbit, surface, dawn/day/night, atmosphere comparison, LOD/seam
  diagnostics, celestial-plane checks, and surface-field debug views.

## Celestial Body Pivot

The shared atmosphere path is still valuable for standalone atmosphere demos,
ocean-scale backgrounds, and future reference work, but the planet project
should not treat it as the authoritative owner of the sun, moon, sky clock, or
other celestial bodies. The current shared background shader can draw a sun
disk, moon disk, stars, and Milky Way for lightweight scene backgrounds. That
convenience becomes the wrong abstraction for `projects/planet`: orbit and
surface views need real occlusion, radius, phase, lighting, diagnostics,
planetary self-rotation, and eventually eclipses or multiple moons.

The target ownership is:

- `projects/planet` owns a project-local `CelestialSystem`;
- celestial bodies own position/orbit state, physical radius, apparent angular
  radius, emission or albedo, texture/atlas references, sky participation, and
  debug labels;
- the sun is modeled first as a distant emissive body that produces a direction,
  angular radius, radiance/illuminance, and visible disk/glow;
- the moon is modeled as a spherical body lit by the sun, so phase and
  terminator behavior come from body geometry instead of an atmosphere shader
  approximation;
- a future planet-scale atmosphere consumes derived scattering inputs: planet
  radius, atmosphere height, camera altitude, sun direction, sun radiance, and
  sun angular radius;
- surface and ocean lighting consume derived direct-light inputs;
- any environment/probe cache is a derived adapter, not the source of truth for
  body state.

This intentionally avoids adding another vague environment owner. If a small
intermediate struct is needed for renderer plumbing, name it after what it
carries, such as `AtmosphereScatteringInputs`, `CelestialLightingInputs`, or
`SkyProbeInputs`.

The render-order contract for the current local path is:

1. planet-owned sky pass with dark space, stars, sun disk/glow, and analytic
   planet limb/occlusion;
2. opaque planet surface and terrain/ocean layers;
3. explicit celestial body geometry, starting with a depth-tested moon sphere;
4. clouds, aerial-perspective overlays, and post as those systems arrive.

For the immediate slice, a distant sun disk/glow in the sky pass plus a
depth-tested moon sphere is enough. The important boundary is that no shared
atmosphere shader decides celestial placement or planet occlusion. Later work
can move the sun to body-backed rendering or add eclipses without changing the
solar-system source of truth.

Established engine precedents support this split. Unreal's Sky Atmosphere
consumes scene Directional Lights marked as atmosphere lights, including
separate sun and moon indices. Godot's sky materials derive sun direction,
energy, and color from `DirectionalLight3D` nodes. Unity HDRP's physically based
sky consumes a Directional Light and treats sun-in-probe baking as a sky
environment option. Filament models sun/moon-style illumination as directional
lights with physical units. The common pattern is that sky/atmosphere rendering
consumes light/body state; it should not be the durable owner of planet-scale
celestial bodies.

Astronomical data references for the current mean model:

- NASA Glenn's sidereal-time note gives the 365.2422d tropical year and derives
  a 23.9345h sidereal day from the Earth/Sun relative rotation.
- NASA Earth facts provide the practical Earth distance, 365.25d public orbit
  summary, and 23.4 degree axial tilt.
- NASA Moon material gives the Moon's approximate distance/radius and the
  27.3d orbit/rotation summary; NASA eclipse material gives the more exact
  27.32166d sidereal lunar month used by the config.

Deferred surface-field work:

- streamed field tiles and cache residency;
- material textures, biome masks, and erosion/weathering inputs;
- real bathymetry sources, shoreline interaction, and terrain/ocean render
  ordering;
- ocean attached as a planet surface layer once these contracts are stable.

## Suggested Sequence

The current near-term gap-closure batch is tracked in
[`docs/notes/planet-gap-closure.md`](../notes/planet-gap-closure.md). Keep
temporary implementation checkpoints there and promote only stable contracts
back into this architecture note.

1. Add this design boundary and resync ocean docs.
2. Add `projects/planet` as an empty-planet viewer with local sky,
   radius/altitude controls, and frame diagnostics.
3. Add a debug planet surface with cube-sphere or quadtree patch IDs.
4. Add LOD selection, wireframe, and patch diagnostics.
5. Add seam handling through skirts or morph bands.
6. Add placeholder terrain/bathymetry/material fields.
7. Keep strengthening atmosphere, LOD, terrain, and diagnostics until they are
   stable enough to host other layers.
8. Done: move planet sun rendering out of the inline atmosphere disk path and
   into a planet-owned celestial body pass.
9. Done as a first analytic body: model sun/moon state through local solar
   system time, with planet orbit, self-rotation, and moon orbit.
10. Done: replace the analytic moon disk with body/geometry rendering.
11. Done as a first consistency pass: add shared project-local atmosphere terms
   for the planet sky and surface haze, plus fixed headless capture controls.
12. Done as a v1 path: replace the analytic default with a small project-local
   single-scattering and aerial-perspective model; keep full atmosphere LUTs
   deferred until the contract needs them.
13. Port ocean as a local water layer once the planet frame and LOD contracts are
   stable.

Non-goals for the first planet pass:

- real-world GIS ingestion;
- full out-of-core streaming;
- global weather simulation;
- full N-body simulation;
- replacing the current FFT ocean core;
- moving existing ocean quality work behind planet infrastructure.

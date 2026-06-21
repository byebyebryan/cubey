# Planet Rendering Direction

This note captures the planet rendering foundation and remaining planet-scale
integration direction. The immediate goal is not to make `projects/ocean`
larger. It is to keep `projects/planet` as the owner of scale, navigation, LOD,
terrain-field, sky/celestial, and coordinate policy before ocean is attached as
one surface layer.

## Decision

Start `projects/planet` as a visible planet foundation project:

- configurable planet radius, including small Kerbal-style planets;
- camera/world position in double precision with camera-relative GPU rendering;
- local tangent frames derived from the active planet frame;
- atmosphere altitude, horizon, and projection derived from the same frame;
- planet surface LOD with wireframe and patch diagnostics;
- planet-owned sky/celestial rendering before ocean complexity arrives;
- planet-owned celestial state for sun and moon, with any future atmosphere
  consuming derived scattering inputs rather than owning those bodies;
- shared HDR scene color and fullscreen post so planet uses the same display
  path as the PBR/ocean renderers.

Ocean should be ported into `planet` when the planet frame and LOD contract are
stable enough to host it. `projects/ocean` remains the focused local-water
renderer and should not own full planet terrain, weather, or streaming.

## Scale Policy

The default planet should exercise Earth-like scale, not the earlier mini-planet
debug scale. The mini scale remains useful for fast visual debugging, but it
cannot be the main product signal if future ocean, terrain, atmosphere, and
surface navigation are expected to share one world frame.

Earth-like scale also makes the LOD boundary explicit:

- the cube-sphere quadtree owns global coverage, silhouette, horizon culling,
  base terrain/bathymetry/material identity, and stable `face/level/x/y`
  addresses;
- viewer-centered local detail owns meter-scale terrain detail, ocean wave
  geometry, normals, foam, wakes, and shoreline interaction masks;
- increasing global quadtree depth is useful for terrain coverage, but it is not
  the mechanism for FFT wave detail or boat-scale interaction;
- ocean should eventually consume the planet local tangent frame, sea datum,
  bathymetry, shoreline fields, and local detail clipmap instead of becoming a
  planet-scale global tile system.

This means the next planet-scale work should harden Earth-like defaults and add
a local-detail planning contract before any ocean port. That contract should make
near cell size, outer extent, level count, and triangle budget visible so ocean
can later choose FFT/wave cascades against the mesh that will actually sample
them.

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
- `cubey::render::AdaptivePatchLod`: coverage-first quadtree patch planning,
  screen-error callback wiring, split/merge hysteresis, patch-budget fallback,
  neighbor LOD repair, edge-transition masks, and diagnostics;
- `cubey::render::TerrainOceanFieldView`: the first height/depth/shore/slope
  field contract used by terrain and ocean;
- `cubey::AtmosphereEnvironmentRuntime`: useful reference/runtime for
  ocean and atmosphere demos, but no longer the active planet sky owner;
- shared HDR post and performance UI contracts.
- project-local `PlanetUi` and `PlanetSurfaceRuntime` boundaries that keep
  control-panel layout, cube-sphere LOD adaptation, hysteresis history,
  diagnostics, render-origin validity, and instance-buffer upload state out of
  the app shell.

Do not promote ocean-specific classes wholesale yet. `OceanSurfaceFrame`,
`OceanHorizonDiagnostics`, and ocean shader surface mapping contain useful
ideas, but they are tied to `OceanConfig`, water datum, FFT sampling, horizon
fog, cascade LOD, and terrain-foam controls.

## LOD Direction

LOD should be established before ocean is ported.

The first planet surface LOD slice is now in place. Planet owns the cube-sphere
domain mapping, stable `face/level/x/y` addresses, terrain displacement/error
callbacks, horizon/view culling callbacks, mesh/skirt generation, shader packing,
and runtime instance-buffer upload. The shared `cubey::render::AdaptivePatchLod`
planner owns the reusable quadtree selection mechanics: coverage-first
refinement, screen-error thresholding, split/merge hysteresis, live-patch budget
fallback, neighbor repair to one LOD step, edge-transition masks, and CPU
diagnostics.

The reusable planner is intentionally not a terrain or planet renderer. It does
not know about cube faces, planet radii, bathymetry, streaming assets, mesh
layouts, or Vulkan resources. Those remain planet/project policy until a second
consumer proves a stronger shared contract.

Geometry clipmaps can remain useful for local viewer-centered data and for ocean
surface payloads. Planet terrain itself should get a more general patch tree
because terrain, bathymetry, cloud shadows, and streamed material fields need a
stable global address.

The global patch tree and the local detail clipmap therefore coexist. The patch
tree answers "which part of the planet is visible and what stable world data
does it reference?" The local clipmap answers "what meter-scale surface data is
needed around the viewer right now?"

## First Contracts

Add these in the planet project first. Promote narrow pieces only when they are
stable enough to stand without planet policy or when a second project uses the
same contract:

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
  limits. The instanced renderer currently accepts LOD 0-12, patch resolutions
  up to 128, and defaults to LOD 8 with a 32x32 reusable grid and a 6 px target
  edge. The older LOD 12 / 64x64 default is now treated as a stress/debug
  setting because it can push near-surface final rendering into multi-million
  triangle budgets on midrange GPUs. Surface-mode planning has an additional
  view-local target override that blends toward a coarser 14 px base target as
  the camera reaches the ground. This keeps the global cube-sphere responsible
  for continuous coverage while reserving meter-scale detail for the local layer
  once its final-view handoff is solved. The CPU mesh path still rejects
  configurations that would materialize too many vertices. Live planning has a
  fixed patch instance budget and falls back to coarser parent coverage when
  aggressive interactive settings would exceed it. Use the live renderer for
  interactive LOD pressure and the CPU mesh path for bounded validation.
- Camera-driven LOD replans no longer rebuild GPU instance buffers immediately
  or wait for the device. `PlanetSurfaceRuntime` owns the current patch plan,
  previous-selection hysteresis history, diagnostics, render-origin validity
  checks, and per-frame-slot instance-buffer generations. Each render frame
  lazily uploads the current instance list into that frame slot's buffer when
  its generation is stale. Full config rebuilds still drain and recreate
  resources because the reusable patch grid itself can change.
- A project-local local-detail clipmap now exists beside the global patch tree.
  It wraps `cubey::render::ClipmapGrid2D`, is anchored to
  `PlanetFrame.local_frame`, defaults to a 4 m near cell over an 8192 m outer
  half extent, and is the v1 near-field terrain detail layer. Its runtime plan
  is altitude-aware: levels whose projected cell size is subpixel are skipped,
  medium-altitude views start from a coarser center patch plus outer rings, and
  orbit-scale views allocate no local-detail mesh. Normal local-detail debug
  views are bounded by the configured/runtime local extent; `local-detail-horizon`
  is the explicit full-range diagnostic for horizon-scale inspection and global
  cutout. Local displacement is now a semantic residual over the global terrain
  field: ridge uplift, channel cuts, and plain undulation are gated by the same
  continent, relief, mountain, valley, plain, and land signals that shape the
  global terrain instead of by an unrelated local noise stack. The current live
  renderer lets the bounded clipmap contribute to default final rendering near
  the ground so surface view is not limited to global-scale terrain. Terrain-field
  debug views and explicit diagnostic views (`local-detail-wireframe`,
  `local-detail-blend`, `local-detail-lod`, `local-detail-height`,
  `local-detail-features`, and `local-detail-final`) inspect ownership, blend,
  active LODs, displacement, residual channels, and shaded bounded handoff.
  It is still procedural and project-local: no terrain streaming, ocean port,
  texture cache, persistent topology, morph policy, or compute terrain
  generation is part of this first consumer.
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
  normalized bathymetry, shoreline mask, land mask, normalized elevation,
  normalized slope, moisture, temperature, roughness, simple material bands, and
  terrain frequency bands. The frequency bands expose base continent/ocean
  shape, broad relief, mid-detail ridge/valley residuals, and fine residual
  detail; `terrain-band-base`, `terrain-band-relief`, and
  `terrain-band-detail` debug views inspect them. It now has named procedural
  landform layers: domain-warped continent/ocean structure, lowland breakup,
  ridge belts, valley cuts, and land/relief-gated fine detail, plus
  patch-cell-scaled normal sampling. Water classification is based on explicit
  sea level; bathymetry and shoreline are diagnostic fields, not yet a separate
  ocean layer. The terrain detail batch is tracked in
  [`planet-terrain-field-v2.md`](../notes/planet-terrain-field-v2.md): keep the
  source procedural and project-local, but make tile payload summaries and field
  diagnostics credible enough for later ocean, biome, cache, and streaming
  layers.
- Current debug views cover patch identity, LOD level, screen error, seam
  skirts, approximate metric cell edge, normalized terrain height, terrain
  frequency bands, normalized terrain slope, terrain material bands, bathymetry,
  shoreline, land mask, moisture, temperature, roughness, wireframe grid, LOD
  transition pressure, celestial-plane validation, and local-detail clipmap
  ownership, active LODs, displacement, semantic residual channels, bounded
  shaded handoff, and horizon-scale inspection. Default final rendering uses the
  same bounded near-field surface when the altitude-gated blend is active;
  `local-detail-final` keeps that handoff inspectable, and `local-detail-horizon`
  remains the full-range diagnostic.
- Planet rendering briefly moved away from the demo-oriented shared atmosphere
  background/runtime so planet orbit views could own solar time, sun/moon state,
  surface lighting, and body occlusion. The durable pieces have since been
  promoted into shared `cubey::render` celestial and atmosphere foundation code:
  `projects/planet` now adapts planet-scale frame and celestial inputs into the
  default `unified-atmosphere` backend while keeping explicit moon body geometry
  under planet control. Repeatable headless captures can pin the solar day,
  hour, clock pause state, and startup camera mode through descriptor-backed
  `RunConfig` options.
- The planet surface frame has moved out of push constants. Per-frame uniform
  data now carries view/projection, render origin, radius, terrain options,
  sea-level/bathymetry/shoreline options, camera render position, horizon
  distance, local sky light, and haze fields. Patch identity and screen-error
  remain per-instance data. This keeps room for planet-scale frame contracts
  without repeatedly repacking the 128-byte push constant budget.
- Planet rendering writes the unified atmosphere background, surface pass,
  explicit celestial body pass, and post pass through an HDR transient scene
  color target. This aligns planet with the renderer-wide linear HDR contract
  while keeping ocean integration deferred.
- The current atmosphere path uses the shared unified atmosphere as the default
  sky backend and keeps the old analytic/legacy paths as debug fallbacks. The
  landed v1 is a direct single-scattering model with spherical atmosphere
  intersections, Rayleigh/Mie vocabulary, sun transmittance, and surface aerial
  perspective. Sky background and surface haze should keep sharing the same
  planet-scale scattering inputs so surface view, orbit view, moon visibility,
  and future ocean/cloud layers do not invent separate formulas.
- Planet now uses shared minimal solar-system state: planet orbit around the
  sun, planet self-rotation, and moon orbit around the planet. The sun is still
  rendered by the unified atmosphere background as a distant disk/glow, while
  the moon is rendered as a depth-tested sphere on a camera-relative shell that
  preserves apparent angular size.
- The current solar-system math is deliberately mean and Earth-like rather than
  ephemeris-driven. `PlanetSolarTime` is a 24h mean solar clock. The default
  config uses a 23.9345h sidereal planet spin, 365.2422d tropical/seasonal
  year, 27.321661d lunar sidereal orbit, an explicit lunar phase epoch offset,
  and a signed synodic phase cycle of about 29.53d. This keeps day/year/moon
  behavior sensible while leaving eccentricity, equation of time, lunar
  nodal/apsidal precession, and Earth-Moon barycenter work for later.

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
  Payload summaries now include coverage, material-count, dominant material,
  climate, roughness, averaged height, averaged height above sea level,
  averaged normalized slope, height, slope, water-depth, and shoreline ranges,
  and their generator revision hashes terrain-relevant config so future cache
  invalidation is not tied only to the seed.
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
  now feeds the default `unified-atmosphere` sky backend through a local
  tangent-frame adapter. `sky-frame-legacy` keeps the old SkyFrame background as
  a short-term A/B fallback. `analytic` remains an atmosphere-mode fallback/debug
  option, while `physical` is the default v1 scattering vocabulary for planet
  atmosphere inputs until the LUT-backed path replaces it.
- Moon rendering remains explicit body geometry. The body pass now receives
  camera-relative shading inputs, applies procedural lunar albedo variation, and
  renders the moon as a depth-tested body with premultiplied visibility for
  phase coverage and daylight sky washout. Planet occlusion is still geometric,
  while the unlit/day-washed portions blend into smooth sky. The sky pass masks
  procedural stars behind the full rendered moon disk so blended moon phases do
  not behave like literal star transparency. Night-side terrain receives a small
  phase-scaled moonlight contribution. The eclipse shadow channel is
  intentionally zeroed until true node-aware lunar eclipses are modeled.
- Repeatable visual capture recipes live in
  [`docs/notes/planet-visual-captures.md`](../notes/planet-visual-captures.md)
  and cover orbit, surface, dawn/day/night, atmosphere comparison, LOD/seam
  diagnostics, celestial-plane checks, and surface-field debug views. A small
  CTest-backed subset now runs as headless PNG smoke coverage so baseline
  planet views, moon/daylight, wireframe LOD, and one terrain-field diagnostic
  cannot silently regress to empty output.

Current alignment by area:

| Area | Status |
| --- | --- |
| Frame and camera | Done as v1. Camera state is double precision, render origin is camera-relative, and frame data distinguishes datum altitude, sampled terrain height, and terrain-relative clearance. |
| Global surface LOD | Done as v1 and now routed through shared `cubey::render::AdaptivePatchLod`. Planet still owns cube-sphere mapping, terrain displacement bounds, horizon/view culling callbacks, mesh/skirt generation, shader packing, and GPU upload policy. |
| Procedural terrain field | Active contract, not just a visual placeholder. CPU tests, tile summaries, shader displacement, named terrain bands, material bands, water depth, shoreline, climate, and roughness use the same project-local vocabulary. |
| Local detail clipmap | Near-field surface layer. The altitude-gated clipmap now uses semantic ridge/channel/plain residuals over the global terrain field and can contribute to `final`, bounded local-detail, terrain-field, and opt-in shaded inspection rendering. `local-detail-horizon` is reserved for full-range horizon diagnostics. Local/global morphing, persistent topology, streaming, and ocean payloads remain explicit follow-ups. |
| Celestial and atmosphere | Done as v1. The planet project owns mean solar time, sun/moon state, planet-frame adapters, view-aware exposure, and moon body rendering while the unified foundation atmosphere is the default sky backend. `sky-frame-legacy` remains a comparison fallback. Atmosphere LUTs, physical transmittance assets, eclipses, and real ephemeris are deferred. |
| Streaming and resource residency | Deferred. Current runtime replans CPU patch lists and lazily uploads instance buffers, but it is not an out-of-core tile streamer or async residency manager. |
| Planet/ocean integration | Deferred. Ocean should port in as a local water layer once the planet frame, terrain field, sea datum, local detail clipmap, and render order are stable enough. |
| Project config facade | Deferred. Planet still routes through shared `RunConfig`; extracting project-owned CLI/config ownership is worthwhile once another project repeats the same option pressure. |

## Celestial Body Pivot

The unified atmosphere path is the default shared sky foundation for standalone
atmosphere demos, ocean-scale backgrounds, dynamic PBR environment lighting, and
planet skies. It consumes sun, moon, clock, and frame state, but it is not the
authoritative owner of those celestial bodies. That ownership lives in shared
`cubey::render` celestial/sky foundation code and planet-scale adapters: the mean
solar clock and Earth-like sun/moon mechanics are shared, the atmosphere
background frame samples generated night-sky/Milky Way atlas content, and the
explicit celestial-body frame renders depth-tested body geometry.
`projects/planet` is the first planet-scale consumer because orbit and surface
views need real occlusion, radius, phase, lighting, diagnostics, planetary
self-rotation, and eventually eclipses or multiple moons.
The `unified-atmosphere` sky backend renders the shared foundation atmosphere in
sky-only mode, receives planet-owned view/sun/moon directions through the tangent
adapter, and leaves explicit celestial body rendering under planet control. The
foundation atmosphere receives moon direction/radius/phase for star masking and
sky washout, but it does not draw the planet moon disk. The next production
atmosphere target is still a LUT-backed path: transmittance first, then sky-view
and multi-scattering LUTs, then shared aerial perspective for terrain/ocean.

The target ownership is:

- `cubey::render` owns `CelestialSystem` and shared atmosphere/sky/celestial
  frame helpers;
- `projects/planet` owns the planet-scale frame, terrain integration, and
  project-specific adapters around that shared state;
- celestial bodies own position/orbit state, physical radius, apparent angular
  radius, emission or albedo, texture/atlas references, sky participation, and
  debug labels;
- the sun is modeled first as a distant emissive body that produces a direction,
  angular radius, radiance/illuminance, and visible disk/glow;
- the moon is modeled as a spherical body lit by the sun, so phase and
  terminator behavior come from body geometry instead of an atmosphere shader
  approximation;
- moon phase remains a derived synodic diagnostic and lighting input; render
  phase should stay geometric unless a future impostor path needs a disk mask;
- lower-atmosphere daytime moon visibility should be contrast washout, not
  transparent blending of a sky sprite;
- moonlight is a secondary directional light derived from lunar phase and
  albedo, useful for night-side terrain even when its v1 intensity is
  demo-scaled rather than physically exposed;
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

The render-order contract for the current unified atmosphere path is:

1. unified atmosphere background pass with dark space, generated night-sky/Milky
   Way atlas content, stars, surface twilight, scattering, sun disk/glow, and
   planet horizon/limb behavior derived from planet frame inputs;
2. opaque planet surface, atmosphere surface terms, and future terrain/ocean
   layers;
3. explicit celestial body geometry, starting with a depth-tested moon sphere
   whose visible body contribution is premultiplied for phase and sky washout,
   plus a sky-pass star mask over the full rendered moon disk;
4. clouds, aerial-perspective overlays, and post as those systems arrive.

For the immediate slice, a distant sun disk/glow in the sky pass plus a
depth-tested moon sphere with blended visibility is enough. The sky pass may
mask stars behind the full moon disk, but no atmosphere shader decides celestial
placement, planet occlusion, moon phase, or moonlight. The shared
`AtmosphereEnvironmentRuntime` now has the frame inputs needed by planet while
existing atmosphere/ocean/glTF paths keep using the same foundation runtime.
`sky-frame-legacy` stays available only as an A/B fallback while the unified path
is hardened. Later work can move the sun to body-backed rendering or add
node-aware eclipses without changing the solar-system source of truth.

Established engine precedents support this split. Unreal's Sky Atmosphere
consumes scene Directional Lights marked as atmosphere lights, including
separate sun and moon indices. Godot's sky materials derive sun direction,
energy, and color from `DirectionalLight3D` nodes. Unity HDRP's physically based
sky consumes a Directional Light and treats sun-in-probe baking as a sky
environment option. Filament models sun/moon-style illumination as directional
lights with physical units. The common pattern is that sky/atmosphere rendering
consumes light/body state; it should not be the durable owner of planet-scale
celestial bodies.

Reference API/docs:

- Unreal Sky Atmosphere:
  <https://dev.epicgames.com/documentation/unreal-engine/sky-atmosphere-component-in-unreal-engine>
- Unreal Directional Lights:
  <https://dev.epicgames.com/documentation/unreal-engine/directional-lights-in-unreal-engine>
- Godot Sky Shaders:
  <https://docs.godotengine.org/en/latest/tutorials/shaders/shader_reference/sky_shader.html>

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

The first planet gap-closure, celestial, atmosphere, and terrain-field batches
are tracked in [`docs/notes/`](../notes/README.md). Keep temporary
implementation checkpoints there and promote only stable contracts back into
this architecture note.

Current next sequence:

1. Harden local/global terrain morphing so the bounded local-detail contribution
   in default final rendering no longer depends on a hard global cutout and does
   not expose local extent or ownership boundaries.
2. Define persistent topology plus cache/streaming contracts for terrain,
   bathymetry, and future ocean payloads.
3. Lock the terrain/ocean/atmosphere render-order and depth/field handoff.
4. Extract project-owned config ownership once another project repeats the same
   option pressure currently routed through shared `RunConfig`.
5. Port ocean as a local water layer once the planet frame, terrain field,
   local-detail, and render-order contracts are stable.

Completed foundation checkpoints include the initial planet viewer, cube-sphere
surface LOD, seam/skirt diagnostics, shared celestial/sky foundation, unified
atmosphere sky default with legacy fallback, visual smoke coverage, `PlanetUi` /
`PlanetSurfaceRuntime`, terrain-field v2, and semantic local-detail diagnostics.

Non-goals for the first planet pass:

- real-world GIS ingestion;
- full out-of-core streaming;
- global weather simulation;
- full N-body simulation;
- replacing the current FFT ocean core;
- moving existing ocean quality work behind planet infrastructure.

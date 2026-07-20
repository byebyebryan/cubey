# Ocean Adjacent Systems

`projects/ocean` is the active reference-derived deep-water renderer. The next
visible quality jump still depends on systems that should not be built directly
inside the ocean project first. Shorelines, shallow water, atmospheric
scattering, clouds, and planet-scale LOD each have enough policy and tuning
surface to deserve standalone project pressure before they become shared
renderer inputs.

This note captures the intended split so parallel work can stay mergeable.

## Direction

Build adjacent systems as separate projects first, then integrate them into the
ocean renderer through small data and shader contracts:

- `projects/atmosphere`: clear-sky scattering, atmosphere debug disks, and
  horizon aerial perspective, now also hosting the surface-only Cloud V1 layer
  through the shared cloud runtime.
- `projects/terrain`: active directly sampled heightfield terrain and clipmap
  rendering. It does not yet own bathymetry or shoreline products.
- `studies/terrain/hydrology`: paused regional terrain-product evidence for
  exported fields, routing, and future shoreline/bathymetry work.
- `projects/fluid_25d`: shallow-water simulation over heightfields for rivers,
  flooding, sources, sinks, and later dynamic shoreline coupling.
- `projects/planet`: whole-world scale, camera-relative origin, planet surface
  LOD, precision diagnostics, and eventual composition of atmosphere, terrain,
  ocean, clouds, and weather.

The active ocean renderer should remain the consumer of these systems, not their
first owner. That keeps the renderer focused on reference-quality water
presentation while the supporting systems become inspectable in isolation.

For planet scale, `projects/ocean` should stop at horizon-scale and curved-local
rendering. `projects/planet` now owns the landed frame, surface LOD,
terrain-field integration, atmosphere preview, and local-detail diagnostics
while consuming the shared sky/celestial foundation; the remaining integration
target is to port ocean as one local water layer when local/global morphing,
persistent topology, streaming, and render order are stable. The reusable
adaptive patch selection mechanics now live in `cubey::render`, but planet still
owns the cube-sphere mapping and world-scale policy.

## Atmosphere Project

The atmosphere path has the highest immediate visual leverage because ocean
reflection, sun glint, horizon fade, and background color all need one coherent
sky model. The standalone atmosphere project owns the clear-sky model, atlas
generation, and debug surface. The reusable background shader package lives
under `shaders/cubey/atmosphere`, and the consumer-facing
`AtmosphereEnvironmentRuntime` owns the shared environment-lighting bridge used
by glTF, water, and ocean. Future atmosphere model work should continue in the
standalone project before promotion into that shared runtime or individual
renderers.

First useful scope:

- deterministic clear-sky atmospheric scattering;
- sun direction, sun radiance, and debug sun disk controls;
- sky color lookup for view direction;
- approximate transmittance and aerial perspective along a view ray;
- headless PNG and MP4 captures for clear visual comparison.

Clouds should stay separate from the clear-sky shader. Treat them as an
additional layer with its own weather map, coverage, density, lighting, and
shadow controls rather than folding cloud noise into the base atmosphere pass.
The first cloud implementation is frozen in `projects/clouds_legacy`, where it
proved surface, above-cloud, and orbit pressure before any shared renderer
promotion. Active production cloud work now lives in `projects/atmosphere`
through `cubey::render::CloudLayerRuntime`.

Ocean integration target:

- sample the same sky model for background, reflection, fog, and sun glint;
- use one shared sun direction and radiance value;
- keep output in linear HDR before project-specific exposure and tone mapping.
- compare the integration against current `projects/ocean` captures before
  retiring any ocean-local fallback.

Current foundation checkpoint:

- shared atmosphere runtime now owns direct light direction/color/intensity,
  diffuse SH, runtime reflection capture, and the ground/sky-only policy needed
  by runtime sky capture;
- `gltf_viewer` consumes the shared runtime, renders the procedural atmosphere
  as the visible background, and uses runtime atmosphere reflection for dynamic
  specular environment lighting;
- `projects/ocean` consumes the same shared atmosphere run-config/time helper,
  renders the shared atmosphere background, samples runtime sky/reflection
  probes for water reflection/fog/fill, and scales foam/water lighting from
  atmosphere light energy;
- `gltf_viewer`, `projects/atmosphere`, and `projects/ocean` now all use the
  shared generated lunar and night-sky atlas upload path instead of
  project-local placeholder textures;
- `projects/ocean` still uses its own non-PBR water material. Surface-cloud
  shadows and planar/cached reflection are integrated; full terrain scene depth,
  bathymetry, and aerial-perspective composition remain future integration
  points.

## Terrain And Bathymetry Project

The ocean renderer needs land and underwater shape before shoreline water can
look convincing. That does not require a shallow-water solver on day one. The
active directly sampled terrain runtime establishes the height-query and clipmap
baseline, but a separate static bathymetry/shoreline product is still needed to
unlock shallow color, beach edges, and surf masks.

First useful scope:

- deterministic heightfield terrain generation;
- island, coast, shelf, and seabed profiles;
- material masks for sand, rock, vegetation, and underwater sediment;
- bathymetry texture where water depth can be sampled in ocean space;
- shoreline mask or coastline signed distance field;
- opaque terrain/scene color and depth output for water refraction tests.

Ocean integration target:

- use bathymetry for shallow-water attenuation, seafloor visibility, and wave
  damping;
- use shoreline masks for foam placement and beach-edge wetness;
- use scene color/depth as the input behind the single-layer ocean water pass.
- port legacy seafloor/refraction behavior only after the active wave core still
  matches or improves on the reference baseline.

`projects/terrain` should remain a directly sampled terrain renderer. Regional
products such as drainage, shoreline distance, and exported bathymetry belong
in a resumed product-generation track based on the paused
`studies/terrain/hydrology`; dynamic gameplay water belongs in
`projects/fluid_25d`. The earlier coastal field contract is preserved in
[`terrain-ocean-field-contract.md`](../archive/terrain/terrain-ocean-field-contract.md),
not as an implementation target.

Current foundation checkpoint:

- `cubey::render::TerrainOceanFieldView` is the shared terrain/ocean field
  contract for height, water depth, shoreline signed distance, slope, and
  material masks;
- the shared packer uploads that contract as one `RGBA32F` texture with
  `height_m`, `water_depth_m`, `shore_sdf_m`, and `slope` in `R/G/B/A`;
- the removed coastal demo proved an adapter from analytical coastal fields
  into this shared view, while `projects/ocean` can bind and inspect a
  generated diagnostic field texture through `terrain-depth`, `terrain-shore`,
  and `terrain-slope` debug views;
- the current ocean terrain field influence is intentionally opt-in and minimal:
  it proves the descriptor/shader boundary and a small shoreline foam hook, but
  it is not yet connected to `projects/terrain`, nor is it real
  bathymetry-driven surf or seafloor rendering.

## Fluid 2.5D Relationship

`projects/fluid_25d` is still the right home for terrain-bound water simulation.
It should not block the first terrain or ocean presentation work. Static
bathymetry and shoreline masks are cheaper and provide clearer visual signal for
the ocean renderer.

Once `fluid_25d` has stable shallow-water state, it can feed dynamic versions of
the same terrain-water contract:

- water depth;
- surface height;
- flow direction and speed;
- wet/dry mask;
- source/sink masks;
- optional foam or turbulence masks.

Those fields can later modulate shoreline foam, local current direction, river
mouths, and surf-zone behavior without requiring the ocean renderer to own the
solver.

## Planet Project Relationship

`projects/planet` is the right home for scale and LOD work that would otherwise
distort the ocean renderer. Its first useful scope should be deliberately plain:

- configurable planet radius, including small Kerbal-style scales;
- camera world position, local tangent frame, horizon, altitude, and far plane;
- cube-sphere or quadtree surface patches with wireframe and LOD colors;
- screen-error or distance/altitude-driven patch selection;
- seam handling through skirts or morph bands;
- patch counts, triangle counts, cell sizes, screen-error, and precision
  diagnostics.

Ocean integration target:

- planet supplies radius, datum, local tangent frame, camera-relative origin,
  atmosphere altitude, shared celestial body state adapters, and render order;
- ocean supplies local wave displacement, normals, foam, material response, and
  optional local wake/disturbance fields;
- terrain, bathymetry, clouds, and global weather remain planet or adjacent
  system inputs, not ocean-owned state.

## Shared Contracts

Before promotion, the projects should agree on a small set of shared assumptions.
These are more important than exact implementation details:

- world units and coordinate convention;
- camera-relative origin behavior for large ocean shots;
- planet radius, datum/sea level, and local tangent frame ownership;
- linear HDR color, exposure, and tone-map ownership;
- shared celestial body ownership, with derived sun direction, sun color/radiance,
  and sun intensity fed into atmosphere and surface lighting;
- depth convention for opaque scene depth, seabed height, water depth, and
  bathymetry;
- texture channel layouts for shoreline, bathymetry, terrain material, weather,
  cloud, and shallow-water fields;
- render order: atmosphere background, explicit celestial bodies where needed,
  opaque terrain/scene depth, cloud background/composite where appropriate,
  ocean water, then post layers. Ocean should consume cloud sky/reflection and
  cloud shadow outputs, not raymarch volumetric clouds inside the water shader.
- shared clipmap or patch-tree diagnostics for patch count, triangle/vertex
  totals, near cell size, screen error, and outer extent so terrain, ocean, and
  planet LOD can report the same concepts.

The initial contracts can remain project-local structs and GLSL includes. Promote
them to shared renderer or shader packages when they are stable enough to stand
without project policy or when at least two projects use the same contract
without special cases. The terrain-ocean field texture and static 2D clipmap
diagnostic helpers have crossed that multi-consumer threshold. The adaptive patch
LOD selection helper moved to `cubey::render` after the planet behavior
stabilized, but project-specific terrain, ocean, and planet policies remain
outside that helper.

## Current Integration Order

1. Keep Surface Ocean V1 and the atmosphere-hosted surface-cloud path stable as
   the visual baseline; aerial/orbit clouds remain a later-version track.
2. Define a current terrain water-body product for datum, bathymetry, shoreline
   distance, and opaque scene depth without expanding the active terrain source
   into a hydrology system.
3. Adapt that product into the existing terrain-ocean field contract, replacing
   the ocean's generated diagnostic texture without changing the wave core.
4. Add shallow attenuation, seafloor visibility, shoreline foam, and wet-edge
   composition in independently inspectable slices.
5. Resume `fluid_25d` only when dynamic terrain-bound water is required, then
   feed its state through the same contract rather than adding solver policy to
   ocean.
6. Continue `projects/planet` as the planet-frame, LOD, local sky, and celestial
   owner before making ocean itself planet-scale.

This order keeps the landed renderers independently useful and makes the next
cross-project dependency explicit instead of routing new work through legacy
projects.

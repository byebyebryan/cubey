# Ocean Adjacent Systems

`projects/ocean` is the active reference-derived deep-water renderer,
`projects/ocean_ref` is the known-good wave baseline, and
`projects/ocean_legacy` is the older feature donor. The next visible quality
jump still depends on systems that should not be built directly inside the
ocean project first. Shorelines, shallow water, atmospheric scattering, and
clouds each have enough policy and tuning surface to deserve standalone project
pressure before they become shared renderer inputs.

This note captures the intended split so parallel work can stay mergeable.

## Direction

Build adjacent systems as separate projects first, then integrate them into the
ocean renderer through small data and shader contracts:

- `projects/atmosphere`: clear-sky scattering, sun disk, horizon aerial
  perspective, and later procedural clouds.
- `projects/procedural_terrain`: heightfield terrain, bathymetry, shoreline
  masks, material masks, and terrain/scene depth rendering.
- `projects/fluid_25d`: shallow-water simulation over heightfields for rivers,
  flooding, sources, sinks, and later dynamic shoreline coupling.

The active ocean renderer should remain the consumer of these systems, not their
first owner. That keeps the renderer focused on reference-quality water
presentation while the supporting systems become inspectable in isolation.

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
- sun direction, sun radiance, and sun disk controls;
- sky color lookup for view direction;
- approximate transmittance and aerial perspective along a view ray;
- headless PNG and MP4 captures for clear visual comparison.

Clouds should come after the clear-sky path is stable. Treat them as an
additional layer with its own weather map, coverage, density, lighting, and
shadow controls rather than folding cloud noise into the base sky shader.

Ocean integration target:

- sample the same sky model for background, reflection, fog, and sun glint;
- use one shared sun direction and radiance value;
- keep output in linear HDR before project-specific exposure and tone mapping.
- compare the integration against `ocean_ref` before retiring the ocean-local
  sky fallback.

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
- `projects/ocean` still uses its own non-PBR water material. Full terrain
  depth, clouds, and aerial-perspective composition remain future integration
  points.

## Terrain And Bathymetry Project

The ocean renderer needs land and underwater shape before shoreline water can
look convincing. That does not require a shallow-water solver on day one. A
static procedural terrain and bathymetry source is enough to unlock much better
composition, shallow color, beach edges, and surf masks.

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

`projects/procedural_terrain` should stay rendering/data focused. Gameplay
water movement, rainfall, sources, sinks, and flooding belong in
`projects/fluid_25d`.

Current foundation checkpoint:

- `cubey::render::TerrainOceanFieldView` is the shared terrain/ocean field
  contract for height, water depth, shoreline signed distance, slope, and
  material masks;
- the shared packer uploads that contract as one `RGBA32F` texture with
  `height_m`, `water_depth_m`, `shore_sdf_m`, and `slope` in `R/G/B/A`;
- `projects/procedural_terrain` exports its analytical fields through this
  shared view, while `projects/ocean` can bind and inspect a diagnostic field
  texture through `terrain-depth`, `terrain-shore`, and `terrain-slope` debug
  views;
- the current ocean terrain field influence is intentionally opt-in and minimal:
  it proves the descriptor/shader boundary and a small shoreline foam hook, but
  it is not yet real bathymetry-driven surf or seafloor rendering.

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

## Shared Contracts

Before promotion, the projects should agree on a small set of shared assumptions.
These are more important than exact implementation details:

- world units and coordinate convention;
- camera-relative origin behavior for large ocean shots;
- linear HDR color, exposure, and tone-map ownership;
- sun direction, sun color, and sun intensity ownership;
- depth convention for opaque scene depth, seabed height, water depth, and
  bathymetry;
- texture channel layouts for shoreline, bathymetry, terrain material, weather,
  cloud, and shallow-water fields;
- render order: atmosphere background, opaque terrain/scene depth, ocean water,
  then optional cloud or post layers.
- shared clipmap diagnostics for patch count, triangle/vertex totals, near cell
  size, and outer extent so terrain and ocean LOD can report the same concepts.

The initial contracts can remain project-local structs and GLSL includes. Promote
them to shared renderer or shader packages when at least two projects use the
same contract without special cases; the terrain-ocean field texture and clipmap
diagnostic helpers have crossed that threshold and now live in `cubey::render`.

## Suggested Order

1. Build `projects/atmosphere` as a clear-sky scattering demo with headless
   capture output.
2. Build `projects/procedural_terrain` as a terrain and bathymetry data demo.
3. Refine ocean/material lighting now that atmosphere background, reflection,
   fog, and light-energy coherence are in place.
4. Integrate terrain scene color/depth plus bathymetry into ocean for shallow
   water and shorelines.
5. Continue `fluid_25d` toward dynamic terrain water, then feed its fields into
   the same shoreline/bathymetry contract.
6. Add clouds after clear sky and terrain composition are coherent.

This sequence keeps each project independently useful while aiming every slice
at concrete ocean integration points.

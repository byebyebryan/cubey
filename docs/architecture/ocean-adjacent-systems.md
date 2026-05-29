# Ocean Adjacent Systems

`projects/ocean` is already a useful deep-water renderer, but the next visible
quality jump depends on systems that should not be built directly inside the
ocean project first. Shorelines, shallow water, atmospheric scattering, and
clouds each have enough policy and tuning surface to deserve standalone
project pressure before they become shared renderer inputs.

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

The ocean renderer should remain the consumer of these systems, not their first
owner. That keeps the renderer focused on scalable water presentation while the
supporting systems become inspectable in isolation.

## Atmosphere Project

The atmosphere path has the highest immediate visual leverage because ocean
reflection, sun glint, horizon fade, and background color all need one coherent
sky model. The current ocean-local procedural sky is a placeholder; a standalone
atmosphere project should prove the model before it is promoted.

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

`projects/procedural_terrain` should stay rendering/data focused. Gameplay
water movement, rainfall, sources, sinks, and flooding belong in
`projects/fluid_25d`.

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

The initial contracts can remain project-local structs and GLSL includes. Promote
them to shared renderer or shader packages only after at least two projects use
the same contract without special cases.

## Suggested Order

1. Build `projects/atmosphere` as a clear-sky scattering demo with headless
   capture output.
2. Build `projects/procedural_terrain` as a terrain and bathymetry data demo.
3. Integrate atmosphere into ocean for background, reflection, fog, and sun
   lighting coherence.
4. Integrate terrain scene color/depth plus bathymetry into ocean for shallow
   water and shorelines.
5. Continue `fluid_25d` toward dynamic terrain water, then feed its fields into
   the same shoreline/bathymetry contract.
6. Add clouds after clear sky and terrain composition are coherent.

This sequence keeps each project independently useful while aiming every slice
at concrete ocean integration points.

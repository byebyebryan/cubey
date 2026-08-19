# Celestial Rendering Research

This note captures the design pivot triggered by the planet orbit-view sun disk
artifact. It is scratch context for the next implementation slice; the stable
architecture belongs in `docs/architecture/planet-rendering.md`.

Status: this is historical design context. The original recommendation to keep
`CelestialSystem` project-local has since been superseded; celestial state now
lives in shared `cubey::render` code and `projects/planet` adapts that shared
state into planet-scale rendering. The current kickoff map for sky work lives in
`docs/notes/sky-celestial-current-state.md`.

## Original Cubey Problem

`projects/planet` previously used the shared atmosphere background in `SkyOnly`
mode, but the atmosphere shader still owned sun and moon disk rendering. That
meant the shader had its own idea of ground/planet occlusion, while the planet
renderer also drew real surface geometry afterward. The result was an ownership
conflict: the sun could read as a masked hole or partially occluded disk even
though the planet surface should be the authority for what covers the sky.

The tactical shader fix reduced one branch of that issue, but it did not solve
the design problem. The atmosphere path was doing too much for planet scale:
scattering, background, environment lighting, sun disk, moon disk, lunar atlas
shading, stars, Milky Way, and some ground masking. The interim planet path
therefore started from a project-local sky/celestial pass. The current path has
promoted the durable pieces into shared celestial and unified-atmosphere code
while keeping planet-scale ownership in the planet adapters.

## External Precedents

Primary-source docs point toward a consistent boundary:

- Unreal's Sky Atmosphere is a scattering/sky component that consumes scene
  Directional Lights. Directional Lights opt into atmosphere interaction and can
  be indexed as sun and moon lights. Unreal also calls out per-pixel atmosphere
  transmittance for planetary views and shadows on nearby moons or other
  celestial objects.
- Godot's `PhysicalSkyMaterial` takes sun color, energy, and direction from the
  first `DirectionalLight3D`; `ProceduralSkyMaterial` supports up to four suns
  sourced from the first four `DirectionalLight3D` nodes.
- Unity HDRP's Physically Based Sky requires a scene Directional Light for
  physically correct setup and separately exposes whether the sun disk is
  included in sky environment/reflection probe baking. HDRP light data also
  carries sky interaction and celestial-body style properties such as angular
  diameter, surface texture, and moon phase.
- Filament treats sun/moon-style illumination as directional lights with
  physical units: directional lights use illuminance in lux, and a full moon is
  a useful low-end reference value. Its `SUN` light type and skybox shader are
  convenience render paths around light state, not a planet-scale body model.

The exact product choices differ. Unreal, Godot, Unity, and Filament can render
a sun disk inside the sky shader or skybox for convenience. The shared lesson is
that light/body state comes from scene lights or celestial records, and the
sky/atmosphere renderer consumes that state. For a planet-scale project, the
atmosphere should not be the source of truth for celestial bodies.

## Cubey Design Pivot

The original recommendation was to use a project-local `CelestialSystem` in
`projects/planet` first and promote it only after another project needed the
same model. That promotion has happened: the current code uses shared
`cubey::render::CelestialSystem` plus planet adapters.

Recommended v1 data model:

- `CelestialBodyId`: stable id for UI/debug and future references.
- `CelestialBodyType`: sun, moon, planet, star, generic.
- `radius_m`: physical radius for visible bodies.
- `position_world_m` or `direction`: finite body position when useful, distant
  direction for v1 sun.
- `angular_radius_rad`: derived apparent size for distant rendering.
- `emission`: radiance/illuminance/color for sun-like bodies.
- `albedo` and optional texture/atlas reference for moon-like bodies.
- `orbit`: simple procedural orbit or solar-clock adapter for v1.
- `debug`: visibility, labels, scale override, and path/orbit display.

Derived adapters:

- `AtmosphereScatteringInputs`: planet radius, atmosphere height, camera
  altitude, sun direction, sun radiance, sun angular radius.
- `CelestialLightingInputs`: directional light direction, color/radiance, moon
  fill if enabled.
- `SkyProbeInputs`: the subset needed to refresh reflection/ambient probes.

These adapters must not own body state. They are caches or frame uniforms built
from `CelestialSystem`.

## Render Contract

The earlier composition idea was atmosphere first, then explicit celestial
bodies. The interim plan was to use a planet-local sky/celestial pass until a
proper planet-scale atmosphere contract existed. The current implementation has
landed that contract through the default `unified-atmosphere` backend plus
planet-owned moon body rendering. The durable composition target is:

1. unified atmosphere background rendering that consumes planet-owned frame and
   celestial state for space, stars, sun disk/glow, and local limb behavior;
2. opaque planet terrain/ocean/cloud-shadow receivers;
3. explicit celestial body geometry, starting with a depth-tested moon sphere;
4. later cloud layers, aerial perspective over scene depth, and post.

The immediate sun implementation can remain a distant emissive disk and glow
drawn as a background body. The moon has moved to body geometry: a sphere placed
on the moon ray at a camera-relative shell distance, scaled to preserve apparent
angular size, and lit by the modeled sun direction. Body occlusion belongs to
the planet/celestial renderer, not the atmosphere scattering shader.

## Implementation Implications

- Historical warning: do not make `projects/planet` depend on a demo-oriented
  atmosphere sky clock or inline celestial-body ownership. The current
  `unified-atmosphere` backend is acceptable because it consumes planet-owned
  frame and celestial inputs through adapters.
- Keep `projects/atmosphere` free to show those disks as demo/debug features.
- Keep sun direction and time ownership in shared `CelestialSystem`; atmosphere
  adapters should receive resolved sun inputs.
- Keep repeatable capture control outside the shader path. A project's typed
  config facade can pin solar day/hour, pause the clock, and choose the initial
  orbit/surface camera mode for headless comparisons.
- Use one project-local atmosphere vocabulary before adding a full scattering
  model. The first implementation shares analytic terms for sun elevation,
  horizon shell, daylight, twilight, star visibility, and toward-sun weighting
  between the sky background and surface haze.
- Do not add a second vague "environment" owner. Environment/probe structs are
  derived renderer plumbing.
- Use the existing sun/time UI only as a migration source; the target UI should
  expose celestial body controls and derived atmosphere diagnostics separately.
- Treat the current planet clock as a mean Earth-like model, not an ephemeris:
  24h mean solar UI time, 23.9345h sidereal Earth spin, 365.2422d tropical
  year, 27.321661d lunar sidereal orbit, and a derived 29.53d synodic phase
  cycle. This is enough to validate sensible day/year/moon behavior before
  adding orbital eccentricity or precession.

## Sources

- Unreal Engine Sky Atmosphere:
  <https://dev.epicgames.com/documentation/unreal-engine/sky-atmosphere-component-in-unreal-engine>
- Unreal Engine Directional Lights:
  <https://dev.epicgames.com/documentation/unreal-engine/directional-lights-in-unreal-engine>
- Godot `DirectionalLight3D`:
  <https://docs.godotengine.org/en/stable/classes/class_directionallight3d.html>
- Godot `PhysicalSkyMaterial`:
  <https://docs.godotengine.org/en/stable/classes/class_physicalskymaterial.html>
- Godot `ProceduralSkyMaterial`:
  <https://docs.godotengine.org/en/stable/classes/class_proceduralskymaterial.html>
- Unity HDRP Physically Based Sky:
  <https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/physically-based-sky-volume-override-reference.html>
- Unity HDRP `HDAdditionalLightData`:
  <https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/api/UnityEngine.Rendering.HighDefinition.HDAdditionalLightData.html>
- Filament physically based lighting:
  <https://google.github.io/filament/main/filament.html>
- Filament red ball tutorial, showing `LightType.SUN` setup:
  <https://google.github.io/filament/webgl/tutorial_redball.html>
- NASA Glenn sidereal-time note:
  <https://www.grc.nasa.gov/www/K-12/Numbers/Math/Mathematical_Thinking/telling_time_by_the_stars.htm>
- NASA Earth facts:
  <https://science.nasa.gov/earth/facts/>
- NASA Moon facts:
  <https://science.nasa.gov/moon/facts/>
- NASA eclipse note on the Moon's orbit:
  <https://eclipse.gsfc.nasa.gov/SEhelp/moonorbit.html>

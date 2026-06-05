# Celestial Rendering Research

This note captures the design pivot triggered by the planet orbit-view sun disk
artifact. It is scratch context for the next implementation slice; the stable
architecture belongs in `docs/architecture/planet-rendering.md`.

## Current Cubey Problem

`projects/planet` uses the shared atmosphere background in `SkyOnly` mode, but
the atmosphere shader still owns sun and moon disk rendering. That means the
shader has its own idea of ground/planet occlusion, while the planet renderer
also draws real surface geometry afterward. The result is an ownership conflict:
the sun can read as a masked hole or partially occluded disk even though the
planet surface should be the authority for what covers the sky.

The tactical shader fix can reduce one branch of that issue, but it does not
solve the design problem. The atmosphere path is doing too much for planet
scale: scattering, background, environment lighting, sun disk, moon disk, lunar
atlas shading, stars, Milky Way, and some ground masking.

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

Use a project-local `CelestialSystem` in `projects/planet` first. Promote only
after another project needs the same model.

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

The intended planet composition is:

1. atmosphere scattering background without inline sun/moon disks;
2. planet-owned celestial rendering for sun/moon/star bodies;
3. opaque planet terrain/ocean/cloud-shadow receivers;
4. later cloud layers, aerial perspective over scene depth, and post.

The immediate sun implementation can be a distant emissive disk and glow drawn
as a background body. The planet surface can then cover it naturally where it
renders. A later moon implementation should choose either depth-aware geometry
or analytic planet/body occlusion in the celestial pass. In both cases, body
occlusion belongs to the planet/celestial renderer, not the atmosphere
scattering shader.

## Implementation Implications

- Add a switch or variant that lets atmosphere render scattering without inline
  sun, moon, star, and Milky Way disks for planet consumers.
- Keep `projects/atmosphere` free to show those disks as demo/debug features.
- Move planet sun direction and time-of-day ownership toward `CelestialSystem`;
  atmosphere should receive resolved sun inputs.
- Do not add a second vague "environment" owner. Environment/probe structs are
  derived renderer plumbing.
- Use the existing sun/time UI only as a migration source; the target UI should
  expose celestial body controls and derived atmosphere diagnostics separately.

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

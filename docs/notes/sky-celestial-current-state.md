# Sky and Celestial Current State

This note captures the starting context for the `sky-rendering` worktree before
new sky, sun, moon, stars, or Milky Way feature work. It is an orientation and
cleanup checklist, not a final architecture document.

## Current Ownership

The sky is not one module. It is a layered foundation with planet-specific
integration on top:

- `cubey::render::CelestialSystem` owns mean solar time, sun and moon state,
  rotation/orbit angles, moon phase, render placement, and derived lighting.
- `cubey::render::AtmosphereEnvironmentConfig` owns atmosphere, night sky,
  moon-disk, Milky Way, render-view, and debug-control inputs for the shared
  atmosphere shader.
- `cubey::render::AtmosphereBackgroundFrame` owns the fullscreen atmosphere
  draw, descriptor layout, and generated lunar/night-sky atlas bindings.
- `cubey::engine::AtmosphereEnvironmentRuntime` adapts atmosphere state into
  engine usage, including reflection-probe and PBR environment plumbing.
- `projects/atmosphere` is the standalone clear-sky and night-sky testbed.
- `projects/planet` owns planet-scale camera/frame state and adapts that state
  into the shared atmosphere backend.

The important rule is that atmosphere rendering consumes celestial state. It
should not become the source of truth for planet-scale sun, moon, phase,
occlusion, time, or lighting decisions.

## Active Render Paths

### Standalone Atmosphere

`projects/atmosphere` renders a fullscreen atmosphere pass and post pass. It can
draw the sun disk, moon disk, stars, and procedural Milky Way directly in the
atmosphere shader. This path is useful for shader iteration, debug views,
headless smoke captures, and generated atlas validation.

The standalone moon disk is a demo/debug feature. It should not be treated as
the planet ownership model.

### Planet

`projects/planet` uses the shared unified atmosphere path for its sky. The old
planet `SkyFrame` backend has been removed from runtime, CLI/config, tests, and
shaders.

The current planet render order is:

1. unified atmosphere background with scattering, twilight, generated stars,
   Milky Way, and sun disk/glow;
2. opaque planet surface and surface atmosphere terms;
3. explicit depth-tested moon body geometry;
4. post processing.

Planet disables the atmosphere shader moon disk. The shared atmosphere still
receives moon direction, angular radius, and phase so it can handle star masking,
night-sky washout, and related sky visibility, but the rendered moon belongs to
planet body geometry.

### Engine PBR Consumers

The shared atmosphere background can also be used as a forward PBR background
instead of an IBL skybox. `AtmosphereEnvironmentRuntime` provides the frame data
and derived environment bindings for those consumers.

## Current Sun, Moon, Stars, and Milky Way

The sun is currently drawn as a disk/glow in the atmosphere shader and also
drives directional lighting through `CelestialSystem` and derived lighting
inputs. It is not yet an explicit rendered body like the moon.

The moon currently has two paths:

- standalone atmosphere: shader disk using the generated lunar atlas;
- planet: explicit body geometry, phase/visibility behavior, and depth testing.

The migration target is a single app-visible moon path: explicit body geometry
using the generated lunar atlas as its appearance source. The atmosphere shader
should keep moon data for moonlight, star masking, washout, and atlas/debug
views, but it should not remain the default owner of the runtime visible moon.
The existing lunar atlas is a near-side disk atlas, so geometry samples it by
projecting surface normals into a moon-facing disk basis rather than by using
mesh UVs as a global equirectangular texture.

The first migration batch covers `projects/atmosphere`, `projects/planet`, and
the atmosphere-backed `fire_3d`, `explosion_3d`, and `water_3d` fluid views.
Forward PBR generic consumers, ocean, and reflection-probe cubemaps remain
explicit follow-up work unless they gain their own geometry insertion point.

Stars and the Milky Way are generated assets and shader procedures, not real sky
catalog data. The night-sky cubemap is deterministic and layered, with
procedural Milky Way structure, dust, star clouds, HII emission, and speckles.
The shader also adds foreground procedural stars and fades sky content through
twilight, horizon, light-pollution, and moon-washout controls.

The atmosphere is a v1 direct-scattering model with optical-depth sampling,
Rayleigh/Mie/ozone terms, transmittance, debug views, and surface aerial
perspective hooks. LUT-backed transmittance, sky-view, multi-scattering,
physical exposure calibration, eclipses, and real ephemeris remain deferred.

## Strengths To Preserve

- Celestial state is shared and testable instead of hidden in a shader.
- Atmosphere, lighting, reflection-probe, and planet adapters already have
  separate boundaries.
- `projects/atmosphere` gives sky work a focused iteration surface.
- `projects/planet` exercises the hard cases: orbit views, surface views,
  planet occlusion, moon body rendering, and surface lighting.
- Generated lunar and night-sky assets are deterministic and metadata-bearing.
- Legacy sky-frame captures remain useful as historical references, but runtime
  comparison now happens through the unified path and atmosphere-mode controls.

## Cleanup Outcome

The first cleanup batch before new feature work is complete. It reduced
ambiguity and test noise without changing sky art direction.

Completed:

1. Update stale notes that still describe `CelestialSystem` as project-local.
   The current code uses shared `cubey::render` celestial state, and the docs
   should not imply that planet owns a separate permanent model.
2. Make the active sky ownership explicit in docs: planet uses the unified
   atmosphere path rather than a selectable sky backend.
3. Record the moon ownership contract in one place: standalone atmosphere may
   draw a shader moon disk, but planet owns the rendered moon body.
4. Remove the legacy `SkyFrame` implementation after visual review confirmed it
   was only a comparison fallback.
5. Establish focused validation commands and capture recipes for sky changes.

Still deferred until dedicated feature work:

- moving the sun to explicit body geometry;
- replacing the procedural stars or Milky Way with catalog or panorama data;
- starting a LUT-backed atmosphere rewrite;
- adding eclipses or real ephemeris.

## Suggested Worktree Sequence

1. Documentation cleanup and current-state alignment.
2. Focused build/test/capture baseline for atmosphere and planet sky paths.
3. Small ownership cleanup if tests expose duplicated or stale plumbing.
4. Visual iteration in `projects/atmosphere`.
5. Integrated validation in `projects/planet`.

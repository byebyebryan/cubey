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
  moon visibility/lighting, Milky Way, render-view, and debug-control inputs
  for the shared atmosphere shader.
- `cubey::render::AtmosphereBackgroundFrame` owns the fullscreen atmosphere
  draw, descriptor layout, visible lunar surface map, and night-sky atlas
  bindings.
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
draw the sun disk, stars, and procedural Milky Way directly in the atmosphere
shader. Final-view moon rendering now uses the shared `CelestialBodyFrame`
geometry path with the generated spherical lunar surface map. The shader moon
disk and old near-side debug atlas have been removed.

The standalone atmosphere app is now an iteration surface for the same visible
moon geometry path used by planet and atmosphere-backed demos, not an equal
runtime shader-disk moon model.

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
geometry.

### Atmosphere-Backed Fluid Scenes

`fire_3d`, `explosion_3d`, and `water_3d` can use the shared atmosphere
background for their sky. These paths now draw the visible moon with
`CelestialBodyFrame` geometry inside the existing scene pass and pass a copied
atmosphere config with the inline shader disk disabled to the fullscreen
background shader.

### Engine PBR Consumers

The shared atmosphere background can also be used as a forward PBR background
instead of an IBL skybox. `AtmosphereEnvironmentRuntime` provides the frame data
and derived environment bindings for those consumers.

## Current Sun, Moon, Stars, and Milky Way

The sun is currently drawn as a disk/glow in the atmosphere shader and also
drives directional lighting through `CelestialSystem` and derived lighting
inputs. It is not yet an explicit rendered body like the moon.

The visible moon now has one canonical app path in migrated projects: explicit
body geometry, phase/visibility behavior, and a generated equirectangular lunar
surface map. The atmosphere shader keeps moon data for moonlight, star masking,
and washout, but it no longer renders a visible moon disk in planet, standalone
atmosphere final/debug views, or atmosphere-backed fluid views. The old
near-side disk lunar atlas has been removed.

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
- Generated lunar surface and night-sky assets are deterministic and
  metadata-bearing.
- Legacy sky-frame captures remain useful as historical references, but runtime
  comparison now happens through the unified path and atmosphere-mode controls.

## Planet GUI Issues Observed 2026-06-24

These were observed interactively in `projects/planet` after the
`sky-rendering` merge. They are recorded for a later diagnosis pass; no fix has
been attempted yet.

- A black band can appear between the terrain horizon and sky at some camera
  altitudes or viewing angles.
- In surface view, rotating the camera rotates the surface correctly, but the
  sky appears static, as if the surface and sky view frames disagree.
- In orbit view, stars appear to be missing or washed out. This may be exposure
  related, but the cause is not confirmed.
- In orbit view, the sun glare has visible banding and does not yet read like a
  clean solar glow.

## Cleanup Outcome

The first cleanup batch before new feature work is complete. It reduced
ambiguity and test noise without changing sky art direction.

Completed:

1. Update stale notes that still describe `CelestialSystem` as project-local.
   The current code uses shared `cubey::render` celestial state, and the docs
   should not imply that planet owns a separate permanent model.
2. Make the active sky ownership explicit in docs: planet uses the unified
   atmosphere path rather than a selectable sky backend.
3. Record the moon ownership contract in one place: the visible moon should be
   geometry in migrated apps, and the shader disk/fallback plumbing can be
   removed once debug views move to geometry.
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

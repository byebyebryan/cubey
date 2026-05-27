# Ocean Rendering Direction

`projects/ocean` is the practical large-scale water renderer path. It is not a
replacement for `projects/fluid/water_3d`: the tank project starts from
particle-grid liquid simulation, while ocean starts from a camera-relative
surface renderer and only adds simulation where interaction needs it.

## Current Shape

- Generate a projected grid in the vertex shader so the surface follows the
  camera and concentrates vertices near the viewer.
- Evaluate waves in world space so the mesh can move without swimming the
  pattern.
- Use multiple directional wave bands with non-commensurate wavelengths and
  distance-filtered short-wave detail to avoid obvious repetition to the
  horizon.
- Shade the surface with Fresnel sky reflection, fake refraction/absorption,
  foam, tonemapping, and debug views.
- Keep explicit hooks for local disturbances and shoreline/bathymetry masks.

## Interaction Path

The renderer should expose inputs that later systems can feed:

- local disturbances for boat wakes, impacts, and gameplay ripples;
- shoreline/bathymetry masks for depth attenuation, surf foam, and shallow-water
  coupling;
- optional foam/spray/whitewater fields that can be visual-only or simulation
  backed.

V1 keeps these as compact controls and shader inputs. Boat physics, buoyancy,
shoreline authoring, and shallow-water simulation should arrive as separate
projects or later integration slices.

## Breaking Waves

Deep-water whitecaps are reasonable in the renderer: detect steep/choppy crests,
add foam, and later spawn spray particles. True plunging shore breakers are not
just a shader feature. They need terrain or bathymetry, shallow-water/surf-zone
state, nonlinear local wave deformation, and often particles or volumes for
spray and aerated water.

The recommended sequence is:

1. Render choppy waves plus crest foam.
2. Add local wake/disturbance textures.
3. Add terrain/bathymetry and shoreline foam.
4. Couple to `fluid_25d` or a local surf simulation for actual breaking waves.

This keeps v1 useful while avoiding an early ocean project that pretends visual
foam is full wave breaking.

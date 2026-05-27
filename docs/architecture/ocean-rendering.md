# Ocean Rendering Direction

`projects/ocean` is the practical large-scale water renderer path. It is not a
replacement for `projects/fluid/water_3d`: the tank project starts from
particle-grid liquid simulation, while ocean starts from a camera-relative
surface renderer and only adds simulation where interaction needs it.

## Current Shape

- Generate a projected grid in the vertex shader so the surface follows the
  camera and concentrates vertices near the viewer.
- Evaluate a multi-cascade spectral wave field in world space so the mesh can
  move without swimming the pattern.
- Use an in-repo Stockham FFT path for deep-water height, choppy horizontal
  displacement, slope, curvature, and crest-compression fields, with
  decorrelated cascade sampling to avoid obvious repetition to the horizon.
- Layer bounded nonlinear macro crests over the spectral field so near-camera
  waves have asymmetric crests instead of smooth sine-like hills.
- Draw a project-local procedural sky first, then shade the surface against that
  same atmosphere model for coherent horizon fog, reflection, and sun glint.
- Shade the surface with Fresnel sky reflection, fake refraction/absorption,
  spectrum-derived normals, persistent crest foam, tonemapping, and debug views.
- Keep explicit hooks for local disturbances and shoreline/bathymetry masks.

## Rendering References

The current renderer is not trying to port a single reference shader. It borrows
the useful shape of established approaches:

- Tessendorf-style FFT spectra for scalable deep-water motion.
- GPU Gems water guidance: split geometric displacement from surface detail,
  filter short wavelengths by sampling footprint, use Gerstner-style choppy
  displacement for sharper crests, and use depth plus Fresnel to control
  reflection/refraction.
- TDM/Inigo Quilez-style `Seascape` presentation lessons: coherent sky color,
  strong directional sun reflection, nonlinear wave shape, and fogging the far
  surface into the sky.

The important distinction is that `projects/ocean` keeps a mesh-backed renderer
that can later interact with scene objects. Pure ray-marched seascapes can look
excellent in a standalone shader, but they do not give us the same path toward
boats, shorelines, depth buffers, and scene integration.

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

1. Render spectral waves plus generated crest foam.
2. Add local wake/disturbance textures.
3. Add terrain/bathymetry and shoreline foam.
4. Couple to `fluid_25d` or a local surf simulation for actual breaking waves.

This keeps v1 useful while avoiding an early ocean project that pretends visual
foam is full wave breaking.

# Atmosphere Rendering Research

This note is initial research for the `atmo-rendering` branch. It should stay a
working note until the implementation direction is proven in `projects/atmosphere`
and then promoted into `docs/architecture/`.

## Current Cubey Context

`projects/ocean` currently owns a local procedural sky gradient and sun disk in
`projects/ocean/shaders/ocean_atmosphere.glsl`. Ocean already samples that same
function for the sky pass, Fresnel reflection, horizon fog, and sun glint, which
is the right integration shape. The missing piece is that the sky function is not
yet a real atmosphere model.

`docs/architecture/ocean-adjacent-systems.md` already points at the intended
split: build `projects/atmosphere` as a standalone clear-sky scattering demo,
then let ocean consume a small shared sky/transmittance contract.

## Terms That Matter

- Rayleigh scattering: scattering by air molecules. It is strongly wavelength
  dependent, which gives clear daylight sky its blue bias and makes low sun
  paths shift warmer after more blue light is removed.
- Mie scattering: scattering by aerosols and haze. It is much more forward
  directed and mostly controls sun halos, horizon haze, low-contrast gray skies,
  and humid or dusty looks.
- Absorption: ozone and other absorbers remove parts of the spectrum without
  adding scattered light. Ozone matters for richer blue-hour and sunset behavior.
- Transmittance: how much light remains after traveling through the atmosphere.
  It is needed both from sun to sample point and from sample point to camera.
- Aerial perspective: in-scattering plus extinction along a view ray toward
  terrain, ocean, clouds, or other scene geometry.
- Multiple scattering: light that scatters more than once. Single scattering is
  a good first milestone, but multiple scattering strongly affects horizon
  brightness and atmosphere energy conservation.

The user's memory of "Ryleigh / Mie" is the right starting point, with the
spelling corrected to Rayleigh. The practical production feature set is larger:
Rayleigh, Mie, absorption, transmittance, aerial perspective, sun disk
luminance, and at least an approximation for multiple scattering.

## Reference Families

### Analytic Sky Models

Preetham/Shirley/Smits and Hosek-Wilkie style analytic skies are compact and
fast. They are useful references for daylight color and turbidity controls, and
can be good enough for a skybox. They are less ideal as the main Cubey target
because ocean needs a shared model for sky color, sun color, horizon fog,
reflection, and scene-distance aerial perspective.

Use this family only if the first milestone needs an extremely small fallback or
comparison mode.

### Direct Ray-Marched Single Scattering

The GPU Gems 2 / O'Neil path numerically integrates atmospheric scattering in a
shader and is a good learning implementation. It gives direct visibility into
Rayleigh and Mie terms, spherical planet geometry, exponential density profiles,
and ray intersections with the atmosphere shell.

This is the best first implementation slice for Cubey because it can start as one
fullscreen sky shader plus CPU-side parameters, produce headless comparison
captures, and later become the validation path for LUT approximations.

### Bruneton/Neyret Precomputed Atmosphere

Bruneton's revised implementation is the strongest reference for a physically
structured precomputed atmosphere. It supports spectral-to-RGB conversion,
custom density profiles, ozone, Mie storage options, and tests that compile the
GLSL-like math on the CPU for dimensional and behavioral checks.

This is valuable as a correctness reference, but likely too large for the first
Cubey slice unless the project immediately needs ground-to-space views and
stable high-quality multiple scattering.

### Hillaire / Unreal Sky Atmosphere

Hillaire's production approach is the best target architecture for real-time
engine integration. It avoids high-dimensional precomputed tables, uses smaller
runtime LUTs, supports dynamic atmosphere parameters, and separates sky lookup,
transmittance, multiple scattering, and aerial perspective quality controls.

This is the recommended long-term Cubey target. Start with direct single
scattering, then add Hillaire-style LUTs once the visual contract is clear.

## Recommended Cubey Direction

1. Create `projects/atmosphere` as a standalone clear-sky demo.
2. Implement a CPU-owned `AtmosphereConfig` with physical-ish defaults:
   bottom radius, top radius, Rayleigh scattering coefficients and scale height,
   Mie scattering/extinction coefficients and scale height, Mie anisotropy `g`,
   ozone absorption profile, ground albedo, sun angular radius, sun direction,
   and sun irradiance/exposure controls.
3. Render a fullscreen sky from camera ray direction using spherical atmosphere
   intersection and direct single-scattering integration.
4. Keep all intermediate radiance in linear HDR and apply Cubey's existing
   display transform only at the end.
5. Add debug views before tuning:
   Rayleigh contribution, Mie contribution, optical depth/transmittance,
   atmosphere shell intersection, sun disk luminance, and aerial perspective
   over a fake depth ramp.
6. Add headless PNG and MP4 captures for several fixed cases:
   noon, low sun, sunset, high haze, thin air, and high-altitude camera.
7. Once single scattering is stable, add small LUTs:
   transmittance LUT first, sky-view LUT second, multi-scattering LUT third,
   aerial-perspective froxel/volume LUT only after scene depth integration
   needs it.
8. Promote only the compact contract to ocean:
   `sun_direction`, `sun_radiance`, `sky_radiance(view_direction)`,
   `transmittance(camera_pos, world_pos)`, and
   `aerial_perspective(camera_pos, world_pos)`.

This gives ocean an immediate improvement without making ocean own atmosphere
policy, clouds, or terrain composition.

## Non-Goals For The First Slice

- Clouds. They should be a later layer with separate coverage, density, shadow,
  and weather controls.
- Full weather. Haze/turbidity through Mie density and anisotropy is enough for
  v1.
- Perfect spectral rendering. Use RGB coefficients first, keep source references
  clear enough to add spectral precompute later if needed.
- General volumetric fog. The first aerial-perspective path should prove ocean
  horizon and terrain-distance composition, not become a broad fog system.
- Space rendering as the main target. The model should not break above the
  atmosphere, but the first visual target is ocean/terrain from near ground.

## Open Questions

- Should `projects/atmosphere` be a pure demo project first, or should its GLSL
  include and C++ config live under shared `cubey/render` from day one?
- Should the first atmosphere shader use absolute planet-scale coordinates, or a
  camera-relative origin plus large-radius math wrapper?
- Which capture matrix should become the regression baseline: color thumbnails,
  numeric probe values, or both?
- How much of Bruneton's CPU-testable shader style is worth adopting for Cubey
  GLSL includes?

## Sources

- Sean O'Neil, GPU Gems 2 Chapter 16, "Accurate Atmospheric Scattering":
  <https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-16-accurate-atmospheric-scattering>.
- Eric Bruneton, revised "Precomputed Atmospheric Scattering" implementation:
  <https://ebruneton.github.io/precomputed_atmospheric_scattering/>.
- Sebastien Hillaire, "A Scalable and Production Ready Sky and Atmosphere
  Rendering Technique": <https://diglib.eg.org/items/8a3e5350-18b3-46bd-9274-3add5af88c75>.
- Sebastien Hillaire reference repository:
  <https://github.com/sebh/UnrealEngineSkyAtmosphere>.
- Unreal Engine Sky Atmosphere documentation:
  <https://dev.epicgames.com/documentation/unreal-engine/sky-atmosphere-component-in-unreal-engine>.
- Preetham, Shirley, and Smits, "A Practical Analytic Model for Daylight":
  <https://dblp.org/rec/conf/siggraph/PreethamSS99>.
- Hosek and Wilkie, "An Analytic Model for Full Spectral Sky-Dome Radiance":
  <https://cgg.mff.cuni.cz/projects/SkylightModelling/HosekWilkie_SkylightModel_SIGGRAPH2012_Preprint_lowres.pdf>.

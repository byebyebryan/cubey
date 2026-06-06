# Planet Atmosphere V1

This note started as the immediate planet-atmosphere plan and now serves as the
checkpoint for the landed v1. The project-local analytic sky and distance haze
have been replaced as the default by a small planet-scale scattering model that
supports surface, orbit, dawn/night, and later clouds or ocean without changing
celestial ownership again.

## Direction

`projects/planet` remains the source of truth for the solar clock, sun, moon,
camera frame, planet radius, and atmosphere radius. Atmosphere rendering
consumes those values; it does not own sun/moon placement, phase, occlusion, or
time of day.

The landed implementation is direct single scattering with explicit
spherical atmosphere intersections, Rayleigh and Mie phase terms, approximate
optical depth, sun transmittance, and aerial perspective for surface geometry.
This is intentionally smaller than a Bruneton/Hillaire LUT stack, but it should
use the same vocabulary so a future LUT path can replace the approximation
without changing the planet-facing contract.

## V1 Success Criteria

- surface and sky use the same sun direction, planet radius, atmosphere radius,
  and scattering vocabulary;
- surface day, dawn, night, and orbit limb views do not show abrupt black/blue
  bands or detached red/orange halos;
- low sun visibly warms the horizon and attenuates direct surface light;
- stars fade in daylight and remain visible at night/high altitude;
- the old analytic path remains available as a debug fallback while the
  physical path is the default.

## Current Limits

- orbit exposure now uses a view-aware analytic light-fraction proxy, but it is
  still not rendered scene luminance or histogram exposure;
- moon/day-sky visibility has a coherent v1 sky-visibility term, but not a
  full physical lunar transmittance or eclipse model;
- visual validation is mostly manual capture recipes plus broad PNG smoke
  checks.

## Deferred

- precomputed transmittance, sky-view, multi-scattering, or aerial-perspective
  LUTs;
- clouds, cloud shadows, volumetric weather, and ocean composition;
- physical unit calibration beyond stable HDR-friendly defaults;
- user-facing Rayleigh/Mie tuning sliders before the model has settled.

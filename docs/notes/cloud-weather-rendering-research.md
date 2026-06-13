# Cloud And Weather Rendering Research

This note captures the first implementation direction for Cubey clouds. It is
intended to keep the next project practical for game-style performance while
still being compatible with ocean high cameras and planet orbit views.

## References

- Unreal Engine Sky Atmosphere and Volumetric Clouds: use planet-scale sky
  parameters, separate cloud tracing quality from atmosphere setup, and expose
  shadow/reflection sample controls instead of treating clouds as a skybox.
- Unity HDRP Volumetric Clouds: drive cloud coverage from cloud maps and expose
  quality/temporal tradeoffs so a project can choose resolution and sample
  budget.
- Guerrilla/Nubis real-time cloud work: model clouds from authorable weather
  and density fields, then keep raymarching heavily budgeted through sparse
  sampling, lighting approximations, and temporal reuse.

Useful URLs:

- https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-in-unreal-engine
- https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@latest/manual/Override-Volumetric-Clouds.html
- https://www.guerrilla-games.com/read/the-real-time-volumetric-cloudscapes-of-horizon-zero-dawn

## Direction

Clouds should not be folded into the clear-sky atmosphere shader. The shared
clear-sky atmosphere remains the base for sun, moon, sky color, exposure, and
environment lighting. Clouds are an additional weather layer with its own
coverage, density, shape, lighting, and shadow controls.

The first implementation should be a standalone `projects/clouds` pressure
project. It should consume the shared atmosphere solar-clock and lighting
foundation, but it should own cloud policy until the model is stable enough for
promotion into shared renderer code.

## Scale Model

Use one shared, planet-aware weather model with different render paths by camera
context:

- Surface: high-quality local view through a curved cloud layer, with horizon
  composition and cloud shadows.
- High altitude: camera may be inside or above the cloud layer and must be able
  to look down onto clouds.
- Orbit: render broad weather masses on a spherical cloud shell with lower local
  detail and strong terminator/shadow readability.

A flat slab is acceptable only as an internal debug simplification. The default
path should use a spherical shell from the start so ocean high cameras and
planet orbit captures do not become separate rewrites.

## Performance Rules

- Render clouds as a separate fullscreen pass at quarter, half, or full
  resolution; composite the result over the atmosphere background.
- Use bounded view and light sample counts tied to quality presets.
- Use single scattering, Beer transmittance, and a cheap edge/powder term before
  attempting expensive multiple scattering.
- Use early-out when transmittance gets low.
- Treat temporal accumulation as a quality/performance feature, reset when
  camera mode or major cloud parameters change.
- Do not raymarch clouds inside ocean or PBR material shaders. Consumers should
  sample composited sky/cloud output, a reflection/environment result, or a
  cloud shadow texture.

## Initial Contracts

The first shared vocabulary should stay small:

- weather map: coverage, type, density, wind, and storminess;
- cloud shell: planet radius, bottom altitude, top altitude, and layer flags;
- lighting: sun/moon direction and radiance from shared atmosphere/celestial
  state;
- outputs: sky/cloud color, approximate transmittance, cloud shadow factor, and
  debug views for weather, density, lighting, shadow, and step count.

Ocean and planet integration should wait until the standalone project proves the
surface, high-altitude, and orbit cases.

## V1 Validation Findings

The first standalone clouds checkpoint is useful as a pressure project, but it
is not ready to feed other renderers. The known blockers are:

- visible seams in the procedural cloud/weather field;
- awkward project-local controls instead of shared hierarchical/config-driven
  controls;
- no FPS/frame-time or raymarch workload diagnostics in the UI;
- recurrence of the atmosphere horizon band because the prototype owns a local
  sky/ground composition path.

Treat these as prerequisites for promotion. The likely next direction is a
seam-safe spherical weather/domain representation, shared performance and
config UI integration, and reuse of the fixed shared atmosphere composition
path instead of duplicating sky composition inside the cloud shader.

The second clouds checkpoint adds the first seam-safe spherical weather domain,
shared performance UI, and explicit prototype composition terms for background
atmosphere, procedural ground, cloud alpha, and cloud-shell ray coverage. That
makes the horizon band and shell framing diagnosable inside the standalone
project.

The third checkpoint adds a real offscreen cloud color target and composite
pass, so `quarter`, `half`, and `full` quality now change both sample budgets
and rendered cloud pixels. It also retunes the default inspection weather toward
broken clouds, uses orbit-aware detail suppression so planet views preserve
broad weather masses, and switches non-ground background rays to the shared
`cubey_atmosphere_classify_sky_background_ray` helper.

The fourth checkpoint splits the offscreen cloud target into linear cloud
radiance plus view transmittance, moves background/ground/atmosphere composition
into the composite pass, adds a standalone analytic surface-shadow diagnostic,
and softens high-altitude cloud contribution near the horizon. These are still
project-local prototype outputs rather than a shared renderer API.

The fifth checkpoint makes the cloud model typed without promoting it out of the
project. `fair-weather`, `broken-cumulus`, `overcast-stratus`, `storm-cells`,
and `high-cirrus` are canonical presets; older names remain aliases. Presets now
set coverage, density, weather scale, wind, cloud-layer altitude, and an
internal cloud-style id passed through the existing push constant footprint. The
raymarch pass and composite surface-shadow diagnostic share the same
`clouds_model.glsl` density/weather implementation, with procedural fronts,
cells, calm gaps, wind-aligned streaks, type-specific height profiles, and
style-aware lighting. The added `projects/clouds/capture_review.sh` helper
captures the standard surface/high/orbit/debug bundle before future tuning.

The sixth checkpoint addresses the first proper-fix batch. The standalone
composite now separates sky/space background from the diagnostic ground proxy,
so sky rays are no longer ground-occluded just because the prototype has a
ground surface. Surface and high cameras use a finite local cloud volume plus a
cheap distant horizon layer, while orbit cameras keep the spherical shell.
`domain` and `distance` debug views expose the active path and march coverage.
Cloud products resolve through per-frame-slot temporal reconstruction using
history textures that are safe for the host's frame-slot model. Night lighting
uses shared atmosphere sun intensity and scalar moon ambient intensity, so
night clouds should read as silhouettes unless the shared environment reports
moonlight.

The seventh checkpoint focuses on the surface/high horizontal streaking seen in
local-volume captures. It adds explicit raw/temporal isolation, base-density and
detail-density diagnostics, density-LOD, step-length, local-march, and
far-horizon views. The density model now reports a broad base field separately
from high-frequency erosion, fades detail toward the base field for distant or
grazing local rays, increases samples only for local horizon rays, widens
stochastic march jitter in that region, and replaces the old single-sample
horizon fallback with a short broad-field integration. The pass reduces hard
banding and makes the source diagnosable, but the remaining surface streaks show
that the finite local volume still needs a more robust reconstruction strategy,
likely a better per-pixel ray integration filter, blue-noise/temporal
reprojection strategy, or a separate horizon/cloud impostor.

The eighth checkpoint tightens the diagnostic and geometry basis before deeper
sampling work. Raw cloud-product debug views now bypass final composition
consistently, so density, alpha, shell, local-march, and related diagnostics can
be inspected without sky/ground lighting hiding the source. Surface and high
cameras now intersect the same spherical cloud shell shape as orbit mode, then
cap the march to the local distance budget rather than using a flat altitude
slab. Local surface/high quality also keeps at least half vertical cloud target
resolution in `quarter` mode. This removes one obvious local/orbit geometry
split and reduces vertical undersampling near the horizon, but captures still
show residual row-like streaking. Treat the next fix as a sampling/reprojection
or horizon-reconstruction problem, not just a tuning issue.

The remaining promotion blockers are now less about first visibility and more
about renderer contracts:

- promote cloud radiance/transmittance out of the standalone project so ocean
  and planet can compose clouds with their own scene passes;
- promote the current analytic sun-shadow factor into a reusable cloud shadow
  texture or sampled lighting input for surface and ocean lighting;
- improve temporal/blue-noise sampling and horizon reconstruction before
  increasing quality or promoting the local-volume path;
- decide whether weather authoring is procedural-only, texture-driven, or a
  hybrid with uploaded weather maps;
- move useful controls onto the shared hierarchical config/UI surface.

Ocean should consume cloud reflection/background/shadow outputs. Planet should
consume the same weather model at orbit and surface scale. Neither should embed
the volumetric raymarch in a water, terrain, or PBR material shader.

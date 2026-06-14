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
- Toft/Bowles/Zimmermann real-time volumetric cloudscapes: jittered sparse
  raymarching, temporal accumulation, and better per-step integration reduce
  sample artifacts, but do not replace a good cloud/domain model.
- NVIDIA spatiotemporal blue noise notes: stochastic sampling should use a
  distribution designed for temporal filters instead of independent hash noise.

Useful URLs:

- https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-in-unreal-engine
- https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@latest/manual/Override-Volumetric-Clouds.html
- https://www.guerrilla-games.com/read/the-real-time-volumetric-cloudscapes-of-horizon-zero-dawn
- https://arxiv.org/abs/1609.05344
- https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-1/

## Direction

Clouds should not be folded into the clear-sky atmosphere shader. The shared
clear-sky atmosphere remains the base for sun, moon, sky color, exposure, and
environment lighting. Clouds are an additional weather layer with its own
coverage, density, shape, lighting, and shadow controls.

The first implementation should be a standalone `projects/clouds_legacy` pressure
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
style-aware lighting. The added `projects/clouds_legacy/capture_review.sh` helper
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

The ninth checkpoint fixes one concrete source introduced by anisotropic local
cloud target scaling: the raymarch pass now traces camera rays with the final
view aspect ratio instead of the scaled cloud texture aspect. It also swaps the
local raymarch to pixel/frame stratified ray-start jitter, increases adaptive
samples only for local grazing rays, fades local detail density earlier for
distant/grazing samples, and adds a final-only lower-sky vertical filter for
local horizon composition. This makes the default surface view materially less
streaky while keeping raw diagnostics unfiltered. The raw density buffer still
shows row-like structure, so a future pass should replace the local horizon
march with a better reconstruction or reprojection strategy rather than only
adding more blur.

The tenth checkpoint adds the first proper reconstruction substrate. The
renderer can now create dynamic passes with multiple color attachments, and the
cloud raymarch writes a metadata target next to the color/transmittance product:
mean cloud distance, cloud alpha, horizon factor, and reconstruction
confidence. The temporal pass keeps color and metadata ping-pong histories,
uploads current/previous camera and weather state through a uniform buffer,
reprojects history from the current cloud-distance estimate, rejects history
when depth/alpha/confidence disagree, and clamps accepted history to the current
neighborhood. The cloud density model also accepts an approximate pixel
footprint, fades high-frequency erosion by that footprint, uses per-step dither
for local grazing rays, and blends a broader low-frequency local horizon layer.
This is the right mechanical base for fixing horizon streaking, but the
captures remain only subtly improved; the remaining artifact should be treated
as a deeper sampling/reconstruction issue, not as a solved tuning problem.

## Reference Code Pass 2026-06-13

The following external projects were cloned beside Cubey for source review:

- `/home/bryan/code/Mesh-Cloud-Rendering`
- `/home/bryan/code/UnityVolumetricCloudsURP`
- `/home/bryan/code/CloudRenderer`

`Mesh-Cloud-Rendering` is not a volumetric renderer. It uses authored cloud
meshes, precomputed per-vertex occlusion/transmittance data, a half-resolution
cloud target, quarter-resolution blur/distortion passes, and a final composite.
That makes it a poor source for local volumetric low-cloud marching, but a
useful reminder that distant clouds do not have to come from the same expensive
raymarch as nearby clouds. A stable mesh, impostor, or shell layer with
intentional blur/distortion could be a better horizon/far-cloud path than a
grazing finite-volume march.

`UnityVolumetricCloudsURP` is the most relevant implementation reference. It is
a URP port of Unity/HDRP-style volumetric clouds and reinforces several
patterns Cubey should keep or adopt:

- keep cloud lighting/transmittance in a low-resolution target and carry cloud
  depth/mean distance as a first-class output for composition;
- use temporal history with neighborhood clipping/rejection, but do not expect
  temporal filtering to fix a bad raw march;
- expose separate shape, erosion, micro-erosion, coverage, density, curvature,
  primary-step, light-step, and temporal-accumulation controls;
- separate local/world clouds from skybox-style clouds instead of forcing one
  path to solve all distances;
- use cloud-map/LUT style authoring for broad coverage/type before high
  frequency erosion;
- blur or filter cloud shadow maps separately because shadow integration often
  uses far fewer steps than the main view march.

`CloudRenderer` is a straightforward OpenGL volume raycaster with cellular
automata density, fixed view samples, and per-sample light rays. It is useful as
an intentionally simple baseline, but it is not a practical scale/performance
model for Cubey's ocean/planet horizon cases.

The previous Cubey attempts and this reference pass point to the same
conclusion: the visible surface/high streaking is not primarily a temporal
resolve issue. Surface raw/final captures are close enough that the row/ring
structure should be treated as part of the raw local horizon signal. The next
implementation pass should stop tuning around that artifact and split the cloud
representation by distance regime:

- near/overhead: keep a bounded volumetric raymarch for clouds that have real
  parallax and thickness;
- mid/far horizon: introduce a low-frequency weather-map or shell/impostor
  layer that fades in before grazing local-volume rays dominate the image;
- high/cirrus: keep this as a cheap shell/2D layer unless a real volumetric
  need appears;
- diagnostics: add isolation toggles for base weather, fronts/cells/streaks,
  detail erosion, and horizon fallback so the source of each visual feature is
  visible without touching shader code.

Blue-noise or spatiotemporal-blue-noise sampling, analytical integration, and
better temporal rejection are still valid follow-ups. They should refine a good
distance-regime model, not act as the primary fix for far-field horizon
streaking.

The eleventh checkpoint implements the first version of that distance-regime
split. Cloud frame data moved from maxed-out push constants into a per-frame
uniform buffer so the shader can carry explicit feature-isolation controls.
The weather model now exposes broad, front, cell, and streak components, with
CLI/UI/config controls for local volume, horizon layer, weather feature
weights, and detail erosion. The old composite-only vertical lower-sky blur was
removed.

The surface horizon now fades dense local-volume marching out of lower-sky
grazing rays and replaces it with a dedicated low-frequency horizon layer. This
made the source easier to isolate, but current visual checks still show
noticeable surface/high streaking, high-oblique horizon separation, and rough
orbit readability. Treat the latest tuning as diagnostic, not as a solved
cloud model. The remaining artifact should not be hidden by yet another final
blur or one-off horizon filter.

## Reference Reboot Pass 2026-06-13

The newer reference checkout lives under `/home/bryan/code/ref`. The most
useful projects for rebooting Cubey clouds are:

- `/home/bryan/code/ref/TerrainEngine-OpenGL`
- `/home/bryan/code/ref/godot-volumetric-cloud-demo`
- `/home/bryan/code/ref/godot-volumetric-cloud-demo-v2`

`TerrainEngine-OpenGL` is useful because it is C++/OpenGL and has a relatively
direct fullscreen compute path. Its cloud renderer is built around classic
Horizon/Decima-style ingredients:

- precomputed 3D Perlin-Worley base shape texture;
- precomputed 3D Worley erosion texture;
- separate 2D weather map for broad coverage/type;
- spherical cloud shell intersection for camera-inside, inside-layer, and
  above-layer cases;
- fixed 64-step view raymarch with Bayer ray-start jitter;
- six-sample cone tracing toward the sun for light transmittance;
- Beer transmittance, powder term, height-gradient cloud type profiles, early
  exit, and distance fog into the sky background.

The important lesson is not that Cubey should copy its exact tuning. The lesson
is that the density model is texture-backed and stable before lighting and
composition happen. Cubey's current inline value-noise weather and erosion path
has too many coupled heuristics, so tuning composition keeps exposing raw
domain artifacts.

`godot-volumetric-cloud-demo` is the compact baseline. It raymarches the sky
directly in a Godot sky shader, uses Perlin-Worley + Worley + weather textures,
uses cloud-type height gradients, stacks multiple Henyey-Greenstein phase
functions, and fades distant clouds into the sky near the horizon. It is useful
as the smallest readable shader to port conceptually, but it still does the
expensive sky march in the visible shader.

`godot-volumetric-cloud-demo-v2` is the better architecture reference for
Cubey. It moves the expensive march into a compute pass that writes an
octahedral hemisphere cloud texture. It updates only one tile per frame,
spreads a full sky update across a configurable number of frames, keeps three
textures, and blends between old/new complete sky textures in the sky shader.
The final sky shader only samples blended cloud textures, samples physical sky
and transmittance LUTs, draws the sun disk, and fades clouds into the background
near the horizon.

This points to a reboot direction:

- stop treating `projects/clouds_legacy/shaders/clouds.frag` as the final product;
- add a texture-backed cloud model first: uploaded or generated
  Perlin-Worley, Worley erosion, and 2D weather maps;
- add a cached sky/cloud producer, likely hemisphere-octahedral at first, with
  region/tile updates and triple-buffered blending;
- keep the old per-view raymarch only as a diagnostic or close/local mode until
  it proves useful;
- make surface, high, and orbit consume the same cached cloud/sky product
  before reintroducing local parallax details;
- reuse the shared atmosphere sky/transmittance basis for lighting instead of
  maintaining project-local sky hacks.

The current uncommitted surface/orbit tuning should be considered disposable
unless a small mechanical fix is independently valuable. The next real change
should be a controlled prototype of the reference-style cached cloud product,
not another incremental tweak to the current local horizon march.

The first reboot target is `projects/cloud_ref`. Its initial checkpoint is a
small texture-backed cached-product baseline: uploaded deterministic 2D weather
texture, fixed 512x512 cloud product, and final compositing over a lightweight
sky/ground background. It intentionally limits review to surface and high
camera modes before adding tiled/triple-buffered cache updates, 3D
Perlin-Worley shape textures, Worley erosion textures, or orbit/planet-scale
clouds. The older implementation is frozen in `projects/clouds_legacy` for
side-by-side comparison.

The remaining promotion blockers are now less about first visibility and more
about renderer contracts:

- promote cloud radiance/transmittance out of the standalone project so ocean
  and planet can compose clouds with their own scene passes;
- promote the current analytic sun-shadow factor into a reusable cloud shadow
  texture or sampled lighting input for surface and ocean lighting;
- improve horizon-layer shape/lighting before promotion; temporal/blue-noise
  sampling is still useful, but the current surface artifact is no longer being
  hidden by a final composite blur;
- decide whether weather authoring is procedural-only, texture-driven, or a
  hybrid with uploaded weather maps;
- move useful controls onto the shared hierarchical config/UI surface.

Ocean should consume cloud reflection/background/shadow outputs. Planet should
consume the same weather model at orbit and surface scale. Neither should embed
the volumetric raymarch in a water, terrain, or PBR material shader.

## TerrainEngine Port Checkpoint

`projects/cloud_ref` now uses the TerrainEngine-style reference path rather
than the earlier 2D weather-only checkpoint. It generates a 128^3
Perlin-Worley base volume, a 32^3 Worley erosion volume, and a 1024^2 weather
map at startup, then runs a compute cloud march into a radiance/transmittance
product before fullscreen compositing. The port keeps the core TerrainEngine
mechanics: spherical shell intersections, cloud-type height gradients, detail
erosion, Bayer start jitter, Beer transmittance, and a short cone light march.

The port is still a standalone reference, not the final cloud architecture.
God rays/bloom, temporal reconstruction, tiled cache updates, and Godot-v2-style
sky-cache blending remain follow-up work. The default camera is high-oblique so
the generated cloud deck is visible immediately; surface/orbit modes are still
diagnostic review angles.

## TerrainEngine Fidelity Baseline

The original TerrainEngine app now builds locally and has been captured under
Xvfb/llvmpipe. Use `outputs/terrainengine-ref-capture/contact-sheet.png`,
`frame-8s.png`, and `frame-14s.png` as the current source-look baseline. These
captures show coherent chunky cumulus shape, high cloud contrast, strong
water/sky interaction, and some low-resolution/post-process texture artifacts.
Those artifacts should not be tuned away inside `cloud_ref` until the port is
source-faithful enough to compare mechanically.

The next `cloud_ref` work should prioritize fidelity over Cubey integration:
TerrainEngine noise generation, mipmapped 3D textures, density/coverage math,
camera-relative shell intersections, source-like lighting/fog/composition, and
reference defaults. Cubey-specific atmosphere, ocean, planet, temporal cache,
and weather-system changes should happen later in the production `cloud`
project.

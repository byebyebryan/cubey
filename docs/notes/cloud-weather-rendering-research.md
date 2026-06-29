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
  frequency erosion, but keep local 3D noise responsible for visible cloud
  placement and silhouette detail;
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

## TerrainEngine Cloud Ref Port 2026-06-14

`projects/cloud_ref` is now the direct TerrainEngine-OpenGL reference track,
separate from the older `projects/clouds_legacy` prototype. The port is meant
to be a known-good mechanical baseline rather than the final Cubey weather
architecture.

Landed mechanics:

- generated 3D volume textures can now allocate mip chains and generate mips
  after compute writes;
- cloud_ref base/detail volume samplers use those mips, matching the OpenGL
  source's `glGenerateMipmap(GL_TEXTURE_3D)` behavior;
- Perlin-Worley, Worley, and weather map generation follow TerrainEngine's
  source formulas instead of Cubey's earlier simplified value-noise model;
- cloud_ref defaults use TerrainEngine's 600 km sphere radius, 800 m surface
  camera, 5 km cloud base, 17 km cloud shell thickness, fixed sun direction,
  source cloud colors, and full-resolution 64-step quality;
- the raymarch uses the TerrainEngine local sphere-center model, source density
  coverage/erosion math, Bayer jitter, Beer transmittance, and short cone light
  march.

Captured outputs:

- original TerrainEngine app: `outputs/terrainengine-ref-capture/`;
- current Cubey port: `outputs/cloud-ref-faithful-port/`.

Current read: the source-space port produces coherent chunky cloud masses and
does not show the old local-volume horizontal streaking as the dominant
failure. It is still gray, visibly single-frame dithered, and missing
TerrainEngine's water, skybox, bloom, god rays, and post chain. Treat future
work in two branches: keep `cloud_ref` source-faithful for comparison, and use
a separate production cloud/weather project to integrate shared atmosphere,
ocean/planet scale, temporal reconstruction, and richer lighting.

## Cloud Ref Presentation Checkpoint 2026-06-14

`cloud_ref` now has a presentation-parity layer around the source-like volume
path. It adds shader-only sky/water horizon context, a final-view alpha-aware
cloud resolve, mild sun halo/contrast shaping, and a `raw-final` diagnostic that
composites the raw cloud product over the same context without final resolve or
post. The new review bundle is `outputs/cloud-ref-presentation-review/`.

The checkpoint improves high and high-oblique readability substantially because
cloud masses now sit over a water-colored scene instead of a flat placeholder.
The straight `surface` camera remains a quiet horizon review angle; use
`surface-up` to inspect surface cloud form. This is still not TerrainEngine's
full presentation stack: real water rendering, skybox textures, bloom FBOs,
god rays, and separate cloud-distance outputs remain unported.

## Production Cloud Sampling Checkpoint

`projects/cloud` now exposes deterministic static ray-start sampling as a
first-class control instead of baking Bayer jitter into the marcher. The default
mode is screen-space interleaved-gradient jitter, with Bayer retained for
reference comparison and `off` mapped to center-of-step sampling. A
`clouds.jitter_strength` control blends any selected pattern back toward center
sampling.

The final composite resolve now reads the cloud metadata target as well as the
cloud radiance/transmittance product. `final` resolves only across neighboring
samples with compatible opacity, mean distance, and confidence, while
`raw-final` remains an unfiltered view of the direct cloud product. This keeps
the final view useful for presentation without hiding whether an artifact comes
from the raw march.

Use `projects/cloud/capture_review.sh` to compare `surface-up`, `high-oblique`,
their Bayer/no-jitter variants, and raw-final variants in one bundle. If
interleaved, Bayer, and off still share the same cloud-shape failure, treat the
issue as density/weather or distance-regime architecture rather than sampling
noise.

## Production Cloud Type Checkpoint

The center-view tower artifact in `projects/cloud` was traced to the raw cloud
product, not final resolve or atmosphere composition. The weather map already
stored cloud type, but the production marcher did not expose it in diagnostics.
A trial that used cloud type to lower isolated weather islands into flatter
profiles made the cloud deck too sparse and was not retained. The added
`cloud-type` diagnostic and capture-review center crops make this class of
extrusion visible without editing shader code.

The retained production fix is scale-aware top shear. The source reference uses
a small fixed height-dependent wind offset, but in this port local positions are
projected against planet radius, so that offset was too small to break vertical
coherence. Scaling the top shear as a fraction of the weather domain breaks the
straight tower silhouette while preserving reference coverage and cumulus height
profile math.

The next production pass split that coupling. `clouds.weather_scale_km` now
drives the horizontal weather/type sampling domain as an approximate broad
feature size, while `clouds.vertical_shear_fraction` controls altitude-dependent
wind shear. Weather and cloud-type diagnostics remain raw midpoint map probes;
`visible-density` and `visible-cloud-type` raymarch the view ray to show the
maximum visible density and density-weighted cloud type. This makes it clearer
whether a problem lives in the authored weather map or in the visible density
integration.

Planar `position.xz` weather projection over the spherical cloud shell remains
a likely cleanup once local cloud shape is stable and planet-scale handoff is
ready.

## Production Weather Authoring Checkpoint

The current production `cloud` weather map is useful but too blunt as a final
authoring model. It stores coverage and type, but the marcher mostly treats the
weather sample as a single scalar coverage gate. That explains the abrupt
transition between dense cloud cover and clear sky: broad weather, edge
softness, cloud type, and high-frequency erosion are coupled into one remap.

The references point to a bounded next step rather than a full meteorological
simulation:

- `TerrainEngine-OpenGL` / `cloud_ref`: generated 2D coverage/type maps are a
  good baseline, but direct scalar coverage remapping is not enough.
- Godot / `cloud_ref_2`: cloud type should drive the height-density profile.
- `clouds_legacy`: broad, front, cell, streak, and calm components are useful
  weather-authoring ideas even though the renderer should not be copied back.
- Unity/HDRP-style systems separate coverage, type/profile, rain/extinction,
  max height, and erosion/shape curves before lighting.

The production direction is therefore a small authoring texture, not "real
weather": coverage in R, cloud type in G, edge breakup/softness in B, and a
reserved alpha channel. Debug views should expose both the authored channels and
the post-threshold coverage mask so tuning failures are visible before final
composition hides them.

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

The first foundation promotion pass moved common `CloudLayer*` contracts,
shader assets, generated-resource helpers, atmosphere backdrop composition, and
an ocean cloud-shadow diagnostic into shared/consumer code. The remaining
promotion blockers are less about first visibility and more about full renderer
ownership:

- turn the shared cloud contract into a reusable runtime so ocean and planet can
  compose real cloud radiance/transmittance products with their own scene
  passes;
- promote the current analytic sun-shadow diagnostic into a real reusable cloud
  shadow texture or sampled lighting input for surface and ocean lighting;
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

## Cloud Ref Lighting Checkpoint

The current `cloud_ref` pass keeps the TerrainEngine-style density and shell
march intact and focuses only on cloud lighting/color. The march now accumulates
separate ambient, direct, and phase/rim lighting terms, exposed as
`ambient-light`, `direct-light`, and `phase-light` debug views. The default
presentation enables powder, lowers ambient wash and distance fog, warms direct
sunlight, and applies a small contrast-preserving final pass.

This made the reference easier to diagnose and modestly less flat in
`surface-up` and `high-oblique` captures, but it did not fully recreate the
original TerrainEngine look. The remaining gap is mostly presentation context
and architecture: TerrainEngine's terrain, water reflection, bloom/god rays,
and post stack carry much of the final image richness. Further `cloud_ref`
constant tuning should be secondary to deciding whether the production cloud
project needs a cached sky/cloud product, real weather layering, and shared
atmosphere/ocean composition outputs.

## Cloud Ref 2 Cache Validation Pass

`projects/cloud_ref_2` is now scoped as a Godot-v2 cached-sky architecture
validation target rather than a visual-fidelity port. The cache path uses
Godot-style hemisphere octahedral encode/decode, explicit cache diagnostics, and
CLI-configurable cache cadence/texture size. The unsafe direction-blind 3x3
cache-space final resolve was removed because it could blur across oct folds and
hide or amplify vertical artifacts.

The new synthetic views isolate cache mechanics from cloud density:

- `cache-direction` checks decoded direction continuity.
- `cache-horizon` checks horizon/fold behavior.
- `cache-checker` checks cache tile and update-grid seams.
- `cache-alpha` checks the transmittance/alpha channel convention.

Because the cache is upper-hemisphere only, the composite path now treats
below-horizon rays as transparent cloud instead of sampling the clamped horizon
cache. This prevents horizon cloud data from smearing through the lower half of
surface-view captures.

Use `projects/cloud_ref_2/capture_review.sh outputs/cloud-ref-2-review` for a
normal review and `CACHE_MATRIX=1 projects/cloud_ref_2/capture_review.sh
outputs/cloud-ref-2-cache-matrix` for the camera/cache cadence/cache size sweep.
If synthetic cache views are clean while final cloud views still show repetition
or smeared shape, classify the remaining problem as source texture/weather/density
data rather than cached-sky architecture. Godot `.tga`/`.bmp` texture import and
sky/transmittance LUT parity remain deferred.

## Cloud Ref 2 Direct Validation Path

`cloud_ref_2` now has a direct composite render path that evaluates the same
march function used by the cache compute shader against the current screen ray.
This is a validation oracle for the cached sky architecture, not a proposed
runtime path. The default `cached` mode still samples the persistent
octahedral sky product; `direct` bypasses the cache; `diff` and `alpha-diff`
show amplified cached-vs-direct cloud product differences before final grading.

Interpret the diff views carefully:

- disable cloud motion with `--cloud-wind-speed-mps 0` for strict comparisons;
- moving-cloud captures compare a multi-frame cached update against the current
  direct frame, so they will show temporal mismatch;
- sharp cloud edges can differ even in static captures because the cached path
  is a filtered finite-resolution octahedral texture while the direct path
  evaluates the exact screen ray.

The useful acceptance check is therefore: cached and direct finals should read
as the same cloud model, synthetic cache views should remain continuous, and
diff views should be used to locate cache resolution/filter/update artifacts
rather than to judge final visual quality directly.

## Production Direction Promotion

The reference passes have now been promoted into
[`docs/architecture/cloud-rendering.md`](../architecture/cloud-rendering.md).
That architecture note is the current direction for `projects/cloud`.

Summary:

- stop tuning `clouds_legacy` as a production base;
- keep `cloud_ref` as the visual/density-shape reference;
- keep `cloud_ref_2` as the cached-sky architecture reference;
- start production cloud work with texture-backed coherent density and shared
  atmosphere/config integration before adding the cached hemisphere path.

The post-review refinement is that clouds consume shared sky, celestial,
atmosphere, and exposure inputs; they do not make atmosphere the owner of sun,
moon, or planet-scale celestial policy. The first production project should also
keep planet-frame compatibility, product alpha/transmittance semantics, and
project-owned render-graph resources explicit before any shared cloud renderer
promotion.

## Production Distance Regime Checkpoint 2026-06-16

The current production `projects/cloud` surface view is credible enough to stop
treating every high-view artifact as a local raymarch tuning problem. High and
orbit captures reveal a separate failure mode: the visible dome/circular cloud
boundary is the projected footprint of the same spherical cloud-shell segment
that works for surface views. The artifact appears in cloud alpha/distance
diagnostics rather than in the background, so it should be fixed as a
distance-regime problem.

External references line up with that read:

- Unreal Volumetric Clouds targets ground, flying, and outer-space views, but
  keeps cloud tracing quality, shadow tracing, sky atmosphere, and aerial
  perspective as separate scalable systems:
  <https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-component-in-unreal-engine?lang=en-US>
- Unity HDRP clouds split cloud maps, volume shaping, erosion, temporal
  accumulation, and quality controls instead of driving all distances from one
  final opacity heuristic:
  <https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/understand-clouds.html>
- Horizon/Nubis emphasizes authorable weather plus density fields and strict
  performance budgets before lighting/post can make clouds look rich:
  <https://www.guerrilla-games.com/read/the-real-time-volumetric-cloudscapes-of-horizon-zero-dawn>
- Frostbite frames clouds as one weather layer inside a dynamic sky and
  atmosphere system, not as a skybox or material-local effect:
  <https://www.ea.com/frostbite/news/physically-based-sky-atmosphere-and-cloud-rendering>
- Skybolt is the closest surface-to-space pressure case: it uses a spherical
  cloud shell, global coverage map, procedural detail, aerial perspective, and
  cloud shadow/occlusion products for planetwide clouds:
  <https://prograda.com/2021/07/28/rendering-planetwide-volumetric-clouds-in-skybolt/>

The next production pass should therefore keep the surface-local volume stable
and add an explicit high/orbit regime:

- surface/near/overhead: keep the current local volumetric raymarch for real
  parallax, thickness, and shape;
- high/orbit: add a broad shell evaluator that samples the existing weather map
  and low-frequency density with suppressed erosion and a cheaper sample budget;
- transition: expose an explicit local-vs-orbit blend diagnostic rather than
  hiding the handoff in color grading;
- cache: defer the full octahedral cached-sky product until the direct broad
  shell path reads correctly.

Implementation should add `clouds.distance_mode = auto|local|orbit-shell|blend-debug`
and a small set of altitude/detail controls. The acceptance target is not final
planet integration; it is removing the obvious high/orbit dome boundary while
preserving the current surface view.

## Orbit Weather/Planet-Scale Reference Pass 2026-06-17

The next problem is no longer just local cloud quality. Orbit captures still
read too much like procedural noise over a toy sphere instead of coherent
planet-scale weather. The practical target is a model where broad coverage and
cloud organization come from a planet-space weather product, and local
volumetric noise only sculpts that product.

New reference checkouts under `/home/bryan/code/ref`:

- `Skybolt`: <https://github.com/Prograda/Skybolt>
- `godot-volumetric-clouds`: <https://github.com/kb173/godot-volumetric-clouds>
- `godot-planet-fly-through-cloud-volume`:
  <https://github.com/appxmod/godot-planet-fly-through-cloud-volume>
- `volumetric_cloud_atmosphere_scattering`:
  <https://github.com/leoawen/volumetric_cloud_atmosphere_scattering>

Skybolt is the primary reference because it is explicitly built for clouds seen
from the planet surface, outer space, and the transition between them:
<https://prograda.com/2021/07/28/rendering-planetwide-volumetric-clouds-in-skybolt/>.
The relevant local code paths are:

- `/home/bryan/code/ref/Skybolt/Assets/Core/Shaders/Clouds.h`
- `/home/bryan/code/ref/Skybolt/Assets/Core/Shaders/VolumeClouds.frag`
- `/home/bryan/code/ref/Skybolt/Assets/Core/Shaders/CloudShadows.h`
- `/home/bryan/code/ref/Skybolt/Assets/Core/Shaders/AtmosphericScatteringWithClouds.h`
- `/home/bryan/code/ref/Skybolt/Assets/Globe/Environment/Cloud/cloud_combined_README.txt`

Important Skybolt lessons:

- Planet-scale coverage is sampled from a global cloud coverage map in spherical
  planet-relative UVs. The included map is based on NASA Blue Marble Clouds.
- Detail coverage is a repeated high-frequency modulation layer. It is not the
  source of the macro layout.
- The density hull is formed from base coverage, height profile, and detail
  coverage. Expensive 3D volume noise is only sampled once the low-resolution
  hull says the ray is inside possible cloud.
- Detail modulation preserves mean coverage across scales. This is exactly the
  behavior we want to avoid the "coverage becomes either nothing or a solid cap"
  failure mode.
- The high/orbit path can fall back to a low-resolution global alpha/color
  evaluation from coverage mip levels rather than fully raymarching every distant
  cloud feature.
- The same global coverage product also feeds cloud shadow and sky occlusion,
  including atmosphere in-scattering occlusion.
- Seam handling matters for equirectangular U wrap. Skybolt explicitly guards
  LOD calculation across the seam.

The Godot references are useful secondary checks, not primary architecture
targets:

- `godot-volumetric-clouds` uses a weather texture whose channels drive density,
  rain/darkness, and type/scale. This matches the direction that weather should
  control local density parameters rather than directly draw final cloud opacity.
- `godot-planet-fly-through-cloud-volume` has useful shell intersection,
  blue-noise jitter, and fly-through mechanics, but the shader is less clear as a
  weather organization reference.
- `volumetric_cloud_atmosphere_scattering` is useful as a compact WebGL/Three.js
  planetary prototype with weather, TAA, and atmosphere composition ideas. It is
  not yet as trustworthy as Skybolt for coverage architecture.

Implementation implications for `projects/cloud`:

- Add explicit orbit weather debug views before changing lighting: global
  coverage, detail modulation, density hull, low-res orbit alpha, and
  local-vs-orbit blend.
- Introduce a planet-space weather sampler separate from the surface-local 3D
  density sampler. It should return coverage, type/height, density scale, and
  erosion/detail strength.
- Build orbit clouds from coverage/hull first, then apply constrained detail
  erosion. Do not let repeated 3D noise define the planet-scale pattern.
- Use the same orbit weather product as the future source of broad cloud shadows
  and sky occlusion.
- Keep the current surface-local raymarch stable while building the orbit shell.
  The direct orbit shell remains a checkpoint before any cached sky product.

Acceptance target for the next batch: the `orbit-coverage` and `orbit-hull`
diagnostics should already read like coherent weather masses from high/orbit
views before cloud lighting, aerial perspective, or post-processing are judged.

## Orbit Weather Scale-Aware Checkpoint 2026-06-20

`projects/cloud` now has the first direct implementation of the Skybolt-style
coverage-first orbit rule without importing an asset-backed global cloud map.
The orbit shell still samples procedural planet-normal fields, but the broad
field, warp, texture breakup, and detail frequencies derive from
`clouds.weather_scale_km` and the configured planet circumference. Detail noise
is constrained toward edge and hull erosion, while the broad field owns the
visible weather-system placement.

This is a useful bridge for the standalone project because `Weather scale`
finally affects orbit weather massing instead of only the surface-local weather
projection. It is not the final planet solution:

- the coverage product is still shader-only, so it cannot yet be reused by
  cloud shadows, planet rendering, or ocean reflections;
- there is still no authored or satellite-style global cloud map;
- limb/max-projection artifacts in `orbit-coverage` and `orbit-hull` are
  diagnostic limitations of the direct shell pass, not solved cache output;
- the direct shell remains a checkpoint before a cached sky/cloud product or
  reusable renderer contract.

The test surface now includes headless orbit final, orbit coverage, orbit
detail, and orbit hull smoke/stat checks so broken orbit outputs are caught
alongside the existing surface/high captures.

## Orbit Detail Believability Target 2026-06-20

The scale-aware orbit checkpoint fixed one mechanical problem, but it
overcorrected toward smooth broad blobs. Real planet-scale cloud shots are
sparser and more structured: broad weather systems are regional, and visible
detail appears as fronts, bands, cells, streaks, curled edges, and broken cloud
texture inside those systems.

The next procedural pass should keep the broad weather gate, but make the orbit
model layered:

- broad systems: sparse regional masks driven by `clouds.coverage` and
  `clouds.weather_scale_km`;
- fronts/bands: mid-frequency ridged structure controlled by
  `clouds.weather_fronts`;
- cells: broken clustered patches controlled by `clouds.weather_cells`;
- streaks: wind-sheared detail controlled by `clouds.weather_streaks`;
- final hull: visibly shaped by `clouds.orbit_detail_strength`, not only
  lightly eroded.

This remains procedural v1. Do not introduce an asset-backed global cloud map in
this batch. The acceptance read is that `orbit-coverage` stays sparse,
`orbit-detail` is visibly rich, and `orbit-hull`/final no longer read as smooth
weather blobs.

## Orbit Weather Product Attempt 2026-06-20

The direct orbit-shell formula asked each raymarch sample to rediscover the same
planet-scale organization from inline noise. That made the final image either
too smooth and blob-like or too noisy without giving future consumers a reusable
weather product, so a generated 2048 x 1024 orbit weather map was tried.

That product is no longer the active direction. It reduced per-sample formula
work, but it did not carry enough planet-scale detail, and its map projection
made block/discontinuity artifacts visible from orbit. The generated texture,
metadata descriptor, descriptor binding, and shader were removed so the orbit
path has one source of truth again.

The next implementation should stay direct and procedural while behaving more
like an authored planet-space cloud product:

- broad coverage owns sparse regional weather placement;
- fronts, cells, and streaks add mid-scale organization inside those regions;
- detail erosion carves holes and texture without creating planet-wide overcast;
- the orbit shell evaluates direct sphere-space coverage/detail/hull
  diagnostics;
- the local surface weather texture stays separate so surface/high tuning is not
  forced to share the same projection.

Acceptance target: orbit captures should gain visible satellite-scale detail
without reintroducing square patch seams, longitude seams, or a full-planet
storm cap. Surface and high captures should remain stable while this orbit path
is proven.

## Direct Orbit Detail Checkpoint 2026-06-20

`projects/cloud` now keeps orbit weather as a direct sphere-space procedural
field. The generated 2D orbit-weather texture path was removed after the first
attempt failed the believability target: it avoided repeated per-sample formula
work, but it exposed map/projection discontinuities and still read too bare from
orbit.

The active shader path separates the layers that matter for orbit read:

- broad coverage gates sparse regional systems from `clouds.coverage` and
  `clouds.weather_scale_km`;
- fronts, cells, streaks, and micro wisps are evaluated directly from the
  planet normal, so they have no longitude seam or 2D texture boundary;
- detail is used as hull erosion, not as a second planet-wide coverage mask;
- the surface/local path still owns the 3D base/detail volume sampling and local
  weather texture.

The current capture checkpoint is
`outputs/cloud-procedural-orbit-review/`. It looks better than the failed texture
product because square patch seams are gone and orbit final has more small-scale
breakup. Remaining work is still visual rather than solved architecture:
high-oblique transition quality, limb/debug-ray artifacts, and physically
believable satellite-scale cloud organization are not finished.

## Local Weather Layering Checkpoint 2026-06-21

The surface/local path now borrows the orbit weather model's useful structure
without sharing its sphere-normal domain. Local volumetric clouds still sample a
planar world-space density field, but placement is no longer a single scalar
scatter gate. Broad local systems, dry/clear slots, front bands, cells, streaks,
and micro fragments are evaluated as separate procedural fields, then combined
into coverage, cloud type, edge detail, clear, and structure outputs.

This is deliberately not the removed generated 2D orbit-weather product. The
goal is richer local scatter with clear windows and high-frequency cloudlets,
while avoiding both old authored-map block seams and the later sparse-island
look. New diagnostics expose `local-clear`, `local-structure`, and
`local-edge-detail` alongside `local-scatter` so tuning can identify whether a
bad capture is a placement, clear-slot, or erosion problem.

## High-Oblique Far-Bridge Diagnosis 2026-06-22

`outputs/cloud-transition-review/` showed that the first local/far/orbit
transition split improved high-oblique framing only slightly. The far-shell
weight activated, but `high-oblique-far-shell-alpha` was only a faint grazing
streak and `high-oblique-local-with-shell-alpha` was nearly identical to
`high-oblique-local-alpha`. The final high-oblique image also changed almost
imperceptibly between far-shell off/default/strong captures.

Forced comparisons clarified the source of the failure:

- the orbit `surface-shell` is appropriate for full-disk orbit views, but its
  grazing/limb filtering removes almost all useful high-oblique horizon mass;
- the fallback orbit volume march contributes a soft horizon smear, not coherent
  broken cloud structure;
- per-branch haze then pushes distant cloud color toward sky/background, so even
  faint far-field cloud reads as haze rather than cloud.

The next implementation direction is therefore a dedicated high-oblique
far-volume bridge. Orbit `surface-shell` remains the default full-orbit path,
while the far bridge should use a cheaper version of the same local cloud field
over the far ray segment, then apply aerial perspective once after branch
composition.

The first implementation checkpoint is `outputs/cloud-far-bridge-review/`.
`high-oblique-far-shell-alpha` now shows a broad broken horizon band, and
far-shell off/default/strong captures are visibly distinguishable. The bridge is
still intentionally soft; further work should improve far-field detail and
lighting before changing the full-orbit shell again.

The follow-up diagnosis is that this first bridge still used the orbit weather
volume rather than the local cloud field. That makes high-oblique captures feel
like a transition between cloud types: foreground structure comes from the
surface/local procedural density, then the horizon assist comes from the
planet/orbit weather model. The next checkpoint should treat the far bridge as a
local-cloud LOD path: fewer steps, higher density LOD bias, same local weather
and 3D density source, and still no changes to the full-orbit shell.

The adaptive-local checkpoint is `outputs/cloud-local-lod-bridge-review/`. The
far bridge now reuses `cloud_sample_density` over only the distant ray segment,
with lower step count and higher LOD bias. `high-oblique-far-shell-alpha` now
shares the local cloud shapes instead of the orbit weather layout. The visible
final difference between far-shell off/default/strong remains modest because the
foreground local march still traverses most of the same long ray; the value of
this pass is domain consistency, not a dramatic final-color change. Further
transition work should decide whether long high-oblique rays need explicit local
march truncation, a cached hemisphere/shell product, or temporal reconstruction
before increasing bridge strength.

`outputs/cloud-inner-boundary-fade-review/` records the next local-volume
boundary fix. `high` and `high-oblique` exposed a horizontal cloud-floor line
from grazing rays accumulating through the inner cloud-shell tangent. The local
march now fades density near the inner-shell ray end, fades lower-layer samples
for elevated downward/grazing views, and damps rays near the inner tangent so
the shell floor does not saturate into a stripe. The `high` camera preset also
moved above the default cloud top; the old 12 km preset sat inside the 5-22 km
default cloud layer and was a poor high-view diagnostic.

`outputs/cloud-horizon-coverage-review/` showed that the boundary fade fixed
the hard floor line but exposed a separate far-coverage problem: in high and
high-oblique views the reduced local far bridge was active, yet its alpha stayed
too sparse and patchy to carry cloud continuity all the way to the horizon. The
follow-up keeps the local-volume far bridge for domain consistency, wires the
existing `clouds.horizon_layer` option into the shader, and adds a cheap
distance/grazing-gated horizon layer driven by the same local procedural weather
field. This is intentionally not the full orbit shell and not a separate orbit
weather type; it is a high-view LOD assist for long rays where reduced local
marching thins out before the horizon.

The same checkpoint exposed a surface-view version of the issue: the local
volume reaches the visible horizon only as sparse cloud bodies, with no cheap
long-ray continuation below the main cloud band. The horizon layer now has a
separate surface gate for low-altitude cameras: narrow lower-sky angles, long
cloud-shell ray segments, and the same local weather field. It should extend the
read of distant cloud cover without turning the lower half of the surface view
into a uniform fog or changing the elevated-camera tangent fix. The first
surface pass was diagnostic-visible but too subtle in final color, so the
surface gate now carries stronger branch weight, optical depth, and radiance
while leaving the elevated high/high-oblique horizon tuning unchanged.

## Orbit Shell Strategy 2026-06-20

The current orbit raymarch is useful as a diagnostic, but it is no longer the
target planet-scale representation. From orbit, cloud thickness is tiny relative
to planet radius, and most of the visual read comes from cloud-top coverage,
front/cell/streak detail, height/normal lighting, shadows, and atmosphere at the
limb. A shell/cloud-top renderer should therefore sit beside the current volume
path before it becomes the default.

Reference direction:

- satellite imagery from NASA/NOAA is the visual target: fronts, broken cells,
  cloud streets, swirls, vortices, and large clear ocean/land gaps rather than
  continent-shaped filled masks;
- Horizon/Nubis-style cloud shaping still applies: large weather fields gate
  where clouds may exist, while noise/detail fields erode and shape the visible
  cloud body;
- `godot-planet-fly-through-cloud-volume` is a useful local reference because
  it uses cubemap/sphere-space cloud coverage instead of a naive equirectangular
  product;
- the failed generated 2D orbit-weather product should not be revived in this
  batch. If cached products return later, use cubemap or octahedral storage with
  mip/filtering.

Implementation target:

- add a side-by-side `clouds.orbit_representation` switch so the current
  volume raymarch remains available for comparison;
- keep the orbit weather source procedural and time-varying. A texture path can
  return later as a cached cubemap/octahedral product, but it should be generated
  from the procedural field instead of becoming static authored weather;
- implement a deterministic single-hit cloud-top shell for orbit views, with no
  ray-start jitter;
- derive visible optical depth, height, normals, and a cheap shadow/occlusion
  scalar from direct sphere-space procedural fields;
- move weather by advecting sphere-space coordinates with wind plus low-amplitude
  curl/domain warp. The field should drift continuously in time and never pop by
  reseeding noise;
- estimate screen-space footprint explicitly from camera distance, FOV, and
  render resolution so high-frequency detail fades before it shimmers;
- hide shell flatness with grazing-angle alpha feather, cloud-top normals,
  limb/rim lighting, and atmosphere blending.

## Absorbed Atmosphere Baseline 2026-06-28

After the standalone cloud project was absorbed into `projects/atmosphere`, the
current review command is:

```sh
projects/atmosphere/capture_cloud_review.sh outputs/atmosphere-cloud-review-current
```

The script now captures full-quality 1920x1080 output by default and includes
`high-oblique-no-clouds` alongside `high-oblique-final`. That comparison matters:
the no-cloud high-oblique frame has a clean clear-sky/reference horizon, while
the final frame still shows the cloud layer becoming a hard, noisy far band.
This points the next fix back at cloud handoff/LOD, not at the shared
atmosphere background.

The current visual split is:

- surface and upward views are the strongest baseline for local volumetric cloud
  body shape;
- high-oblique final is acceptable as a checkpoint, but the far handoff is still
  visibly rough;
- orbit-shell oblique gives useful broad weather/shell diagnostics, but it is
  too flat and translucent to be the high-oblique target by itself.

Next transition work should preserve the no-cloud comparison, then decide
whether high-oblique needs local march truncation, a dedicated cached
hemisphere/shell product, or a more explicit far-cloud LOD bridge before cloud
shadows/reflections are treated as foundation-ready.

## Shadertoy Horizon Artifact Inventory 2026-06-28

The local reference folder is `/home/bryan/code/ref/ShaderToy`. The important
lesson from the inventory is that the current atmo artifacts are not unusual:
real-time cloud examples either spend many more samples near grazing rays, filter
distant density/detail aggressively, or rely on temporal reconstruction designed
for stochastic samples. Bayer ray-start jitter without temporal convergence
mostly turns bands into visible noise.

Highest-value references for the current atmo failure:

- `enscape_cube_*`: spherical Earth/cloud-shell setup with hash jitter, temporal
  accumulation, and neighborhood clamping. It is the closest reference to the
  Cubey atmo/ocean/planet camera problem.
- `starry_night_*`: blue-noise and golden-ratio frame offsets are used
  specifically to hide banding under temporal accumulation.
- `clouds_raymarching_*`: an expensive but clear slab marcher with reprojection,
  variance clipping, and enough samples to show what quality the sparse path is
  approximating.
- `clouds.glsl`: IQ-style fBM volume marching uses progressive step growth and
  lowers density/noise detail with distance instead of sampling the same detail
  field everywhere.
- `overcast_cloud.glsl`, `wather.glsl`, and `horizon_clouds.glsl`: useful
  horizon/slab comparisons, including explicit comments about noisy horizon
  behavior and grazing-ray step issues.
- `sun_sky_clouds.glsl` and `tiny_planet_clouds.glsl`: useful secondary checks
  for atmosphere/cloud distance caps and planet-scale shell composition.

The active Cubey path matches the known bad case: half quality traces a reduced
cloud product, uses static Bayer jitter by default, keeps temporal accumulation
off, and gives the far-horizon bridge only a small number of samples across very
long tangent rays. Full resolution hides some upsample damage, but the raw march
still undersamples the far field. The next fix should target half-res atmo
output: blue-noise or spatiotemporal jitter, confidence-aware temporal resolve,
distance/footprint density LOD, and a stronger metadata-aware composite resolve.
Quarter quality can remain a rough fallback until a cached hemisphere/impostor
path exists.

## Atmo Horizon Sampling Checkpoint 2026-06-28

This batch moved the active atmo cloud path from static Bayer half-res sampling
toward the reference-backed strategy above:

- added diagnostic views for jitter, horizon step length, and horizon filter LOD;
- made half-res atmosphere clouds the default quality target;
- added a generated blue-noise texture with frame-varying temporal sampling;
- tightened temporal neighborhood clamping and history validity;
- filtered local density detail by distance, step footprint, and grazing angle;
- replaced the half-res composite with a metadata-aware 5x5 bilateral resolve.

Validation passed with:

```sh
cmake --build --preset dev --target atmosphere planet
ctest --preset dev --output-on-failure
```

The full test suite reported 159/159 passing tests. The review pack is:

```text
outputs/atmosphere-cloud-horizon-noise-20260628/
```

That pack includes the standard cloud review frames plus targeted horizon
diagnostics and full/quarter high-oblique comparison frames. The high-oblique
transition is more legible and the debug views now expose the sampling regime,
but surface-horizon dark silhouettes/holes are still visible. That residual
failure now looks less like pure stochastic noise and more like a far-horizon
lighting/opacity/handoff problem. The next visual pass should target far-cloud
radiance, alpha, and the near/far handoff before adding more resolution tweaks.

## Integrated Horizon Handoff Checkpoint 2026-06-28

This follow-up implements the handoff direction from the Shadertoy inventory:
near/local cloud samples are truncated as rays become horizon-grazing, then a
deterministic integrated far-horizon layer reconstructs the distant cloud band.
The intent is to avoid throwing more stochastic samples at extremely long
tangent rays while keeping the same weather/local-detail field as the source.

Landed changes:

- the architecture note now defines the far cloud target as an integrated
  low-detail horizon layer, not another detailed local march;
- the local volume march fades density in the grazing/far handoff region;
- the old far bridge no longer runs a noisy secondary local march;
- atmosphere clouds now default to `auto` distance mode so the atmo project
  actually exercises the distance-aware cloud path;
- new diagnostics expose `horizon-handoff`, `local-truncation`,
  `integrated-horizon-alpha`, and `integrated-horizon-radiance`;
- the atmosphere cloud review script now captures horizon on/off comparisons
  plus the handoff diagnostics for surface and high-oblique views.

Validation passed with:

```sh
cmake --build --preset dev --target atmosphere cubey_project_atmosphere_tests
ctest --preset dev -R '^atmosphere_config_tests$' --output-on-failure
```

Review captures:

```text
outputs/atmosphere-cloud-horizon-handoff-20260628/
outputs/atmosphere-cloud-horizon-handoff-20260628-auto-fix/
```

The mechanical handoff now activates in surface and high-oblique views, and the
debug masks clearly show where local samples are being removed. The visible
final/no-horizon delta is intentionally subtle because the integrated layer is a
soft far cloud band, not a new cloud type. The full-size captures also show that
the hard black strip at the surface horizon is already present with `--no-clouds`;
that is an atmo/background or horizon-composition issue, not a cloud-march
artifact. Treat that as a separate atmo fix before judging further cloud
horizon tuning.

## Cloud Edge Artifact Diagnosis 2026-06-29

The latest review showed the artifact is not limited to the far field: overhead
cloud edges also show row/band structure. That changes the diagnosis from a
horizon-only handoff bug to a cloud-edge integration problem. Center sampling
and Bayer reveal stable under-sampling bands, while frame-varying blue noise
makes those bands move and therefore shimmer unless it is paired with a real
sparse temporal reconstruction path.

Reference comparison:

- `TerrainEngine-OpenGL` keeps the active cloud shape simpler: one coherent
  texture-backed density field, stable Bayer ray-start offset, smooth remaps,
  mipmapped volume textures, 64 direct steps, fog/background fade, and a small
  Gaussian post blur. It does not depend on animated blue-noise jitter.
- `flower`, `Meteoros`, and the stronger Shadertoy examples use stochastic
  samples only with reprojection, history, neighborhood/variance clamping, or
  phased 4x4 sparse updates.
- `Project-Marshmallow` is the most directly useful integration reference: it
  marches coarse while empty, backs up and enters a smaller-step mode on first
  cloud hit, then returns to coarse stepping after several misses.

That diagnosis led to an adaptive local-march experiment, but GUI review showed
the cost/quality tradeoff was not acceptable. Keep these conclusions, but do
not repeat that exact adaptive path as the default:

- keep Bayer as the production default until sparse temporal reconstruction is
  robust;
- avoid frame-varying jitter unless it is paired with real sparse
  reconstruction;
- filter or pre-integrate unresolved density edges before adding more visual
  detail;
- keep the far-horizon layer broad and low detail, with stable stratified sample
  phases rather than another detailed local march.

## TerrainEngine Resolve Recheck 2026-06-29

A closer crop review changed the reference read: `cloud_ref` is not an
artifact-free oracle. Its `cloud-alpha` and `raw-final` captures show the same
stippled cloud-edge sampling noise, but the final view hides it behind lower
contrast and flatter lighting. TerrainEngine's captured final image stands up
better because it keeps the raw model simple and then presents it through the
source post chain.

Relevant TerrainEngine source details:

- `CloudsModel.cpp` defaults `postProcess = true`;
- `VolumetricClouds::getCloudsTexture()` returns the post-processed cloud
  texture when that flag is set;
- `shaders/clouds_post.frag` applies a small 3x3 Gaussian blur to the cloud
  color product before scene composition.

The next production attempt should therefore target the final cloud resolve
rather than another density model. Keep `raw-final` as the unfiltered diagnostic
view, preserve the current metadata-aware resolve for comparison, and add a
TerrainEngine-style post resolve that filters the premultiplied cloud product
with enough alpha/distance guarding to avoid obvious horizon or terrain smears.

## Cloud Edge Stability Attempt and Rollback 2026-06-29

The adaptive edge-march attempt was reverted after GUI review. It reduced the
obvious row-like slicing in some overhead captures, but it did not remove edge
noise at half or full quality and degraded performance enough that half quality
could no longer sustain the 144 fps cap while full quality dropped below 20 fps.
Do not reintroduce that path as the default without a separate performance
budget and a clearly better edge result.

Current kept changes:

- atmosphere and shared cloud defaults use stable Bayer sampling with 0.65
  jitter strength;
- blue noise and temporal reconstruction remain explicit diagnostics/future
  sparse-reconstruction work;
- the artifact diagnosis remains: random jitter is not the fix, and a future
  solution needs either true sparse temporal reconstruction, a cached/cloud-top
  product, or a cheaper deterministic integration strategy.

Validation before the rollback passed with:

```sh
cmake --build --preset dev --target atmosphere planet cubey_project_atmosphere_tests
ctest --preset dev -R '^atmosphere_config_tests$' --output-on-failure
ctest --preset dev -R '^(render_apps_use_dynamic_rendering|atmosphere_headless_writes_png|atmosphere_headless_writes_png_stats)$' --output-on-failure
ctest --preset dev --output-on-failure
```

The full suite reported 159/159 passing tests before the adaptive shader revert.
After the rollback and timeout cleanup, focused validation passed with:

```sh
cmake --build --preset dev --target atmosphere planet cubey_project_atmosphere_tests
ctest --preset dev -R '^atmosphere_config_tests$' --output-on-failure
ctest --preset dev -R '^(render_apps_use_dynamic_rendering|atmosphere_headless_writes_png|atmosphere_headless_writes_png_stats)$' --output-on-failure
```

Review captures:

```text
outputs/atmosphere-cloud-edge-stability-20260629/
outputs/atmosphere-cloud-edge-stability-20260629-targeted/
```

The targeted captures are still useful for comparing Bayer jitter strength, but
they should not be treated as a solved quality checkpoint. Horizon and
high-oblique frames also expose a separate atmosphere/background boundary
problem in the no-cloud comparison. The next cloud-specific step should be a
real sparse temporal/reconstruction path or a more explicit cached far-cloud
product, not more random jitter or the reverted adaptive march.

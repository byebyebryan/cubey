# Cloud Rendering Direction

This document promotes the lessons from the retained `projects/cloud_ref`
reference and retired `projects/clouds_legacy` and `projects/cloud_ref_2`
prototypes into the direction for the production surface cloud layer hosted by
`projects/atmosphere`. Those projects are historical evidence; new cloud
feature work should not keep tuning throwaway ports or a separate standalone
production app.
The retirement rationale and recovery anchor are recorded in
[`docs/archive/retired-projects.md`](../archive/retired-projects.md).

## Decision

Use the shared `cubey::render::CloudLayerRuntime` through `projects/atmosphere`
instead of reviving `projects/clouds_legacy` or `projects/cloud_ref_2` as
standalone cloud applications or pulling either experiment toward visual
quality.

The accepted production baseline is now the surface-volume cloud path validated
through the `cloud_ref` parity work, with the shared lower-sky horizon handoff
enabled by default. It should be named and treated as `surface-volume`, not as a
temporary reference mode.

Cloud V1 is intentionally surface-only. Its job is credible clouds for ground,
ocean, and horizon-scale atmosphere-backed views. Aerial, high-altitude, orbit,
satellite-style weather, visible cloud-top caches, and surface-to-orbit
transitions are deferred to later versions. The bounded filtered environment
cache used by reflections is a surface integration product, not an aerial/orbit
cloud renderer.

The V1 production renderer should combine:

- the texture-backed cloud density model from the TerrainEngine-style
  `cloud_ref`;
- the integration contracts and scale pressure learned from
  `projects/clouds_legacy`;
- shared sky, celestial, atmosphere, and exposure inputs for sun/moon light,
  sky color, and background classification.

Later aerial/orbit work may borrow the cached sky/cloud product architecture
from `projects/cloud_ref_2`, but that cache architecture is not part of Cloud
V1.

Do not treat any one reference as the final renderer. `cloud_ref` is the best
shape reference; `projects/cloud_ref_2` is the cache architecture reference,
and `projects/clouds_legacy` is the warning about scale, horizon, UI, and
integration failure modes.

## Current Ownership

The production cloud foundation is the shared cloud layer, not a standalone
cloud app. The current stable path is:

- `cubey::render::CloudLayerRuntime`: owns generated noise/weather textures,
  the cloud product, optional temporal pass, metadata, and composition;
- `cubey::CloudEnvironmentRuntime`: owns the coherent double-buffered cloud
  cubemap lifecycle, refresh cadence, generation blending, fallback packaging,
  and the shared PBR texture-binding handoff;
- `cubey::CloudEnvironmentConfig`: owns the project-facing cloud settings,
  defaults, and weather presets; shared schema metadata binds typed startup
  options for consumers that expose those controls;
- `cubey::host::draw_cloud_environment_controls`: owns the shared V1 tuning UI
  for those settings;
- `projects/atmosphere`: primary tuning and inspection surface for shared
  clouds;
- `projects/ocean`: surface-view consumer that composites shared clouds over
  the atmosphere background while preserving every opaque scene pixel, samples
  projected cloud transmittance for direct light, renders a reflected-camera
  cloud product for local planar reflection, and uses a coherent filtered cloud
  environment as broad fallback;
- `projects/gltf_viewer`: general forward-PBR consumer that uses the engine
  renderer's opaque-foreground cloud composition and the filtered cloud
  environment above the procedural clear-sky probe;
- `projects/fluid/water_3d`: refractive-water consumer that composes clouds into
  its HDR scene background without modifying opaque scene pixels before the
  water surface pass, and reuses the same filtered environment through its
  existing PBR bindings;
- `projects/planet`: orbital-only globe with a project-local restrained cloud
  veil. It does not instantiate the shared Cloud V1 runtime and is not an
  aerial/orbit pressure consumer.

Consumers still decide which cloud products they need and package local camera
and atmosphere lighting into `CloudLayerFrameInfo`. Generated noise/weather
ownership also remains with the visible cloud layer because shadows, planar
views, and cached environments share those resources. The cached environment's
scheduling, invalidation, previous/current generation state, and PBR descriptor
contract are no longer project-local.

Scene-depth composition is an explicit render policy, not a cloud tuning option.
Atmosphere-only, cached-environment, and planar-reflection products disable scene
depth. glTF Viewer, ocean, and Water 3D preserve any opaque foreground pixel
exactly because their visible cloud product is a background layer. The retired
local-detail Planet prototype established a distance-aware composition mode for
cloud shells that can appear in front of a surface, but no active executable
currently owns that path. Active background consumers return untouched HDR
scene color before cloud resolve/post work whenever geometry fully occludes the
cloud.

Visible cloud ownership follows the same rule in every accepted surface
consumer: the project's `AtmosphereEnvironmentRuntime` owns one
`CloudEnvironmentRuntime`. Consumers may add local adapters such as ocean's
planar reflection, but they must not create a second generated-cloud or cached
environment runtime beside it. Static HDR/generated environment modes do not
create or compose procedural clouds.

`AtmosphereEnvironmentRuntime::advance` is the single cadence owner for both
the clear-sky reflection probe and its cloud environment. Ordinary solar-clock
changes only request a new coherent clear-sky generation; they do not invalidate
the cloud timeline. Cloud invalidation is reserved for structural cloud edits,
such as weather/noise resource changes, where retaining the old generation would
misrepresent the active field.

Treat the accepted production mode as `surface-volume`: full-resolution, auto
distance with the lower-sky horizon handoff, TerrainEngine-style density/noise,
Bayer sampling, single-frame sampling, terrain-post resolve, and no temporal
reconstruction by default. `local` plus `--no-cloud-horizon-layer` remains the
strict surface reference fallback. This mode is expected to work for surface and
ocean-style background use before aerial/orbit clouds are reconsidered in a
later version.

The runtime now keeps this surface path separate from the experimental branch:
`surface_cloud_march.comp` is the lean local-only reference shader, while
`cloud_march.comp` carries the production horizon handoff plus deferred
aerial/orbit diagnostics. Keep pure local-cloud fixes in the lean shader, and
keep handoff/aerial/orbit changes in the general shader where their diagnostics
are visible.

Keep the unfinished paths, but label them honestly:

- aerial/orbit transition, orbit shell, far shell, and high-altitude controls
  are deferred scale work, not Cloud V1;
- temporal reconstruction is diagnostic until it stops turning cloud noise into
  shimmer;
- `cloud_ref` is the local/surface visual reference and lighting test bed;
- `projects/cloud_ref_2` is an architecture reference, not a quality target;
- `projects/clouds_legacy` is evidence of failed and useful ideas from the first
  cloud implementation.

Compatibility aliases such as `reference-parity`, `cloud-ref-compatible`, and
`procedural` can keep parsing old configs, but docs, UI, and generated config
templates should present the canonical names above.

The shared `CloudEnvironmentUiConfig` defaults to the Cloud V1 surface control
surface. Atmosphere and ocean inherit that default, so density-model,
distance-mode, horizon-bridge, and orbit-shell controls stay hidden in normal
tuning. No active project opts into the deferred aerial/orbit controls; a future
dedicated batch must provide its own pressure surface rather than treating
hidden controls as implemented scope.

## Lessons From the Historical Prototype

`projects/clouds_legacy` proved the right long-term product pressure:

- surface, high-altitude, and orbit views all matter eventually, but only the
  surface regime belongs to Cloud V1;
- clouds need to consume the shared atmosphere/celestial lighting state;
- clouds should render into their own product before composition;
- consumers such as ocean and any future planet-surface product should receive
  cloud outputs rather than raymarching clouds in their material shaders;
- performance diagnostics, quality presets, temporal toggles, and debug views
  are mandatory, not polish.

It also showed what not to carry forward:

- an inline procedural value-noise density model is too coupled to tune;
- local grazing rays near the horizon produce persistent row/ring/streak
  artifacts when the far field is forced through the same march as nearby
  clouds;
- temporal reconstruction and final blurs can reduce artifacts but cannot fix a
  poor raw cloud signal;
- project-local sky/ground composition is fragile and repeatedly reproduced the
  atmosphere horizon-band class of bugs;
- controls become unmanageable when each project owns ad hoc UI instead of
  exposing configurable options through the shared config/UI path.

The useful legacy output contract should survive: cloud radiance,
transmittance, metadata, approximate shadow, and diagnostics. The local density
and horizon model should not.

## Lessons From Cloud Ref

`cloud_ref` is the current visual-shape reference. The important lesson is that
cloud form must be stable before lighting, cache, temporal filtering, or
composition can make it look good.

Keep these ideas:

- generated or uploaded 3D Perlin-Worley base noise;
- generated or uploaded 3D Worley/detail erosion noise;
- 2D weather maps for broad coverage/type control, applied as parameter bias
  over local density instead of as final opacity masks;
- cloud-type height gradients and coverage/density shaping;
- separate authored weather channels for broad coverage, cloud type, and edge
  softness/breakup, with continuous procedural local scatter as the default
  endpoint for the influence control;
- default production tuning should preserve the local scatter endpoint until
  authored weather shapes are proven better than the old random distribution;
- spherical cloud-shell intersections;
- Beer transmittance, powder/edge response, and a short light march;
- source-like debug views for authored weather, local scatter, coverage bias,
  base density, detail density, density, lighting, transmittance, shadow,
  distance, and step count.

Do not overfit these parts:

- TerrainEngine's grey standalone presentation;
- its missing Cubey atmosphere, ocean, planet, exposure, and UI contracts;
- its exact constants as final art direction;
- its direct per-view march as the only runtime path.

The production renderer should start by making coherent masses in final and raw
diagnostic views. If the silhouette reads as noisy fibers or sheets, the problem
is the density/weather model, not the cache.

The production weather model should stay authoring-oriented for now. It should
not pretend to simulate meteorology, but it also should not collapse cloud state
to one scalar coverage knob or directly cut out final density. The minimum
useful contract is coverage, type, edge softness, and erosion as separate
parameter biases over the local 3D density field before lighting and
composition.

### Cloud Ref Lighting Test Bed

`projects/cloud_ref` should now serve as the clean lighting/rendering test bed
for the local volumetric cloud path. It should keep the TerrainEngine-style
density, shell, noise, weather, and basic cone-light march as the stable raw
signal, but lighting is allowed to diverge from TerrainEngine where the source
demo is too presentation-specific or too crude for Cubey.

The corrected lighting direction is additive rather than a mix between ambient
fill and direct sun:

1. march density through the view ray and update view transmittance with
   Beer-Lambert extinction;
2. evaluate sun visibility with the existing short cone march;
3. evaluate directional response with dual or stacked Henyey-Greenstein phase
   functions;
4. apply a powder or cheap multi-scattering approximation as a modifier of the
   sun contribution, not as a switch that suppresses direct light;
5. evaluate ambient as a separate sky/top/ground term, initially from the
   standalone `cloud_ref` sky context and later from shared atmosphere/sky
   inputs;
6. integrate `direct + ambient` into linear cloud radiance, and output
   radiance plus view transmittance/alpha for final resolve and composition.

The earlier `cloud_ref` powder toggle was treated as experimental because it
multiplied the direct-light mix weight by a local density term. Current
`cloud_ref` lighting should keep powder/rim/backlit controls tied to view and
light optical depth diagnostics instead. That keeps the Beer-Powder role closer
to the references: a controlled local-scattering approximation layered on top of
Beer transmittance and phase, not a density switch that suppresses direct light.

Reference alignment for this lighting direction:

- `TerrainEngine-OpenGL`: still the source-faithful density/march baseline, but
  its final source mix is not the production lighting target;
- `godot-volumetric-cloud-demo-v2`: good compact model for sky-LUT-derived sun,
  ambient, and ground terms plus stacked phase and Beer-Powder-style lighting;
- `UnityVolumetricCloudsURP`: strongest product-level model for separating
  scattering/transmittance, ambient probe, powder intensity, multi-scattering,
  light steps, mean distance, and upscale/resolve metadata;
- `diharaw-volumetric-clouds`: compact cross-check for Beer-plus-powder energy,
  dual HG phase, cone-density lighting, and simple ambient/sun controls;
- `Meteoros` and `Project-Marshmallow`: useful explanatory references for the
  Horizon/Decima vocabulary: directional scattering, absorption/out-scattering,
  in-scattering, cone samples, and silver-lining behavior;
- ShaderToy cloud refs: useful visual/look-dev checks, especially derivative
  lighting, horizon-specific clouds, moonlit fill, and storm flash lighting, but
  not direct code donors because of mixed licenses and flat/AABB assumptions.

The TerrainEngine `resources/pic2.jpg` screenshot is a useful cloud-lighting
target because the cloud body picks up the orange low-sun color while the forms
remain bright, detailed, and non-stormy. Cubey should borrow that cloud-specific
behavior directly: scalar powder intensity, warm low-sun body tint, stronger
silver-lining edge response, cool underside ambient, and explicit final resolve
controls. Increasing coverage alone is the wrong response because it turns the
sky into a white cap without adding dawn color or useful lighting detail.
The accepted production default is a restrained sunny-warm version of that
reference: fuller surface coverage and stronger phase/powder/detail relief than
the old default, but lower shadow and lower twilight intensity than the explicit
TerrainEngine-inspired study preset.

The retained `cloud_ref` target remains the lighting/shape comparison surface:
inactive controls must stay absent or honestly labeled, and debug views should
continue to expose real ambient/direct/phase/source terms. New production work
belongs in the shared atmosphere-hosted path after it is validated there.

## Lessons From `projects/cloud_ref_2`

`projects/cloud_ref_2` was not a useful visual target. It deliberately reused
Cubey noise/weather data instead of faithfully importing the Godot source
textures, and the result remained messy even when the cache was bypassed.

It did prove several architecture points:

- an upper-hemisphere octahedral cached cloud product is a plausible way to
  amortize sky/cloud raymarching;
- tiled updates plus triple-buffered old/new blending give a clear performance
  path for sky-scale clouds;
- synthetic cache diagnostics are essential: direction continuity, horizon
  fold behavior, update tiles, and alpha/transmittance convention;
- a direct path beside the cached path is useful for validation, but not a
  runtime goal;
- cache artifacts should be separated from density artifacts before tuning.

Future aerial/orbit work should borrow the cache idea only when a direct density
model is visually credible. Otherwise, cache work just hides or
amplifies bad cloud shape.

## Production Shape

The production cloud renderer now lives as the shared cloud layer consumed by
`projects/atmosphere` and `projects/ocean` for surface-view sky composition.
Ocean consumes local shadow, planar reflection, and cached fallback products;
the glTF viewer validates the cached environment through general PBR materials.
Planet-scale Cloud V1 high/orbit integration remains a later-version track.
The active orbital Planet instead owns a deliberately restrained project-local
veil and does not consume this shared cloud runtime.

Initial scope:

- shared sky/celestial/atmosphere background and lighting input;
- texture-backed cloud density: base volume, detail/erosion volume, weather map;
- world-scale weather/type sampling where `clouds.weather_scale_km` means
  approximate broad feature size rather than texture period;
- explicit local density projection scale where `clouds.shape_domain_km` owns
  base/detail cloud texture frequency. Physical planet radius must not stretch
  or shrink local cloud breakup;
- raw weather/type diagnostics plus ray-marched visible density/type diagnostics
  so map artifacts and visible artifacts can be separated before changing
  production shaping;
- independent, opt-in vertical shear control so broad weather scale does not
  smear local cloud cells into slabs;
- surface, surface-up, and surface-horizon review views before any aerial/orbit
  polish;
- cloud product target containing linear radiance and transmittance;
- metadata target for mean distance, alpha/confidence, and any reconstruction
  inputs;
- tunable direct/ambient/phase lighting and final resolve controls so final
  image polish can be isolated from raw march quality;
- deterministic static sampling controls for interleaved-gradient, Bayer,
  blue-noise, and center-of-step ray starts, plus a metadata-aware final resolve
  that uses opacity, mean distance, confidence, and a cloud-edge mask while
  leaving `raw-final` unfiltered;
- stable Bayer ray-start jitter is the default production sampling path until
  sparse temporal reconstruction is stronger. Blue-noise remains available for
  diagnostics and future spatiotemporal resolve work, but frame-varying jitter by
  itself turns unresolved cloud-edge bands into shimmer;
- the shared cloud layer retains a diagnostic compute temporal resolve: the ray
  march writes current product/metadata, a ping-pong history pass reprojects by
  mean cloud distance, clamps against the current neighborhood, and resets on
  incompatible cloud parameter changes;
- the default composite background is atmosphere-only. The earlier water proxy
  remains as a historical standalone-capture lesson, but it should not be part
  of the baseline cloud read;
- local surface weather should use the same layered idea in a planar world-space
  domain: broad systems gate placement, dry slots preserve gaps, and fronts,
  cells, streaks, and micro fragments drive scatter and erosion;
- local volume detail should use deterministic footprint filtering from camera
  distance, step length, pixel size, grazing angle, shape domain, and generated
  noise texture size. `clouds.footprint_filter_strength` controls that filter;
- quality presets tied to render scale, view steps, light steps, and cache
  cadence;
- diagnostics for every major field;
- shared cloud schema metadata plus existing ImGui helper controls from the
  start, composed into each consuming executable's typed facade.

The absorption pass keeps the shared `cubey::render::CloudLayer*` contract,
common shader assets, shared generated-resource helpers, atmosphere backdrop
composition, cloud config/UI controls, and cloud diagnostics in the atmosphere
project. Treat this as the Cloud V1 production pressure surface, not a finished
multi-scale cloud product.

The layer must keep planet handoff constraints visible: use meters, carry planet
radius/cloud-shell metadata explicitly, keep camera GPU state camera-relative,
and define weather coordinates so they can later map onto a planet frame, local
tangent frame, or stable global weather address.

The TerrainEngine/cloud_ref diagnostic exposed a specific scale trap: the
reference density projection originally divided local `position.xz` by the cloud
inner radius. That made the same density field look detailed on the 600 km
reference sphere but smooth and capped on an Earth-radius atmosphere. Production
clouds therefore split the controls:

- physical `planet_radius_m`: shell intersection, horizon curvature, altitude,
  and orbit/planet geometry;
- `weather_scale_km`: macro weather placement, broad coverage, and type fields;
- `shape_domain_km`: local base/detail density texture projection;
- `footprint_filter_strength`: deterministic mip/filter response for distant or
  grazing cloud detail;
- `edge_softness`: footprint-aware widening of procedural density thresholds so
  under-resolved cloud boundaries do not collapse into binary Bayer dots;
- `edge_detail_fade`: reduction of high-frequency erosion only where the density
  edge is under-resolved;
- `edge_resolve_strength`: final-composite edge resolve weight. It is applied
  through the `edge-mask` debug diagnostic, not as a whole-image blur.

The old `cloud-ref-compatible` name should stay as a config alias for the
accepted surface path, but the production-facing name is `surface-volume`.
That path should match the reference through `shape_domain_km`, not by
pretending the atmosphere has a 600 km planet radius. The previous procedural
surface/aerial path is useful only as explicit `experimental-aerial-orbit`
scaffolding for a later-version aerial bridge.

Deferred beyond Cloud V1:

- aerial, high-altitude, orbit, and surface-to-orbit transitions;
- cloud-top shell rendering and full-disk/satellite-style weather;
- finished planet-scale orbit weather art direction;
- removal of remaining orbit-shell projection/alias artifacts in shell-alpha
  output and ray-sampled coverage/detail diagnostics;
- high-oblique transition polish, especially local-volume foreground plus
  far-shell background continuity;
- orbit motion/shimmer review against the satellite capture pack;
- a stronger planet-scale weather model than fixed experimental synoptic
  anchors, dry slots, and procedural breakup;
- terrain and planet consumption of projected cloud shadows;
- shared atmosphere/PBR lifecycle and descriptor ownership for the cloud
  environment probe already proven by ocean;
- full cached octahedral sky blending;
- temporal reconstruction beyond basic diagnostic toggles;
- blue-noise/spatiotemporal sampling until a useful temporal path exists;
- cirrus, storm, and multi-layer weather authoring beyond the first useful
  cumulus/broken-cloud model.

## Distance Regimes

Cloud V1 uses one production regime:

- `auto`: bounded surface-volume march with real parallax and thickness for
  surface, surface-up, and horizon-scale review views, plus a low-detail
  lower-sky handoff so clouds do not end abruptly near the horizon.

The local-only fallback remains useful for A/B review:

- `local`: bounded surface-volume march without the horizon handoff.

The other regimes remain useful design notes and diagnostics, but they are not
active Cloud V1 targets:

- high/aerial: local foreground plus a far bridge or cached hemisphere product;
- orbit: filtered cloud-top shell or cached product judged against satellite
  imagery;
- high/cirrus: cheap layer or shell until real volumetric need appears.

When those tracks resume, the transition between near volume and far
cached/cloud-shell output should be explicit and debug-visible. It should not be
hidden in final color grading. The existing `distance-regime`,
`transition-weights`, `far-shell-*`, and `orbit-*` diagnostics remain as
deferred scaffolding for that work.

## Renderer Contract

Clouds are a weather layer above clear-sky atmosphere. Cloud V1 consumes the
shared sky/celestial/atmosphere state and emits:

- cloud product RGB is linear cloud radiance;
- cloud product alpha is view transmittance for background composition, not
  cloud opacity;
- metadata/debug outputs carry mean distance, cloud opacity, confidence, and
  density for reconstruction and future depth-aware composition;
- an optional texel-snapped, receiver-plane cloud transmittance product for
  low-frequency direct-light modulation;
- a visible-view radiance/transmittance product for final background
  composition and diagnostics;
- a reflected-camera radiance/transmittance product for local planar receivers;
- a coherent, roughness-prefiltered cloud environment cache for broad fallback;
- debug views for weather, base/detail density, lighting, shadow, distance,
  steps, and composition, plus deferred cache/local/orbit regime diagnostics.

Deferred outputs include planet-scale shadow products and aerial/orbit weather
products. Shared PBR ownership of clouded environment bindings has landed:
glTF Viewer and Water 3D consume the coherent environment handoff, while ocean
also samples the receiver shadow and reflected-camera products without owning
cloud raymarch code. Future terrain or planet-surface consumers should follow
the same ownership rule when their scale contracts are ready.

V1 uses `RenderGraphBuilder` to make cloud product, environment, shadow,
reflection, and composite passes explicit. Runtime owners retain descriptors,
textures, and material instances, while graph execution owns synchronization at
declared boundaries. Downstream work should use the atmosphere cloud capture
helper and shared `clouds.*` config options to check visual and contract
assumptions without adopting cloud internals.

The final composite tone and color post pass is shared through
`shaders/cubey/cloud/cloud_composite_post.glsl`. The active composite shaders
are the external-background variants: atmosphere uses the background-only path;
glTF Viewer, ocean, and Water 3D use opaque-foreground scene depth. The physical
distance-aware mode is retained for future scale pressure but has no active
Planet consumer. The retained `cloud_ref` project keeps its local shaders;
superseded standalone cloud viewers are available only through Git history.

## Current Milestone

The standalone production pressure project has served its purpose and has been
absorbed into atmosphere. Cloud V1 surface quality is good enough to act as the
current shared foundation checkpoint for atmosphere and ocean surface views.
Further surface polish is useful, but it is no longer a blocker for resuming
focused ocean, terrain, asset, or renderer feature work.

The active Cloud V1 milestone is:

1. Keep atmosphere final/no-cloud/debug captures visually comparable through
   `projects/atmosphere/capture_cloud_review.sh`.
2. Make full and half-resolution Bayer captures stable enough to diagnose raw
   cloud integration without hiding bands behind stochastic shimmer.
3. Improve surface horizon blending without regressing the credible overhead
   surface cloud look.
4. Keep sampling and resolve controls isolated so capture bundles can compare
   blue-noise/temporal, Bayer, interleaved, no-jitter, raw-final, and edge-mask
   output.
5. Keep the accepted ocean shadow, planar reflection, and cached fallback
   bounded and measurable while preserving the atmosphere tuning surface.
6. Keep the shared cached environment coherent in forward PBR while visible
   cloud composition remains an explicit consumer choice.

Acceptance for this milestone:

- raw density has coherent cloud masses, not noisy curtains;
- final view still reads without relying on temporal smear or final blur;
- surface and surface-horizon captures do not show the legacy horizontal
  streaking as the dominant artifact;
- half-resolution final captures do not rely on full-res supersampling to hide
  horizon/far-field noise;
- atmosphere exposes enough controls to isolate density, detail erosion,
  weather map, local shape domain, footprint filtering, edge softening,
  sampling, lighting, composition, and metadata output;
- the implementation does not duplicate project-local atmosphere horizon logic.

The filtered surface-cloud environment is now available to general PBR through
`CloudEnvironmentRuntime` and per-frame `ForwardPbrRenderer3D` environment
updates. This does not change the later milestone for aerial/orbit cloud shape,
weather, visibility, or transitions.

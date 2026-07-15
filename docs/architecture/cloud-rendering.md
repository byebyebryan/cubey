# Cloud Rendering Direction

This document promotes the lessons from `projects/clouds_legacy`,
`projects/cloud_ref`, and `projects/cloud_ref_2` into the direction for the
production surface cloud layer hosted by `projects/atmosphere`. The reference
projects should remain available as guardrails, but new cloud feature work
should not keep tuning throwaway ports or a separate standalone production app.

## Decision

Use the shared `cubey::render::CloudLayerRuntime` through `projects/atmosphere`
instead of continuing `clouds_legacy`, reviving a standalone `projects/cloud`,
or pulling `cloud_ref_2` toward visual quality.

The accepted production baseline is now the surface-volume cloud path validated
through the `cloud_ref` parity work, with the shared lower-sky horizon handoff
enabled by default. It should be named and treated as `surface-volume`, not as a
temporary reference mode.

Cloud V1 is intentionally surface-only. Its job is credible clouds for ground,
ocean, and horizon-scale atmosphere-backed views. Aerial, high-altitude, orbit,
satellite-style weather, cloud-top shells, cached sky-cloud products, and
surface-to-orbit transitions are deferred to later versions. They can stay in
the tree as explicit experiments and references, but they are not V1 acceptance
criteria and should not drive surface cloud defaults.

The V1 production renderer should combine:

- the texture-backed cloud density model from the TerrainEngine-style
  `cloud_ref`;
- the integration contracts and scale pressure learned from `clouds_legacy`;
- shared sky, celestial, atmosphere, and exposure inputs for sun/moon light,
  sky color, and background classification.

Later aerial/orbit work may borrow the cached sky/cloud product architecture
from the Godot-v2-style `cloud_ref_2`, but that cache architecture is not part
of Cloud V1.

Do not treat any one reference as the final renderer. `cloud_ref` is the best
shape reference. `cloud_ref_2` is the best cache architecture reference.
`clouds_legacy` is the best warning about scale, horizon, UI, and integration
failure modes.

## Current Ownership

The production cloud foundation is the shared cloud layer, not a standalone
cloud app. The current stable path is:

- `cubey::render::CloudLayerRuntime`: owns generated noise/weather textures,
  the cloud product, optional temporal pass, metadata, and composition;
- `cubey::CloudEnvironmentConfig`: owns the project-facing cloud settings,
  defaults, weather presets, and run-config mapping;
- `cubey::host::draw_cloud_environment_controls`: owns the shared V1 tuning UI
  for those settings;
- `projects/atmosphere`: primary tuning and inspection surface for shared
  clouds;
- `projects/ocean`: surface-view consumer that composites shared clouds over
  the atmosphere background, samples projected cloud transmittance for direct
  light, renders a reflected-camera cloud product for local planar reflection,
  and uses a coherent filtered cloud environment as broad fallback;
- `projects/planet`: deferred aerial/orbit pressure surface; not a Cloud V1
  consumer and not the source of surface-cloud defaults yet.

The remaining ownership gap is a `CloudEnvironmentRuntime`-level wrapper above
`CloudLayerRuntime`. Today each consumer still decides when to create generated
cloud resources, when config changes require weather/noise refresh, how to
package frame inputs from atmosphere/celestial lighting, and which optional
shadow/reflection products it needs. That is acceptable for Cloud V1
integration, but it should be addressed before adding more cloud consumers or
reviving aerial/orbit work.

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
- `cloud_ref_2` is a cached-sky architecture reference, not a quality target;
- `clouds_legacy` is frozen evidence of failed and useful ideas from the first
  planet-aware cloud prototype.

Compatibility aliases such as `reference-parity`, `cloud-ref-compatible`, and
`procedural` can keep parsing old configs, but docs, UI, and generated config
templates should present the canonical names above.

The shared `CloudEnvironmentUiConfig` defaults to the Cloud V1 surface control
surface. Atmosphere and ocean inherit that default, so density-model,
distance-mode, horizon-bridge, and orbit-shell controls stay hidden in normal
tuning. Planet explicitly opts into those deferred controls as a pressure path;
that makes later aerial/orbit work visible without turning it into the default
foundation contract.

## Lessons From Legacy

`clouds_legacy` proved the right long-term product pressure:

- surface, high-altitude, and orbit views all matter eventually, but only the
  surface regime belongs to Cloud V1;
- clouds need to consume the shared atmosphere/celestial lighting state;
- clouds should render into their own product before composition;
- consumers such as ocean and planet should receive cloud outputs rather than
  raymarching clouds in their material shaders;
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

The first implementation pass should make `cloud_ref` lighting honest before
adding more features: remove or relabel inactive controls, make debug views show
real ambient/direct/phase/source terms, replace the ambient/direct mix with an
additive source equation, then reintroduce powder as a scalar intensity with
off/current/new comparisons in the capture pack.

## Lessons From Cloud Ref 2

`cloud_ref_2` is not a useful visual target yet. It deliberately reused Cubey
noise/weather data instead of faithfully importing the Godot source textures,
and the result still reads messy even when the cache is bypassed.

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

The production renderer should borrow the cache idea only after the direct
density model is visually credible. Otherwise, cache work just hides or
amplifies bad cloud shape.

## Production Shape

The production cloud renderer now lives as the shared cloud layer consumed by
`projects/atmosphere` and by `projects/ocean` for surface-view sky composition.
It should continue to be tested and tuned in atmosphere before ocean water
materials or PBR viewers consume cloud products directly. Planet high/orbit
views are a later-version track, not the current production target.

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
- shared `RunConfig` descriptors plus existing ImGui helper controls from the
  start.

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

Deferred outputs include planet-scale shadow products and shared PBR ownership
of clouded environment bindings. Ocean proves the receiver contracts without
owning cloud raymarch code: it samples the shared shadow product, uses the
reflected-camera product for local detail, and falls back to a coherent cached
environment.
Planet, terrain, and glTF/PBR consumers should follow the same ownership rule
when their scale and environment-product contracts are ready.

V1 should use `RenderGraphBuilder` to make the cloud product and composite
passes explicit. Descriptor sets, textures, material instances, and synchronization
policy remain owned by the atmosphere integration until at least two consumers
need direct cloud products. Downstream branches should use the atmosphere cloud
capture helper and shared `clouds.*` config options to check visual and contract
assumptions without adopting cloud internals.

The final composite tone and color post pass is shared through
`shaders/cubey/cloud/cloud_composite_post.glsl`. The active composite shaders
are the external-background variants used by atmosphere, ocean, and planet;
legacy standalone/reference cloud projects keep their own local shaders instead
of depending on a shared standalone composite entry point.

## Current Milestone

The standalone production pressure project has served its purpose and has been
absorbed into atmosphere. Cloud V1 surface quality is good enough to act as the
current shared foundation checkpoint for atmosphere and ocean surface views.
Further surface polish is useful, but it is no longer a blocker for resuming
focused ocean, terrain, planet, or renderer feature work.

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

Only after that should the production layer resume aerial/orbit work or promote
the cached cloud environment into general PBR consumption beyond the bounded
ocean surface contract.

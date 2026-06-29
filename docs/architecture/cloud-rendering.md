# Cloud Rendering Direction

This document promotes the lessons from `projects/clouds_legacy`,
`projects/cloud_ref`, and `projects/cloud_ref_2` into the direction for the
production cloud layer hosted by `projects/atmosphere`. The reference projects
should remain available as guardrails, but new cloud feature work should not
keep tuning throwaway ports or a separate standalone production app.

## Decision

Use the shared `cubey::render::CloudLayerRuntime` through `projects/atmosphere`
instead of continuing `clouds_legacy`, reviving a standalone `projects/cloud`,
or pulling `cloud_ref_2` toward visual quality.

The production renderer should combine:

- the texture-backed cloud density model from the TerrainEngine-style
  `cloud_ref`;
- the cached sky/cloud product architecture from the Godot-v2-style
  `cloud_ref_2`;
- the integration contracts and scale pressure learned from `clouds_legacy`;
- shared sky, celestial, atmosphere, and exposure inputs for sun/moon light,
  sky color, and background classification.

Do not treat any one reference as the final renderer. `cloud_ref` is the best
shape reference. `cloud_ref_2` is the best cache architecture reference.
`clouds_legacy` is the best warning about scale, horizon, UI, and integration
failure modes.

## Lessons From Legacy

`clouds_legacy` proved the right product pressure:

- surface, high-altitude, and orbit views all matter;
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
`projects/atmosphere`. It should continue to be tested and tuned there before
ocean, planet, or PBR viewers consume cloud products directly.

Initial scope:

- shared sky/celestial/atmosphere background and lighting input;
- texture-backed cloud density: base volume, detail/erosion volume, weather map;
- world-scale weather/type sampling where `clouds.weather_scale_km` means
  approximate broad feature size rather than texture period;
- raw weather/type diagnostics plus ray-marched visible density/type diagnostics
  so map artifacts and visible artifacts can be separated before changing
  production shaping;
- independent, opt-in vertical shear control so broad weather scale does not
  smear local cloud cells into slabs;
- one clear surface camera and one high camera before orbit polish;
- cloud product target containing linear radiance and transmittance;
- metadata target for mean distance, alpha/confidence, and any reconstruction
  inputs;
- tunable direct/ambient/phase lighting and final resolve controls so final
  image polish can be isolated from raw march quality;
- deterministic static sampling controls for interleaved-gradient, Bayer,
  blue-noise, and center-of-step ray starts, plus a metadata-aware final resolve
  that uses opacity, mean distance, and confidence while leaving `raw-final`
  unfiltered;
- stable Bayer ray-start jitter is the default production sampling path until
  sparse temporal reconstruction is stronger. Blue-noise remains available for
  diagnostics and future spatiotemporal resolve work, but frame-varying jitter by
  itself turns unresolved cloud-edge bands into shimmer;
- the shared cloud layer has a compute temporal resolve for the final view: the ray
  march writes current product/metadata, a ping-pong history pass reprojects by
  mean cloud distance, clamps against the current neighborhood, and resets on
  incompatible cloud parameter changes;
- the default composite background is atmosphere-only. The earlier water proxy
  remains as a historical standalone-capture lesson, but it should not be part
  of the baseline cloud read;
- distance-regime controls are now explicit: `clouds.distance_mode` can force
  local or orbit-shell behavior, while `auto` blends high and orbit views toward
  a broad low-detail shell before the full cached sky product exists;
- the next distance-regime target is high-oblique composition, not another
  renderer reboot: local volume should own nearby parallax/thickness, while a
  far shell contributes behind it so clouds keep continuity toward the horizon;
- orbit rendering is split from the surface march. The volume raymarch remains
  available as `clouds.orbit_representation = volume`, while the default
  `surface-shell` path tests a filtered cloud-top shell: regional dry slots and
  storm tracks own planet-scale spacing, while fronts, cells, streaks, height,
  normals, and limb treatment own the visible cloud read;
- orbit weather must remain procedural and time-continuous. Static textures are
  acceptable later as generated caches or diagnostics, but not as the source of
  truth; wind and slow domain warp should move broad systems without reseeding
  the noise field;
- orbit visual direction should be judged against satellite/full-disk Earth
  imagery, not against the current volume raymarch. The useful cues are broken
  regional weather systems, spiral/frontal bands, cellular breakup inside cloud
  masses, and large clear ocean windows. Fuller coverage should fill totally
  empty regions through the same weather fields rather than through a smooth
  planet-wide cap; `clouds.orbit_fill` owns that bias so the coverage target
  can be tuned without changing shader constants. The volume path is an
  implementation comparison only and is not an art target;
- local surface weather should use the same layered idea in a planar world-space
  domain: broad systems gate placement, dry slots preserve gaps, and fronts,
  cells, streaks, and micro fragments drive scatter and erosion;
- orbit shell detail should be filtered by pixel footprint and grazing angle so
  disk detail survives while limb/edge shimmer does not define the image;
- the cloud-top shell should composite from column optical depth, not an
  arbitrary alpha curve, so orbit opacity can be tuned through density and
  extinction controls that map to a plausible cloud mass;
- quality presets tied to render scale, view steps, light steps, and cache
  cadence;
- diagnostics for every major field;
- shared `RunConfig` descriptors plus existing ImGui helper controls from the
  start.

The absorption pass keeps the shared `cubey::render::CloudLayer*` contract,
common shader assets, shared generated-resource helpers, atmosphere backdrop
composition, cloud config/UI controls, and cloud diagnostics in the atmosphere
project. Treat this as the production pressure surface, not a finished
multi-consumer cloud product.

The layer must keep planet handoff constraints visible: use meters, carry planet
radius/cloud-shell metadata explicitly, keep camera GPU state camera-relative,
and define weather coordinates so they can later map onto a planet frame, local
tangent frame, or stable global weather address.

Deferred until the shape is credible:

- ocean reflection integration;
- finished planet-scale orbit weather art direction;
- removal of remaining orbit-shell projection/alias artifacts in shell-alpha
  output and ray-sampled coverage/detail diagnostics;
- high-oblique transition polish, especially local-volume foreground plus
  far-shell background continuity;
- orbit motion/shimmer review against the satellite capture pack;
- a stronger planet-scale weather model than fixed experimental synoptic
  anchors, dry slots, and procedural breakup;
- production cloud shadow consumption by ocean/terrain from a real projected
  cloud shadow product;
- full cached octahedral sky blending;
- temporal reconstruction beyond basic diagnostic toggles;
- blue-noise/spatiotemporal sampling until a useful temporal path exists;
- cirrus, storm, and multi-layer weather authoring beyond the first useful
  cumulus/broken-cloud model.

## Distance Regimes

One march should not be forced to solve every distance.

Use separate regimes:

- near/overhead: bounded volumetric march with real parallax and thickness;
- mid/far sky: cached hemisphere or shell product with low-frequency weather
  massing and controlled horizon fade;
- orbit: cloud-top shell view with filtered sphere-space detail. At planet
  scale, cloud thickness is mostly a lighting/edge cue; visible alpha, height,
  normals, shadows, and atmosphere/limb blending should carry the image rather
  than a sparse volume march;
- high/cirrus: cheap layer or shell until real volumetric need appears.

The transition between near volume and far cached/cloud-shell output should be
an explicit feature with debug views. It should not be hidden in final color
grading.

The current production cloud layer implements the `auto` transition as three
separate contributors:

- `local`: the normal surface volume march, responsible for foreground thickness
  and parallax;
- `far shell`: an integrated low-detail horizon layer from the same local weather
  field, used after local volumetric detail fades out of grazing long rays;
- `full orbit`: the orbit shell as the replacement path for true orbit/high
  altitude views.

Composition is intentionally staged instead of a single lerp:

1. Compute `full_orbit_blend` from camera mode and altitude.
2. Compute raw far-shell assist from ray length, camera altitude gate, and
   `clouds.far_shell_strength`.
3. Attenuate far-shell assist by the remaining local branch:
   `effective_far_shell = raw_far_shell * (1 - full_orbit_blend)`.
4. Front-to-back compose `local + effective_far_shell`, then mix that branch
   toward the full orbit result using `full_orbit_blend`.
5. Apply cloud aerial perspective once to the composed result, not separately
   inside local and far source branches.

This prevents the orbit representation from contributing once as far background
and again as the full replacement during the same handoff. The far bridge must
not reuse the orbit cloud-top shell directly: captures showed that the orbit
shell is correctly limb/grazing-filtered for full-disk views, but it becomes a
faint haze source rather than readable high-oblique cloud mass. It should also
not use the orbit weather volume as its primary source, because that swaps cloud
domains during the surface-to-orbit transition and reads as a different cloud
type. The intended bridge is a deterministic integrated layer over the distant
horizon segment: it should sample broad local/weather mass, suppress
high-frequency erosion by distance and grazing angle, lift far-cloud lighting,
and blend into sky haze. It should not use stochastic long-ray starts as its
primary anti-banding tool. Full orbit remains a separate cloud-top shell problem.

The diagnostic contract is: `distance-regime` shows full orbit, effective
high-view bridge, and residual local regime; `transition-weights` shows
local-branch availability, final bridge contribution, and full orbit takeover.
`local-alpha`, `far-shell-alpha`, `local-with-shell-alpha`, and `orbit-alpha`
isolate the visible alpha at each stage. The `far-shell` debug name is a
historical compatibility label for the high-view far bridge.
`projects/atmosphere/capture_cloud_review.sh` includes these views for
atmosphere-hosted surface, high-altitude, and orbit-shell review.

## Renderer Contract

Clouds are a weather layer above clear-sky atmosphere. They should consume the
shared sky/celestial/atmosphere state and emit reusable outputs:

- cloud product RGB is linear cloud radiance;
- cloud product alpha is view transmittance for background composition, not
  cloud opacity;
- metadata/debug outputs carry mean distance, cloud opacity, confidence, and
  density for reconstruction and future depth-aware composition;
- low-frequency cloud shadow factor for terrain/ocean;
- optional sky/reflection or environment contribution for water/PBR consumers;
- debug views for weather, base/detail density, lighting, shadow, cache,
  distance, local/orbit regime, steps, and composition.

Ocean, planet, and glTF/PBR viewers should not own cloud raymarch code. They
should sample cloud outputs or composed sky/environment products.

V1 should use `RenderGraphBuilder` to make the cloud product and composite
passes explicit. Descriptor sets, textures, material instances, and synchronization
policy remain owned by the atmosphere integration until at least two consumers
need direct cloud products. Downstream branches should use the atmosphere cloud
capture helper and shared `clouds.*` config options to check visual and contract
assumptions without adopting cloud internals.

## Current Milestone

The standalone production pressure project has served its purpose and has been
absorbed into atmosphere. The next milestone should be small and hard to fake:

1. Keep atmosphere final/no-cloud/debug captures visually comparable through
   `projects/atmosphere/capture_cloud_review.sh`.
2. Make full and half-resolution Bayer captures stable enough to diagnose raw
   cloud integration without hiding bands behind stochastic shimmer.
3. Improve high-altitude and horizon continuity without regressing the credible
   surface cloud look.
4. Keep sampling and resolve controls isolated so capture bundles can compare
   blue-noise/temporal, Bayer, interleaved, and no-jitter output.
5. Expose cloud outputs only when a second consumer has a concrete contract for
   radiance/transmittance, metadata, shadow, or reflection data.

Acceptance for this milestone:

- raw density has coherent cloud masses, not noisy curtains;
- final view still reads without relying on temporal smear or final blur;
- surface/high captures do not show the legacy horizontal streaking as the
  dominant artifact;
- half-resolution final captures do not rely on full-res supersampling to hide
  horizon/far-field noise;
- atmosphere exposes enough controls to isolate density, detail erosion,
  weather map, sampling, lighting, composition, and metadata output;
- the implementation does not duplicate project-local atmosphere horizon logic.

Only after that should the production layer add the cached hemisphere path from
`cloud_ref_2` or promote direct cloud-product consumption into ocean/planet.

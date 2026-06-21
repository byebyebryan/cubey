# Cloud Rendering Direction

This document promotes the lessons from `projects/clouds_legacy`,
`projects/cloud_ref`, and `projects/cloud_ref_2` into the direction for the
production `projects/cloud` renderer. The reference projects should remain
available as guardrails, but new cloud feature work should not keep tuning
throwaway ports.

## Decision

Start a new production cloud project instead of continuing `clouds_legacy` or
pulling `cloud_ref_2` toward visual quality.

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
  softness/breakup, plus a local scatter channel so the influence control can
  fade back to the old noise-driven scatter;
- default production tuning should preserve the local scatter endpoint until
  authored weather shapes are proven better than the old random distribution;
- spherical cloud-shell intersections;
- Beer transmittance, powder/edge response, and a short light march;
- source-like debug views for weather, base density, detail density, density,
  weather bias, lighting, transmittance, shadow, distance, and step count.

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

The production cloud renderer should be a standalone `projects/cloud` project
first. It should not start inside ocean or planet.

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
- deterministic static sampling controls for interleaved-gradient, Bayer, and
  center-of-step ray starts, plus a metadata-aware final resolve that uses
  opacity, mean distance, and confidence while leaving `raw-final` unfiltered;
- Bayer ray-start jitter should remain the default static anti-banding path
  until temporal reprojection or blue-noise sampling is available;
- active cloud now has a compute temporal resolve for the final view: the ray
  march writes current product/metadata, a ping-pong history pass reprojects by
  mean cloud distance, clamps against the current neighborhood, and resets on
  incompatible cloud parameter changes;
- the default standalone background is atmosphere-only. The earlier water proxy
  remains available as `clouds.background_mode = water-context` for ocean
  inspection captures, but it should not be part of the baseline cloud read;
- distance-regime controls are now explicit: `clouds.distance_mode` can force
  local or orbit-shell behavior, while `auto` blends high and orbit views toward
  a broad low-detail shell before the full cached sky product exists;
- orbit rendering is split from the surface march. The volume raymarch remains
  available as `clouds.orbit_representation = volume`, while the experimental
  `surface-shell` path tests a filtered cloud-top shell: broad weather acts as a
  soft envelope, while fronts, cells, streaks, height, normals, and limb
  treatment own the visible planet-scale cloud read;
- quality presets tied to render scale, view steps, light steps, and cache
  cadence;
- diagnostics for every major field;
- shared `RunConfig` descriptors plus existing ImGui helper controls from the
  start.

Even while standalone, the project must keep planet handoff constraints visible:
use meters, carry planet radius/cloud-shell metadata explicitly, keep camera GPU
state camera-relative, and define weather coordinates so they can later map onto
a planet frame, local tangent frame, or stable global weather address.

Deferred until the shape is credible:

- ocean reflection integration;
- finished planet-scale orbit weather art direction;
- cloud shadow consumption by ocean/terrain;
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

## Renderer Contract

Clouds are a weather layer above clear-sky atmosphere. They should consume the
shared sky/celestial/atmosphere state and emit reusable outputs:

- cloud product RGB as linear cloud radiance and product alpha as view
  transmittance for background composition;
- mean distance, cloud opacity, and confidence in metadata/debug outputs for
  reconstruction and depth-aware composition;
- low-frequency cloud shadow factor for terrain/ocean;
- optional sky/reflection or environment contribution for water/PBR consumers;
- debug views for weather, base/detail density, lighting, shadow, cache,
  distance, local/orbit regime, steps, and composition.

Ocean, planet, and glTF/PBR viewers should not own cloud raymarch code. They
should sample cloud outputs or composed sky/environment products.

V1 should use `RenderGraphBuilder` to make the cloud product and composite
passes explicit. Descriptor sets, textures, material instances, and synchronization
policy remain project-owned until at least two consumers need a shared cloud
renderer contract.

## First Milestone

The first production milestone should be small and hard to fake:

1. Create `projects/cloud` as a new standalone project.
2. Port or reuse the `cloud_ref` texture-backed density path, but wire it to
   shared sky/celestial/atmosphere inputs and shared config descriptors from day
   one.
3. Render a cloud radiance/transmittance product and composite it in a separate
   pass.
4. Add raw diagnostics and a repeatable capture script before tuning.
5. Validate surface-up and high-oblique captures against `cloud_ref`, not
   `cloud_ref_2`.
6. Keep sampling and resolve controls isolated so capture bundles can compare
   default, Bayer, and no-jitter output before adding temporal accumulation.

Acceptance for this milestone:

- raw density has coherent cloud masses, not noisy curtains;
- final view still reads without relying on temporal smear or final blur;
- surface/high captures do not show the legacy horizontal streaking as the
  dominant artifact;
- the project exposes enough controls to isolate density, detail erosion,
  weather map, sampling, lighting, composition, and metadata output;
- the implementation does not duplicate project-local atmosphere horizon logic.

Only after that should the production project add the cached hemisphere path
from `cloud_ref_2`.

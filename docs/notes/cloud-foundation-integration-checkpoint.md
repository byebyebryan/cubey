# Cloud Foundation Integration Checkpoint

This note checkpoints the integration scope that promoted the former
`projects/cloud` standalone renderer into a shared weather layer. Current cloud
tuning happens through `projects/atmosphere`; planet now consumes the shared
runtime as an opt-in integration target.

## Batch Scope

- Promote reusable cloud contracts for cloud radiance/transmittance, metadata,
  low-frequency shadowing, and a future reflection contribution.
- Keep the production cloud app as the primary tuning surface while moving stable
  render vocabulary and shader assets into shared engine/render locations.
- Integrate clouded sky first through the atmosphere testbed, then add ocean
  shadow consumption smoke coverage.
- Do not integrate planet in this batch. Current planet sky/horizon issues are
  tracked separately in `docs/notes/sky-celestial-current-state.md`.
- Do not build a dynamic cloud reflection cubemap in this batch. The reflection
  surface is an API slot only until backdrop and shadow output are stable.

## Acceptance

- Cloud captures continue to render through the same visual model after the
  standalone app is absorbed.
- Shared cloud contracts are available without importing `projects/cloud`.
- Atmosphere can render a clouded-sky path without folding cloud noise into the
  clear-sky atmosphere shader.
- Ocean can consume a cloud shadow factor without raymarching clouds in the water
  shader.

## Landed Status

- Shared cloud vocabulary now lives in `cubey::render` through
  `include/cubey/render/cloud_layer.h` and `src/cubey/render/cloud_layer.cpp`.
  The promoted surface covers quality, sampling, distance/orbit modes, debug
  view enums, frame uniforms, temporal uniforms, generated resource helpers,
  cloud product metadata, a shadow product slot, and a placeholder reflection
  contribution slot.
- Cloud shaders moved under `shaders/cubey/cloud/`. `projects/atmosphere` is
  now the primary tuning surface, while generated cloud resources,
  march/temporal/composite materials and pipelines, render-graph product
  declaration, descriptor updates, and temporal history are owned by
  `cubey::render::CloudLayerRuntime`.
- `projects/atmosphere` now builds the shared cloud shaders, consumes
  `CloudLayerRuntime` in external-background mode, composites clouds over the
  clear-sky atmosphere in final view, and exposes a collapsed Clouds control
  group backed by the same `clouds.*` run-config overrides used by the
  standalone cloud project.
- `projects/ocean` now has a cloud-shadow diagnostic view and controls that
  consume the shared `CloudLayerShadowProduct` shape. The current shadow is
  procedural and local to ocean so water does not import the cloud raymarcher.
- `projects/planet` now has an opt-in `--clouds` path that composites the shared
  cloud product over the planet HDR scene using scene depth. It is an integration
  checkpoint, not a finished cloud shadow/reflection/environment-lighting path.

## Remaining Gaps

- The shared runtime is still a render-layer helper, not a full environment
  system. It does not yet output production cloud shadows, reflection probes, or
  clouded environment lighting.
- Atmosphere has runtime-backed cloud controls and can enable temporal resolve,
  but cloud debug-view surfacing, lighting feedback into the atmosphere, and
  cloud-driven environment/reflection outputs remain deferred.
- Ocean receives only a diagnostic analytic cloud shadow factor. It does not
  sample a real cloud shadow texture, cloud reflection product, or clouded
  sky-probe contribution.
- Planet cloud shadows, reflections, and final high-oblique/orbit transition
  polish remain deferred until the active planet sky/horizon issues are
  stabilized.

## Runtime Promotion Checkpoint

The reusable cloud runtime has landed without intentionally changing the cloud
look during promotion. `projects/atmosphere` now owns cloud-specific review
captures and visual tuning, and planet proves that a consumer can reuse the same
cloud renderer without copying descriptor or temporal-history code.

Still out of scope: production cloud shadow textures for ocean/terrain/planet,
cloud reflection or sky-probe contribution, clouded lighting feedback, and
visual retuning beyond integration fixes.

## High-Oblique Baseline 2026-06-28

Current absorbed-cloud review pack:

```sh
projects/atmosphere/capture_cloud_review.sh outputs/atmosphere-cloud-review-current
```

The pack now defaults to 1920x1080 full-quality output and includes a
`high-oblique-no-clouds` comparison beside `high-oblique-final`. The no-cloud
frame confirms the clear atmosphere/reference horizon is not the source of the
remaining high-oblique roughness; the issue is in the cloud handoff itself.

Current read:

- surface horizon/upward views remain the strongest baseline for local
  volumetric shape;
- high-oblique final has usable foreground clouds, but the far cloud band still
  reads harder and noisier than the clear-sky comparison;
- orbit-shell oblique is useful for shell/weather diagnostics, but it remains a
  flatter cloud-top representation rather than the desired high-oblique handoff;
- future work should keep the high-oblique comparison in the acceptance pack
  before promoting cloud shadows/reflections or planet-scale consumers.

## Surface Reference Reboot 2026-07-01

`projects/cloud_ref` is now locked as a narrow surface-view/local-volume
reference. It is useful because its surface cloud shape, deterministic Bayer
sampling, terrain-post resolve, lower ceiling, and optical-depth lighting give a
stable local signal with much less visible edge noise than the absorbed
foundation cloud path.

It is not the high-altitude or orbit target. Its camera-following cloud dome,
standalone sky/water context, and horizon masking are deliberate limitations
that should not be copied as production architecture.

The production reboot happens in the shared cloud layer through
`projects/atmosphere`. Use:

```sh
projects/atmosphere/capture_cloud_ref_parity.sh outputs/cloud-ref-parity-current
```

as the comparison pack before changing shared cloud lighting or horizon
handoff. The pack forces atmosphere into full-quality, local-only,
`cloud-ref-compatible` mode with temporal off, Bayer sampling, terrain-post
resolve, and no far-horizon cloud layer so shader and lighting differences stay
visible. Surface noon/backlit parity is the first acceptance target; dawn,
dusk, and night checks make sure the shared environment lighting contract does
not keep clouds sunlit after the sun is below the horizon.

## Reference-Parity Implementation Checkpoint 2026-07-01

The shared atmosphere path now has an explicit `reference-parity` cloud preset
that selects the local `cloud-ref-compatible` renderer rather than the high or
orbit cloud paths. This is intended as a narrow surface-cloud testbed, not a
replacement for the later high-oblique/orbit solution.

The shared cloud frame also receives resolved environment lighting from
`AtmosphereEnvironmentLighting`: separate sun, moon, and ambient terms are now
packed into cloud uniforms instead of assuming a fixed sun-only input. This
keeps the parity path compatible with the day/night environment contract before
cloud lighting is promoted to a fuller production model.

`cloud-ref-compatible` now uses the same local idea as `projects/cloud_ref` for
direct lighting: march light optical depth, integrate view transmittance through
optical thickness, keep deterministic single-frame sampling, and rely on the
terrain-post resolve for the final edge cleanup. The current capture pack is:

```sh
projects/atmosphere/capture_cloud_ref_parity.sh outputs/cloud-ref-parity-current
```

Current read:

- atmosphere surface-up/noon captures now recover the high-frequency local cloud
  shape closely enough to use as the next shared-cloud tuning baseline;
- `cloud_ref` remains the cleaner surface/horizon reference because it owns its
  own dome, water, and horizon masking;
- atmosphere horizon captures still expose integration artifacts, especially the
  hard horizon handoff and night/twilight contrast, so horizon polish is not
  accepted yet;
- high-oblique/orbit weather remains deliberately out of scope for this parity
  step.

## Surface Lighting Regime Checkpoint 2026-07-04

The `reference-parity` path now treats surface/night lighting as a separate
regime instead of allowing moonlit ambient and post contrast to reuse the same
strength as daylight. This removes the obvious blue-white cloud silhouette that
showed up at night while keeping stars and moonlight visible in the background.

The local `cloud-ref-compatible` march also has a surface-only grazing horizon
fade. This is not an orbit/high-oblique cloud solution; it is a scoped handoff
for the clean surface reference mode, where the cloud dome can otherwise expose
hard local-volume edges near the horizon. The fade works even when the optional
far-horizon layer is disabled by `reference-parity`.

Use this capture pack for future lighting changes:

```sh
projects/atmosphere/capture_cloud_lighting_regimes.sh outputs/cloud-lighting-regimes-current
```

Current read:

- surface-up noon remains the baseline for local cloud shape and high-frequency
  detail;
- surface-up night no longer reads as sunlit clouds against a dark sky;
- twilight is intentionally softer than noon, but should not wash the cloud
  layer into a flat pale blanket;
- horizon captures are diagnostic only for this surface reference pass. A
  production high-oblique/orbit handoff still needs separate treatment.

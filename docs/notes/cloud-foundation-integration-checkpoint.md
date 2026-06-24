# Cloud Foundation Integration Checkpoint

This note checkpoints the integration scope before promoting `projects/cloud`
from a standalone renderer into a shared weather layer.

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

- Cloud standalone captures continue to render through the same visual model.
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
- Cloud shaders moved under `shaders/cubey/cloud/`. `projects/cloud` remains
  the primary tuning surface, but generated cloud resources, march/temporal/
  composite materials and pipelines, render-graph product declaration,
  descriptor updates, and temporal history are now owned by
  `cubey::render::CloudLayerRuntime`.
- `projects/atmosphere` now builds the shared cloud shaders, consumes
  `CloudLayerRuntime` in external-background mode, composites clouds over the
  clear-sky atmosphere in final view, and exposes a collapsed Clouds control
  group backed by the same `clouds.*` run-config overrides used by the
  standalone cloud project.
- `projects/ocean` now has a cloud-shadow diagnostic view and controls that
  consume the shared `CloudLayerShadowProduct` shape. The current shadow is
  procedural and local to ocean so water does not import the cloud raymarcher.

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
- Planet integration remains deferred until the active planet sky/horizon issues
  are stabilized.

## Runtime Promotion Checkpoint

The reusable cloud runtime has landed without intentionally changing the
standalone cloud look. `projects/cloud` still owns cloud-specific camera modes,
diagnostic captures, and visual tuning; `projects/atmosphere` now proves that a
consumer can reuse the same cloud renderer without copying descriptor or
temporal-history code.

Still out of scope: planet integration, production cloud shadow textures for
ocean/terrain, cloud reflection or sky-probe contribution, clouded lighting
feedback, and visual retuning beyond integration fixes.

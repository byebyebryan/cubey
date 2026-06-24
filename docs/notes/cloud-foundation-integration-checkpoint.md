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
- Cloud shaders moved under `shaders/cubey/cloud/`. `projects/cloud` still owns
  the full app runtime, UI, temporal history orchestration, captures, and visual
  tuning, but it now consumes the shared layer constants, layouts, pass metadata,
  generated-resource helpers, and uniform packing.
- `projects/atmosphere` now builds the shared cloud shaders and composites a
  conservative half-resolution cloud product over the clear-sky atmosphere in
  final view. This is a backdrop smoke path, not yet a full atmosphere/cloud
  controls surface or temporal cloud integration.
- `projects/ocean` now has a cloud-shadow diagnostic view and controls that
  consume the shared `CloudLayerShadowProduct` shape. The current shadow is
  procedural and local to ocean so water does not import the cloud raymarcher.

## Remaining Gaps

- No complete shared cloud renderer runtime exists yet. Textures, descriptors,
  render graph wiring, temporal history, and UI ownership remain mostly
  project-owned.
- Atmosphere does not yet expose cloud controls, temporal cloud resolve, cloud
  debug views, or cloud-driven environment/reflection outputs.
- Ocean receives only a diagnostic analytic cloud shadow factor. It does not
  sample a real cloud shadow texture, cloud reflection product, or clouded
  sky-probe contribution.
- Planet integration remains deferred until the active planet sky/horizon issues
  are stabilized.

## Runtime Promotion Plan

The next foundation pass should promote a reusable cloud runtime without changing
the standalone cloud look. The runtime should own generated cloud resources,
march/temporal/composite materials and pipelines, transient cloud products,
temporal history, and descriptor binding. `projects/cloud` remains the primary
tuning surface, while `projects/atmosphere` should consume the same runtime in
external-background mode with editable cloud controls.

Out of scope for that pass: planet integration, production cloud shadow textures
for ocean/terrain, cloud reflection or sky-probe contribution, and visual
retuning beyond fixes required by the runtime migration.

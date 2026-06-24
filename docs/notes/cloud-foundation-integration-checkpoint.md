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

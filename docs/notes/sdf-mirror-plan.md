# Mirrored stroke authoring

Status: implementation in progress, 2026-10-22.

The organic guardian demonstrates useful authored detail, but its history is
1,011 segment reveals rather than 113 sculpting strokes. Bilateral features
are emitted separately. Introduce an opt-in X mirror authoring helper and
atomic whole-stroke history, retaining ordinary explicit version-2 geometry.

## Contract

- A history item accepts either legacy `node` or nonempty `nodes`, never both.
  Every primitive appears once across the history. A grouped operation commits,
  seeks, and undoes all its primitives together. Mixed additive/subtractive
  groups are rejected so the pre-commit preview has an unambiguous meaning.
- A mirrored stroke includes its reflected copy in the same history item.
  Reflect positions and quaternion frames, including nested transforms. Retain
  materials, tapered endpoints, and Boolean structure. Centerline operations
  are emitted once; mirror can be disabled for asymmetric finishing.
- Preview every subtractive primitive in an operation, in red and depth limited
  by the current solid. Per-frame preview storage must not overwrite in-flight
  frames. Keep existing bounded geometry/program capacities.
- This is scripted authoring plus native playback, not a new interactive brush
  editor. It reduces authoring/replay operations, not steady geometry evaluation.

## Delivery and acceptance

1. Implement grouped history and previews; test malformed histories, atomic
   undo/seek, transformed paired cuts, nested subtraction, and legacy behavior.
2. Add reusable mirror authoring helpers with analytic reflection tests. Create
   a separate guardian from the saved organic source, retaining central forms
   and intentionally asymmetric central strokes. Use the left bilateral stroke
   as the source for its mirrored counterpart; report that geometry change.
3. Qualify every native mirrored history state, final static/replay agreement,
   export/reload, paired red cues, and desktop controls. Preserve the original
   guardian and its evidence. Compare matched final views and controlled costs.
4. Publish a compact visual report and document measurements, scope, and limits.
   Commit reviewable milestones locally; do not push.

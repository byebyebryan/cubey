# Terrain Mountain Macro Shape Plan

Date: 2026-07-01

Revision 24 added diagnostic gully fields, but it did not improve the visible
mountain mesh because `terrain_preview` still renders `height_m`. The next pass
should make the difference explicit: add a preview surface selector for
before/after review, then improve the mountain stress recipe's source hierarchy
so `height_m` itself reads more like broad mountain mass, connected ridges, and
summit buildup.

## Decision

Revision 25 should target mountain macro shape first. The gully diagnostic stays
review-only until the mountain source is strong enough to judge whether erosion
detail is helping or just adding texture.

The source hierarchy should add three explicit mountain fields:

- `mountain_mass`: broad highland/range support;
- `mountain_shoulder`: foothill and shoulder buildup around the mass;
- `mountain_summit_core`: sparse summit cores derived from peak/ridge support.

The preview path should also be able to render alternate product surfaces:

- `height`;
- `post-erosion`;
- `pre-process`.

## Implementation Boundary

- Bump the generator revision because public fields and debug views change.
- Keep the gully pass diagnostic-only; do not apply `erosion_delta_m` to
  `height_m`.
- Do not change river topology, water bodies, materials, vegetation, or shared
  procedural foundation in this batch.
- Treat `post-erosion` preview geometry as a diagnostic surface. Materials and
  river/wetness fields still describe `height_m`, so use height color for that
  comparison unless specifically debugging material mismatch.

## Acceptance

- `terrain_preview` can render `height`, `post-erosion`, and `pre-process`
  surfaces.
- The mountain stress recipe emits inspectable `mountain_mass`,
  `mountain_shoulder`, and `mountain_summit_core` fields.
- Default river recipes keep the new mountain macro fields inactive.
- Tests prove summit-core average height is higher than broad mountain mass,
  which is higher than lowland samples.
- Refreshed captures include both normal mountain perspective and
  `post-erosion` perspective comparison output.

## Outcome

Implemented in revision 25.

- `terrain.preview_surface` and `--terrain-preview-surface` select `height`,
  `post-erosion`, or `pre-process` geometry for `terrain_preview`.
- The mountain stress recipe emits `mountain_mass`, `mountain_shoulder`, and
  `mountain_summit_core` with matching scalar debug views.
- Default river recipes keep those macro fields inactive.
- Tests cover the inactive default fields, active mountain stress hierarchy,
  debug export coverage, and alternate preview surfaces.
- Refreshed local captures:
  - `outputs/terrain/mountain-range-stress/` at `513x513`, revision 25, 50
    fields, 44 scalar views, plus oblique/profile/post-erosion previews.
  - `outputs/terrain/mountain-range-stress-1025/` at `1025x1025`, revision 25,
    50 fields, 44 scalar views.

Visual read: broad mass and shoulder buildup are clearer in perspective, and
summit support is sparse enough to inspect separately. The remaining visible
problem is source shape quality: some peaks still read too pointy/stylized, so
the next mountain batch should refine summit/ridge source shape before adding
more erosion detail.

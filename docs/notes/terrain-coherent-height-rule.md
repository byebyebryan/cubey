# Terrain Coherent Height Rule

Date: 2026-07-01

The revision 25 mountain review exposed a recurring terrain failure mode:
separate masks were independently adding visible elevation. That made the
terrain inspectable, but it still produced flat shoulders, pointy peaks, and
jagged ridge strokes because the final height was assembled as pasted features.

## Rule

`height_m` should come from one coherent terrain state.

Feature fields can shape that state before height is solved, or they can be
derived diagnostics after height is solved. They should not become independent
visible height layers that stack into shelves, cones, or disconnected strokes.

## Mountain Application

The mountain stress recipe should build height from a continuous mountain
profile:

```text
range/peak potential -> coherent profile height -> derived diagnostics -> bounded detail
```

The diagnostics remain useful:

- `mountain_mass`: broad highland support;
- `mountain_shoulder`: foothill and shoulder ramp;
- `mountain_summit_core`: sparse summit influence;
- ridge and peak fields: attribution and review of where the profile steepens.

Those fields should explain the profile, not replace it with separate additive
uplifts.

## Failure Modes To Guard

- broad high-shoulder regions that are almost flat;
- isolated needle peaks without surrounding summit support;
- grid-walk ridge strokes that define the visible silhouette;
- local detail or peak/ridge uplift dominating macro height.

Revision 26 applies this rule to the mountain stress source by introducing
`mountain_profile_height_m` as the coherent source height. The result removes
the worst stacked-layer artifacts, but it is still a midpoint: the next mountain
work should make summit and ridge shaping less rounded without returning to
independent pasted height layers.

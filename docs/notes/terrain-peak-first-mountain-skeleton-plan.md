# Terrain Peak-First Mountain Skeleton Plan

Revision 20 made the mountain source layers inspectable, but it still reads too
much like a random field with bumps. The named hierarchy is not yet a form
hierarchy: peaks do not dominate the composition, ridges do not visibly build
toward those peaks, and height still mostly follows processed noise.

Status: planned for revision 21.

## Direction

Keep the pass scoped to `temperate-mountain-range-stress`. Default river recipes
should keep their current visual behavior and continue emitting inactive
mountain stress-only fields where needed.

The mountain stress recipe should move to a peak-first skeleton:

- `mountain_envelope`: smooth macro support and broad uplift mass.
- `mountain_peak_anchors`: sparse deterministic local maxima selected from the
  envelope with spacing and prominence checks.
- `mountain_peak_prominence`: peak dominance field derived from anchors, not
  local noise alone.
- `mountain_ridge_skeleton`: sparse primary/secondary ridge paths connecting
  and branching from accepted peaks.
- `mountain_ridge_influence`: broader ridge shoulder field derived from the
  skeleton and used for support and uplift.

The existing revision 20 fields remain useful diagnostics, but they should
become derived from the skeleton model instead of being the primary source.

## Implementation Batch

1. Add the five new product fields and debug views.
2. Generate a smooth mountain envelope from coherent low-frequency fields.
3. Select peak anchors using deterministic local maxima, grid-size-scaled
   spacing, and a bounded target count.
4. Build a ridge skeleton by connecting accepted peaks with deterministic
   weighted paths over the envelope/support field, then add short secondary
   branches from dominant peaks.
5. Derive revision 20 fields from the skeleton:
   - `mountain_support` from envelope plus ridge influence;
   - `mountain_ridge_hierarchy` and `ridge_support` from ridge influence;
   - `mountain_peak_candidates` from peak anchors/prominence;
   - `peak_support` from prominence gated by support and ridge influence.
6. Build height from broad envelope uplift, ridge shoulder uplift, peak
   prominence uplift, then damped fine residual detail.
7. Refresh the mountain stress captures and docs.

## Review Criteria

- `mountain-envelope.png` should be broad and smooth.
- `mountain-peak-anchors.png` should show a small number of spaced anchors.
- `mountain-peak-prominence.png` should show dominant buildup around anchors.
- `mountain-ridge-skeleton.png` should be sparse and connected enough to read as
  ridge structure, not a full noisy field.
- `mountain-ridge-influence.png` should widen the skeleton into ridge shoulders.
- `mountain-relief.png` should read as broad mass building to peaks through
  ridges, even before erosion, talus, snow, or glacial shaping exists.

## Deferred

- Tectonic plates and world-scale range graphs.
- Physically evolved thermal/hydraulic erosion.
- Talus, snow/ice, treeline, and alpine material polish.
- Applying mountain envelope or peak uplift to default river recipes.

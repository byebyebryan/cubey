# Terrain Mountain Hierarchy Plan

Revision 19 made mountain fields explicit, but the stress capture still reads
too much like noisy rough terrain in places. The next correction should improve
the source hierarchy before adding alpine biome polish.

## Direction

Keep the work scoped to `temperate-mountain-range-stress`. The default river
recipe should continue emitting the same field contract with broad mountain and
peak uplift disabled so river captures stay comparable.

The goal is not a finished mountain biome. It is a better mountain driver with
inspectable source layers:

- `mountain_range_spine`: coherent macro range bands. This is the source driver
  for where mountain mass is allowed to organize, not a hand-authored line.
- `mountain_ridge_hierarchy`: ranked ridge support inside the range support,
  combining primary ridges with secondary ridges instead of using one flat
  ridged-noise field everywhere.
- `mountain_peak_candidates`: sparse summit candidates tied to high range and
  ridge hierarchy, so peaks are attached to ridges instead of scattered by local
  noise alone.

## Implementation Batch

1. Add the three fields to the product contract and debug review set.
2. Build the mountain stress recipe from those fields:
   - broaden `mountain_support` from the range spine plus low-frequency support;
   - derive `ridge_support` from the hierarchy field, gated by support;
   - derive `peak_support` from peak candidates, ridge hierarchy, and support;
   - reduce fine detail where macro support is weak, so the capture reads as
     organized range form before local noise.
3. Keep the default recipe behavior stable by emitting zero range spine, zero
   peak candidates, and the existing ridge source for non-mountain-stress
   recipes.
4. Add tests for field presence, deterministic mountain output, bounded
   coverage, and debug-view parsing/export.
5. Refresh `outputs/terrain/mountain-range-stress` and
   `outputs/terrain/mountain-range-stress-1025`.

## Review Criteria

Review `mountain-relief.png` first, then the source fields:

- `mountain-range-spine.png` should read as broad coherent range organization,
  not as a circular mask or quadrant.
- `mountain-ridge-hierarchy.png` should show primary/secondary ridge structure
  within the range support.
- `mountain-peak-candidates.png` should be sparse and attached to high ridge
  hierarchy.
- `mountain-relief.png` should show clearer macro mountain form with detail
  layered onto it, while still being honest about the lack of erosion, talus,
  snow, and glacial shaping.

## Deferred

- Tectonic plate or world-scale range graph.
- Thermal/hydraulic erosion time.
- Glacial valley carving, snow/ice, treeline, or biome material polish.
- Applying mountain uplift to the default river recipe.

# Archived Terrain Attempts

Date: 2026-07-19

The former `terrain_workbench_legacy`, `terrain_lab_legacy`, and
`procedural_terrain_legacy` applications were removed after the terrain product
promotion. Git history remains the implementation archive; this note preserves
the conclusions that should constrain later work.

## Terrain Lab

The local biome slices established that named intermediate fields and
side-by-side diagnostics are useful, but isolated feature masks do not produce
coherent terrain. Authored canyon lines, ridge skeletons, quadrant masks, and
similar patch-local composition repeatedly read as artificial. Biome recipes
should consume coherent source and process fields rather than inventing visible
landforms independently.

The river work also established a scope boundary. D8 and D-infinity fields are
useful diagnostics, but turning a finite routing raster into a visually natural,
continuous river network became a hydrology project in its own right. That work
now belongs in `studies/terrain/hydrology`, not in the terrain renderer.

## Terrain Workbench

The regional workbench made generation phases, scalar exports, and process
attribution inspectable. It also showed the cost of treating every experimental
field as a runtime product: generation became slow, artifacts grew large, and
the renderer inherited controls for source versions, routing, erosion, and
presentation experiments.

Mountain attempts based on stacked ridge, summit, shoulder, and valley masks
produced thin fins, pointy cones, flat shelves, and blurred corrections. The
important lesson is not a preferred ridge formula; it is that the visible
heightfield must remain one coherent source. Process fields may modify or
explain that surface, but they should not be independently painted features.

## Coastal Procedural Terrain

The coastal demo proved a useful data vocabulary but not a reusable terrain
generator. The durable boundary is the shared height, water depth, shoreline
signed distance, slope, and optional material-mask contract preserved in
[`terrain-ocean-field-contract.md`](terrain-ocean-field-contract.md).

## Promotion Consequences

- `projects/terrain` owns one raster heightfield backdrop path.
- Retained visual references and hydrology experiments are opt-in studies.
- Generated terrain and large diagnostic packs do not live in Git.
- Close terrain, hydrology, coastlines, foliage, and planet-scale streaming are
  explicit later projects rather than hidden modes of the backdrop app.
- Future source work starts from a known-good generator or reference and must
  preserve source provenance, diagnostic comparisons, and runtime cost gates.

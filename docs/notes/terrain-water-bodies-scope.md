# Terrain Water Bodies Scope

Date: 2026-06-29

This note captures the water-body boundary before moving from river work to the
next terrain driver. Rivers, lakes, wetlands, coast, and ocean are related, but
they should not be implemented as one large water feature.

## Current Classification

- Rivers: active terrain-process product. The stress recipe now uses a
  graph-first source topology, graph discharge diagnostics, major-channel trunk
  promotion, and minor tributary fields.
- Lakes and wetlands: future terrain hydrology products. They should begin as
  basin/standing-water fields and diagnostics, not as full hydraulic simulation.
  Useful future fields include `lake_mask`, `wetland_mask`, `water_level`,
  `outlet`, `overflow`, and `shore_distance`.
- Coast and ocean: terrain/ocean/planet handoff data, not terrain-owned water
  rendering. Terrain should eventually provide sea-datum-relative fields such
  as bathymetry, shoreline signed distance, coastal material bands, beach slope,
  and river-mouth or estuary hints.

## Boundaries

Do not add lake, wetland, coast, or ocean implementation in the next terrain
driver batch. Lake and wetland generation need a basin/outlet model that is
more scale-aware than the current patch-local river graph. Coast work should
wait until the terrain product is ready to feed the existing ocean and planet
contracts instead of reinventing ocean rendering inside `projects/terrain`.

For now, keep water-body work as explicit product vocabulary and diagnostics.
The next non-river implementation batch should move to a separate terrain
foundation driver, most likely mountains/ridges, so rivers are not overfit
before other terrain sources exist.

## Existing Anchors

- `docs/architecture/terrain-reboot.md`: terrain product vocabulary, including
  future lake/wetland masks and shoreline/bathymetry adapters.
- `projects/procedural_terrain_legacy/terrain_ocean_contract.md`: legacy
  shoreline/ocean handoff vocabulary that should remain an adapter target.
- `docs/architecture/planet-rendering.md`: planet-scale terrain/ocean handoff
  and water-datum vocabulary.

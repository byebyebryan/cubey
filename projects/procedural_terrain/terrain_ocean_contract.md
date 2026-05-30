# Terrain-Ocean Field Contract

`procedural_terrain` owns static terrain fields that downstream shoreline and
ocean rendering can consume. Ocean systems own time-varying wave motion, foam,
surface shading, and water simulation state.

## Coordinates and Units

- World space is right-handed with `+Y` up.
- Terrain samples are laid out on the XZ plane, centered around world origin.
- `cell_size_m` is the spacing between neighboring field samples in meters.
- Terrain height is `height_m`, measured in meters with positive values above
  the configured sea level.
- Water depth is `water_depth_m = max(0, sea_level_m - height_m)`, measured
  positive downward.

## Indexing

- Field arrays are row-major: `index = y * width + x`.
- `x` advances along world `+X`; `y` advances along world `+Z`.
- The grid origin in `TerrainGridDesc` is the terrain-space origin for the
  field; current generated terrain uses `origin_x_m = 0` and `origin_z_m = 0`.
- Consumers must use the same `width`, `height`, and `cell_size_m` from
  `TerrainGridDesc`; do not infer dimensions from mesh vertex counts.

## Fields

- `height_m`: static terrain/bathymetry elevation in world meters.
- `water_depth_m`: static still-water depth relative to `sea_level_m`.
- `shore_sdf_m`: signed distance to the still-water shoreline; positive on land,
  negative underwater, approximately zero at the shoreline.
- `slope`: magnitude of the sampled height gradient.
- `material_masks`: normalized weights for sand, rock, vegetation, and sediment.

## Ownership Boundary

- Terrain owns the analytical still-water shoreline and material masks.
- Ocean owns dynamic displacement, surface normals, foam evolution, and final
  water shading.
- Ocean should treat `shore_sdf_m` and `water_depth_m` as setup inputs for
  attenuation, foam seeding, wet sand masks, and near-shore wave behavior.
- Terrain debug views may render full bathymetry; final terrain visuals may use
  clipped land meshes and water overlays without changing the analytical fields.

# Terrain

`projects/terrain` is the active terrain reboot. It produces a deterministic
CPU terrain patch with named source, geometry, and regional hydrology fields;
the scalar exporter and Vulkan renderer consume that same product.

The first recipe is `upland-catchment-v1`. Its default patch is `257x257` at
`32 m` per sample with a 32-sample process halo. The published product contains
only the requested interior.

## Product Fields

Source and geometry:

- `source_height_m`, `mountain_support`, `height_m`;
- `slope`, `curvature`, `local_relief_m`.

Regional hydrology diagnostics:

- `routing_surface_m`, `routing_fill_delta_m`;
- `flow_direction_x`, `flow_direction_z`;
- `contributing_area_m2`, `stream_order`, `discharge_proxy`;
- `sink_mask`, `flow_boundary_mask`.

Hydrology does not alter `height_m`. There is no river mask, channel carving,
water, material product, vegetation, LOD, streaming, or planet adapter yet.

## Commands

```sh
cmake --build --preset dev --target \
  cubey_project_terrain cubey_project_terrain_generate cubey_project_terrain_tests
ctest --preset dev -R '^(terrain_reaches_glfw|terrain_tests|terrain_generate_exports_(fields|manifest)|terrain_headless_writes_png(_stats)?|terrain_surface_low_headless_writes_png(_stats)?)$' \
  --output-on-failure

./build/dev/projects/terrain/terrain_generate \
  --terrain-seed 9012 \
  --terrain-output-dir outputs/terrain/v1-upland-catchment/fields/seed-9012

./build/dev/projects/terrain/terrain \
  --terrain-seed 9012 \
  --terrain-camera-preset oblique \
  --debug-view surface

projects/terrain/capture_review.sh
```

The renderer accepts `surface`, `flow-direction`, `height`, `source`, or any
published field name through `--debug-view`. Camera presets use the shared
terrain choices, including `oblique`, `top`, `profile`, `surface`, and
`surface-low`.

The ignored review output lives under
`outputs/terrain/v1-upland-catchment/`. See
[`docs/notes/terrain-v1-baseline.md`](../../docs/notes/terrain-v1-baseline.md)
for the first multi-seed findings and known routing limitations.

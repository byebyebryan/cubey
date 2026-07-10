# Terrain

`projects/terrain` is the active terrain reboot. It produces a deterministic
CPU terrain patch with named source, geometry, and regional hydrology fields;
the scalar exporter and Vulkan renderer consume that same product.

The default recipe is the corrected `upland-catchment-v1` revision 2 baseline.
`upland-broad-noise-control-v1` is an OpenSimplex comparison source, not a
promoted replacement. The default patch is `257x257` at `32 m` per sample with
a 32-sample process halo. The published product contains only the requested
interior.

## Product Fields

Source and geometry:

- `source_height_m`, `mountain_support`, `height_m`;
- `slope`, `curvature`, `local_relief_m`.

The broad-noise control additionally publishes `uplift_potential`,
`macro_mass`, and `base_relief_m`.

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
ctest --preset dev -L terrain --output-on-failure

./build/dev/projects/terrain/terrain_generate \
  --terrain-seed 9012 \
  --terrain-recipe upland-broad-noise-control-v1 \
  --terrain-origin-x 0 --terrain-origin-z 0 \
  --terrain-export-raw \
  --terrain-output-dir outputs/terrain/source-control/seed-9012

./build/dev/projects/terrain/terrain \
  --terrain-seed 9012 \
  --terrain-camera-preset oblique \
  --debug-view surface

projects/terrain/capture_review.sh

projects/terrain/run_analytical_oracle.py \
  --analytical-ref ~/code/ref/analytical-terrains
```

Raw `.f32` output is opt-in and exists for lossless research interchange. The
analytical runner uses the separately cloned, research-only reference as an
external oracle; it is not a build or runtime dependency.

The renderer accepts `surface`, `flow-direction`, `height`, `source`, or any
published field name through `--debug-view`. Camera presets use the shared
terrain choices, including `oblique`, `top`, `profile`, `surface`, and
`surface-low`.

The current ignored review output lives under
`outputs/terrain/source-bakeoff-v1/`. See
[`docs/notes/terrain-source-bakeoff-v1.md`](../../docs/notes/terrain-source-bakeoff-v1.md)
for measurements, visual findings, and the next model boundary. The older
single-recipe layout remains available through `capture_v1_baseline.sh`.

The next candidate is the finite regional analytical model specified in
[`docs/notes/terrain-landscape-evolution-v1.md`](../../docs/notes/terrain-landscape-evolution-v1.md).
It will keep the broad source as its initial condition and add uplift-driven
landscape evolution before any fine-detail amplification work.

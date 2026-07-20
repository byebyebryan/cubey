# Terrain Hydrology Study

`studies/terrain/hydrology` preserves the previous CPU patch, regional
hydrology, and analytical landscape-evolution experiment. Development is
paused while `projects/terrain` follows the directly sampled terrain v1
runtime. This project remains buildable so its diagnostics and evidence do not
rot, but it is not a dependency or contract donor for terrain v1.

The default recipe is the corrected `upland-catchment-v1` revision 2 baseline.
`upland-broad-noise-control-v1` is an OpenSimplex comparison source, not a
promoted replacement. `upland-landscape-evolution-v1` is the finite regional
candidate: it evolves the broad source through deterministic river trees and a
transient analytical erosion model. The default patch is `257x257` at `32 m`
per sample with a 32-sample process halo. The landscape candidate internally
uses a 64-sample guard. Every published product contains only the requested
interior.

## Product Fields

Source and geometry:

- `source_height_m`, `mountain_support`, `height_m`;
- `slope`, `curvature`, `local_relief_m`.

The broad-noise control additionally publishes `uplift_potential`,
`macro_mass`, and `base_relief_m`.

The landscape candidate additionally publishes physical model inputs and
process truth:

- `uplift_rate_m_per_year`, `process_drainage_area_m2`;
- `process_flow_direction_x`, `process_flow_direction_z`,
  `process_breach_mask`;
- `fluvial_advection_rate_m_per_year`,
  `hillslope_advection_rate_m_per_year`, `thermal_active_mask`;
- `analytical_height_m`, `altitude_correction_delta_m`, `process_delta_m`.

Regional hydrology diagnostics:

- `routing_surface_m`, `routing_fill_delta_m`;
- `flow_direction_x`, `flow_direction_z`;
- `contributing_area_m2`, `stream_order`, `discharge_proxy`;
- `sink_mask`, `flow_boundary_mask`.

Hydrology does not alter `height_m`. There is no river mask, channel carving,
water, material product, vegetation, LOD, streaming, or planet adapter yet.

## Commands

```sh
cmake --preset dev-terrain-studies
cmake --build --preset dev-terrain-studies --target \
  cubey_study_terrain_hydrology \
  cubey_study_terrain_hydrology_generate \
  cubey_study_terrain_hydrology_tests
ctest --preset dev-terrain-studies -L terrain_hydrology_lab --output-on-failure

./build/dev-terrain-studies/studies/terrain/hydrology/terrain_hydrology_generate \
  --grid-size 513 --terrain-cell-size 100 \
  --terrain-seed 9012 \
  --terrain-recipe upland-landscape-evolution-v1 \
  --terrain-output-dir outputs/terrain_hydrology_lab/landscape-evolution-v1/seed-9012

./build/dev-terrain-studies/studies/terrain/hydrology/terrain_hydrology_generate \
  --terrain-seed 9012 \
  --terrain-recipe upland-broad-noise-control-v1 \
  --terrain-origin-x 0 --terrain-origin-z 0 \
  --terrain-export-raw \
  --terrain-output-dir outputs/terrain_hydrology_lab/source-control/seed-9012

./build/dev-terrain-studies/studies/terrain/hydrology/terrain_hydrology \
  --terrain-seed 9012 \
  --terrain-camera-preset oblique \
  --debug-view surface

studies/terrain/hydrology/capture_review.sh

studies/terrain/hydrology/capture_landscape_evolution_v1.sh

studies/terrain/hydrology/run_analytical_oracle.py \
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
`outputs/terrain_hydrology_lab/source-bakeoff-v1/`. See
[`docs/notes/terrain-source-bakeoff-v1.md`](../../../docs/notes/terrain-source-bakeoff-v1.md)
for measurements, visual findings, and the next model boundary. The older
single-recipe layout remains available through `capture_v1_baseline.sh`.

The finite regional analytical candidate is specified in
[`docs/notes/terrain-landscape-evolution-v1.md`](../../../docs/notes/terrain-landscape-evolution-v1.md).
It keeps the broad source as its initial condition and adds uplift-driven
landscape evolution before any fine-detail amplification work. It is regional
truth, not an independently seam-safe tile recipe; later streaming should
extract patches from a shared regional solve.
Its three-seed evidence and remaining four-neighbor routing artifacts are
captured in
[`docs/notes/terrain-landscape-evolution-v1-review.md`](../../../docs/notes/terrain-landscape-evolution-v1-review.md).

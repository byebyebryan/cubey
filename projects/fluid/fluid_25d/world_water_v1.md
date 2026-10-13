# Prepared-world Terrain Water V1

Status: integration/validation in progress. No default replacement or push.

Connect the existing shared world/site packages to the managed native rainfall
demo and solver-neutral captured-water consumer. Keep immutable elevations,
the native session protocol, existing six-site catalogue and rendering defaults.

The explicit `terrain_water_world_sites.json` collection contains the original
mountain control, interior mountain and interior valley. Its three sites and
five established grid presets fit the existing 32-case control bound. Missing
or changed prepared packages disable admission; no inference or substitute asset
is launched. The original collection remains the default.

Prepared cases retain the shared `cubey.terrain.sampled-source.v1` binding with
an optional `terrain_site` identity: package manifest path/hash and world ID.
Shared Terrain package admission validates producer, height transform, world
coordinates, nested coverage and margins. World crops may not shift at borders.
Raster/climate payload identities retain their existing checks.

Coverage is not hydrology: the default 1024²/16m live rain region is nominally
16.384 km square; the approximately 131 km prepared backdrop is ground-only.
The 262 km atlas is a display/navigation product, not solver input. No coarse
outer water splice, full-world simulation, new solver, source editing or rain-off
capture is part of this pass.

Validation checkpoints: strict package/case admission; shared exterior ground
with context-off parity and seam checks; managed selection/reset lifecycle;
matched dry-start 512 mm/h rainfall at 512²/32m (6 physical hours) and
1024²/16m (12 hours); exact rain-on export; Terrain/Fluid visual review; repeated
headless concurrent performance and a ten-minute live soak. Proposed 1080p
targets are GPU scene p95 <=8ms, CPU service p95 <=4ms, displayed-field age p95
<=1s without backlog, and measured pace >=54x for a 60x request. Paced frame
intervals include intentional waiting and are reported separately.

Generated cases, fields and evidence stay ignored under cache/outputs. Capture
time, live simulation time and decorative motion must remain distinctly labelled.
Native GUI/human acceptance is separate from automated headless validation.

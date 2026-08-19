# Retired Project Snapshot

These whole-project viewers were removed from the active CMake graph and are no
longer supported runnable targets. Their last pre-cleanup source snapshot is
the local Git commit `e33ae632` (`refactor(config): remove lazy-serializable`),
which is the recovery and historical comparison anchor if an implementation
detail must be revisited.

- `projects/cloud_ref_2`: a Godot-v2-style cached-sky/cloud architecture
  experiment. It was removed because the active shared cloud layer owns the
  production path and this viewer had no remaining consumer.
- `projects/clouds_legacy`: the frozen first-pass planet-aware cloud/weather
  viewer. It was removed after its scale, horizon, UI, and integration lessons
  were captured in the cloud architecture notes and no runnable target owned
  it.
- `projects/planet_legacy`: the pre-reboot cube-sphere/local-detail planet
  viewer. It was removed because the active orbital `projects/planet` product
  owns the planet path and the old viewer would otherwise force a legacy
  RunConfig migration.

The retained terrain studies under `studies/terrain` remain opt-in research
sources and algorithm/export tests; their viewer layers were retired separately
without deleting their useful source evidence.

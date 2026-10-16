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
  viewer. It was removed when the orbital `projects/planet` reboot owned
  the planet path and the old viewer would otherwise force a legacy
  RunConfig migration.

The retained terrain studies under `studies/terrain` remain opt-in research
sources and algorithm/export tests; their viewer layers were retired separately
without deleting their useful source evidence.

## Reclassified cloud reference (2026-10-15)

Unlike the removed viewers above, `projects/cloud_ref` was moved rather than
deleted. Its source, attribution, diagnostic executable, and tests are retained
under [studies/cloud/terrainengine](../../studies/cloud/terrainengine/README.md),
behind `CUBEY_BUILD_CLOUD_STUDIES=ON` / the `dev-cloud-studies` preset. Normal
project builds no longer include it. Existing reference identifiers remain for
comparison continuity; there is no replacement standalone `cloud` product.
Production clouds remain in the shared atmosphere/environment foundation and
are showcased through Atmosphere.

## Paused Planet study (2026-10-15)

The orbital reboot was moved from `projects/planet` to
[studies/planet](../../studies/planet/README.md). It did not reach the intended
visual quality and fell behind active environment work. The last pre-move
snapshot is `c8afc450`; the older surface/LOD viewer above remains in Git history.

The orbital C++, shaders, configuration, tests, and headless review tools are
retained behind `CUBEY_BUILD_PLANET_STUDIES=ON` / `dev-planet-studies`. Normal
project builds, portfolio entries, and the showcase queue exclude Planet.
The [design](../../studies/planet/design.md) and
[former shot proposal](../../studies/planet/showcase-plan.md) are paused study
evidence. Existing outputs/caches and archived surface work were preserved.
Shared sky, procedural generation, caching, and staging remain active foundation
code. Any revival requires a new visual target and current foundation assessment.

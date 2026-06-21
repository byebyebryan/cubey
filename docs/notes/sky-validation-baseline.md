# Sky Validation Baseline

Captured on 2026-06-21 15:21 PDT from the `sky-rendering` worktree.

Tested source commit before this note commit:

```text
ffd38dfc test: update archived terrain ui paths
```

## Baseline Commands

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_core_tests cubey_project_atmosphere_tests cubey_project_atmosphere cubey_project_planet_celestial_tests cubey_project_planet cubey_png_stats
ctest --preset dev -R '^(cubey_core_tests|atmosphere_config_tests|planet_celestial_tests)$'
ctest --preset dev -L sky
git diff --check
```

## Results

- `cmake --preset dev`: passed.
- Baseline build targets: passed. The final post-fix rebuild reported `ninja: no
  work to do`.
- Focused unit tests: passed, 3/3.
- `ctest --preset dev -L sky`: passed, 39/39.
- Vulkan-dependent PNG smoke tests ran and passed; no sky tests skipped.
- `git diff --check`: passed.

## Notes

The first focused unit-test run found an unrelated stale source-path assertion in
`cubey_core_tests`: `tests/cubey/ui_foundation_tests.cpp` still referenced
`projects/procedural_terrain/procedural_terrain_ui.cpp` after the terrain project
was archived under `projects/procedural_terrain_legacy`. Commit `ffd38dfc`
updates those paths before this baseline was recorded.

The `sky` CTest label covers the current atmosphere config tests, standalone
atmosphere sky PNG smokes, planet celestial tests, and planet sky-facing PNG
smokes. It intentionally excludes planet local-detail, terrain-field, and broad
non-sky visual smokes.


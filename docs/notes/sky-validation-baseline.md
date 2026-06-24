# Sky Validation Baseline

Captured on 2026-06-21 from the `sky-rendering` worktree after removing the
legacy planet `SkyFrame` backend. Updated on 2026-06-22 after adding focused
planet sky smoke views for sun-facing dawn, antisun dawn, and moon occlusion.

Tested source commit before this note commit:

```text
bdef1495 planet: use unified atmosphere as the only sky backend
```

## Baseline Commands

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_core_tests cubey_project_atmosphere_tests cubey_project_planet_tests cubey_project_planet_celestial_tests cubey_project_planet cubey_png_stats
ctest --preset dev -R '^(cubey_core_tests|atmosphere_config_tests|planet_frame_tests|planet_celestial_tests)$' --output-on-failure
ctest --preset dev -L sky --output-on-failure
git diff --check
```

## Results

- `cmake --preset dev`: passed through build-time regeneration.
- Baseline build targets: passed.
- Focused unit tests: passed, 4/4.
- `ctest --preset dev -L sky`: passed, 43/43.
- Vulkan-dependent PNG smoke tests ran and passed; no sky tests skipped.
- `git diff --check`: passed.

## Notes

The `sky` CTest label covers the current atmosphere config tests, standalone
atmosphere sky PNG smokes, planet celestial tests, and planet sky-facing PNG
smokes. The planet sky-facing subset includes baseline dawn/day/night, orbit
lit/terminator, daytime moon, sun-facing dawn, antisun dawn, and moon occlusion
captures. It intentionally excludes planet local-detail, terrain-field, and
broad non-sky visual smokes.

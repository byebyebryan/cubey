# Paused Planet study

Status: paused on 2026-10-15. Planet did not reach the intended visual result
and fell behind the active environments. Its orbital reboot is retained for
research and comparison, outside the active portfolio, normal build, and
showcase queue. Reopening starts with a new visual target and an assessment
of the current shared foundation.

`planet` renders a deterministic Earth-like globe for whole-disk and high-orbit
views. It has no terrain streaming, surface rendering, or ocean/coast integration.

The surface is a cached six-face direction-domain RGBA product:

- `R`: normalized land elevation
- `G`: land mask
- `B`: polar and highland ice mask
- `A`: macro roughness

The fixed sphere samples those fields with direct lighting, ocean glint, a
soft terminator, a restrained cloud veil, and the shared atmosphere/night-sky
background. It has no displacement, cube-sphere patch selection, or LOD.

## Explicit build and use

`CUBEY_BUILD_PLANET_STUDIES` defaults to `OFF`. Build the study deliberately:

```sh
cmake --preset dev-planet-studies
cmake --build --preset dev-planet-studies --target cubey_study_planet cubey_study_planet_tests -j 4
ctest --preset dev-planet-studies
```

The study still uses the shared Vulkan host, shaders, and procedural cache.
Its C++ namespaces, CLI, cache recipes, and output names retain their original
identities for comparison continuity. Automated checks establish reproducibility
and basic execution; they do not establish visual acceptance.

```sh
./build/dev-planet-studies/studies/planet/planet
./build/dev-planet-studies/studies/planet/planet --planet-view terminator --planet-disk-coverage 0.48
./build/dev-planet-studies/studies/planet/planet --headless --planet-view crescent \
  --planet-terrain-seed 9012 --width 1600 --height 900 --output /tmp/planet.png
```

`--planet-view` accepts `lit`, `terminator`, `crescent`, and `night`.
`--planet-surface-quality draft` selects a 256-pixel cubemap; `standard` is
512 pixels. `--planet-terrain-seed` controls the deterministic source, and
`--planet-disk-coverage` keeps the visible disk in the V1 envelope of
15-70 percent of viewport height. `--planet-camera-mode surface` is rejected
deliberately.

Use `--debug-view land`, `elevation`, `ice`, `roughness`, or `albedo` to inspect
source channels without material or lighting.

## Review

```sh
studies/planet/tools/capture_orbital_review.sh outputs/planet/new-review
```

The script requires a built study executable; it never configures or builds it.
It writes the lit, phase, and source-field review pack to the supplied directory
(default `outputs/planet/orbital-v1`), without deleting the directory. Choose a
new output directory to preserve a previous review. The procedural cache remains
local at `cache/procedural/v1` and is not committed.

The full boundary, reference provenance, and explicitly deferred scope live in
[the retained design](design.md). The [former showcase proposal](showcase-plan.md)
is historical study material, not active editorial work.
The pre-reboot `projects/planet_legacy` implementation is preserved as
historical design evidence and in Git history; it is not an active project
dependency. See the [retirement snapshot](../../docs/archive/retired-projects.md).
The [relocation map](../relocations.json) resolves former source and tool names;
existing outputs, caches, and archived surface experiments remain in place.

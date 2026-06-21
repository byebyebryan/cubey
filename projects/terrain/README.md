# Terrain

`projects/terrain` is the rebooted terrain workbench. It starts as a local
CPU/reference terrain product generator, not as a direct continuation of
`terrain_lab_legacy`, not as a coastal/ocean demo, and not as a planet renderer.

The first goal is a deterministic product contract that downstream systems can
inspect and eventually consume:

- coherent source fields;
- terrain feature and process fields;
- material and vegetation-potential hints;
- summaries and debug exports.

The current first slice is a temperate mountain river catchment over a local
kilometer-scale grid. Rendering, ocean integration, planet streaming, foliage
rendering, and physically complete erosion are deferred until the product fields
are credible.

See [Terrain reboot direction](../../docs/architecture/terrain-reboot.md) for
the current design checkpoint.

## Commands

```sh
cmake --build --preset dev --target cubey_project_terrain cubey_project_terrain_tests
ctest --preset dev -R terrain --output-on-failure

./build/dev/projects/terrain/terrain
```

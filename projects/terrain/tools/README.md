# Terrain preparation tools

Supported external-source and saved-landscape preparation, separate from the
[dated study harnesses](../../../studies/terrain/landscapes/README.md).

- [run_terrain_diffusion_bake.sh](run_terrain_diffusion_bake.sh) /
  [terrain_diffusion_bake.py](terrain_diffusion_bake.py): explicit pinned external
  generator workflow; generation can need models, GPU and an isolated environment.
- [terrain_world_coverage.py](terrain_world_coverage.py): hierarchical coverage.
- [terrain_site_package.py](terrain_site_package.py): source-bound site packages.
- [terrain_grid_sampling.py](terrain_grid_sampling.py): world-coordinate sampling.
- [prepare_terrain_landscapes.py](prepare_terrain_landscapes.py): explicit catalogue
  admission/preparation, not a solver or automatic startup action.
- `test_*.py`: regression contracts for these preparation APIs.

Start with the [Terrain quickstart](../README.md). Generated inputs belong under
ignored `cache/terrain`; review/performance outputs under `outputs/terrain`.
These are fixed-focus source/backdrop tools, not a streaming terrain engine.

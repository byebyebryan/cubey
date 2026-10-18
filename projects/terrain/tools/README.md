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
- [terrain_bundle.py](terrain_bundle.py): shared recoverable publication for all
  seven Terrain Diffusion generation modes.
- `test_*.py`: regression contracts for these preparation APIs.

Start with the [Terrain quickstart](../README.md). Generated inputs belong under
ignored `cache/terrain`; review/performance outputs under `outputs/terrain`.
These are fixed-focus source/backdrop tools, not a streaming terrain engine.

## Generated bundle replacement

The Python 3.11+ POSIX generator tools finish writing and validating staging
before replacing a destination. Publication serializes cooperating writers
with a persistent `.<name>.publish.lock` file. Keep that file: its existence
does not mean a producer is running, and removing it can split the lock.
Existing reuse and explicit `--force` regeneration choices remain unchanged.

The previous directory is renamed to a unique `.<name>.previous.*` sibling
before installation. An install failure restores it where possible. Publication
failures retain the complete `<name>.tmp.*` staging root and report recovery
locations; a rollback failure also reports the retained previous directory.
Generation/validation failures discard only partial staging. Cleanup failures
after successful installation report a warning and keep the installed bundle.

After a hard termination, a previous directory can remain beside staging while
the destination is absent. Stop any producer for that destination, inspect and
validate the retained directories, then rename the selected previous directory
back to the missing destination. Later attempts never delete an earlier recovery
directory. Source presets also retain their existing `.generating` marker on
hard termination; verify that producer has stopped before clearing its marker.
Replacement has a short interval with no destination: close readers before
regeneration and reopen after success. This does not guarantee an atomic
exchange for concurrent readers or durability through storage/power failure.

Run the publication fault, rollback, contention and process-kill tests without
models, NumPy, CUDA or generated assets:

```sh
python3 -B projects/terrain/tools/test_terrain_bundle.py
```

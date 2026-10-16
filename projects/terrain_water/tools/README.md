# Terrain Water tools

Supported setup/host code, separate from [retained studies](../../../studies/fluid_25d/README.md).

- [prepare_case.py](prepare_case.py): explicitly serialize a named terrain/grid
  case in the optional SynxFlow IO environment; never dispatches the solver.
- [native_input.py](native_input.py): released IO serialization and native-ID/
  boundary/input audits extracted unchanged from the retained runoff study.
- [runtime.py](runtime.py): stdlib-only hashing, exclusive output and owned-process
  cleanup. No study, NumPy or CUDA import during normal launcher startup.
- [terrain_water_sites.py](terrain_water_sites.py) and companion JSON catalogues:
  source-bound curated/world site admission, not an automatic survey/bake.

Use the [canonical launcher](../run_terrain_rain_demo.py), [setup](../demo_setup_v1.md)
and [dependency handoff](../external_synxflow/README.md). Source worker/build/lock
files now belong to `projects/terrain_water/external_synxflow`. The existing
qualified private binary remains at its original ignored build path to preserve
its absolute dependency/RPATH identity; `--extension` can select the same hash.
No binary, optional dataset, model cache or recorded state was relocated.

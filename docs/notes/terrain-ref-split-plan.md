# Terrain Ref Split Plan

Date: 2026-07-06

The current `projects/terrain` project has become a useful workbench, but it is
not a clean base for the next visual terrain pass. It carries river and mountain
process experiments, scalar debug exports, phase profiling, product fields, and
reference captures. Those are valuable as evidence, but they keep pulling new
terrain rendering work back into old assumptions.

The next split should make the roles explicit:

- `projects/terrain_workbench_legacy`: preserve the current product/debug
  workbench, including river, mountain, scalar export, process-field, and
  capture history. Keep it buildable for comparison and regression checks, but
  stop treating it as the active terrain reboot path.
- `studies/terrain/reference`: a clean renderer-focused reference lane. Start with
  the TerrainEngine-inspired runtime height/material model, then use this lane
  for TerrainEngine rendering work and selected ShaderToy terrain references.
- future `projects/terrain`: reserve the canonical name for a later production
  terrain project after the reference lane proves the source/render/runtime
  shape.

The split should avoid another broad rewrite. The legacy workbench can keep its
existing internal namespace and most file names. The new `terrain_ref` project
should copy only the minimum clean pieces needed for a renderer-backed
TerrainEngine reference: runtime config, deterministic height sampling,
clipmap review mesh, shaders, headless/windowed host wiring, and focused tests.

Do not move the river/mountain process pipeline into `terrain_ref`. If those
ideas become useful again, reintroduce them later as explicit, reviewed
operators rather than as inherited default terrain behavior.

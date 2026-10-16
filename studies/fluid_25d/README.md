# Fluid 2.5D / Terrain Water studies

The earlier river-to-rain investigation is retained here, not in an active
application folder. Start current work with [builtin Fluid 2.5D](../../projects/fluid/fluid_25d/README.md)
or [Terrain Water](../../projects/terrain_water/README.md). Normal Terrain Water
startup does not import these study harnesses.

## Read by question

- [Decision/evidence map](notes/development_map.md): why authored rivers, generated
  terrain, external reuse and shared rendering evolved separately.
- [Bank/shoreline studies](notes/shoreline_boundary_study_v1.md),
  [Scenic lineage](notes/scenic_rendering_v1.md) and [terrain presentation](notes/terrain_presentation_v1.md).
- [Offscreen performance V2](notes/offscreen_perf_v2.md); [V1](notes/live_perf_terrain_v1.md)
  is historical, not native desktop FPS evidence.
- [Sites](notes/site_expansion_v1.md), [prepared-world water](notes/world_water_v1.md),
  [rain response](notes/rain_response_v1.md) and [managed controls](notes/terrain_demo_ux_v1.md).
- [Published film reproduction](notes/presentation_v1.md): retained production tooling;
  no new film or changed provenance is implied by this relocation.
- [Frozen README and presentation history](harness/README.md).
- [CPU reference adapter](adapters/fluid_25d_external_reference_baseline.cpp):
  source-only rainfall comparison around the production finite-volume oracle.
  It has no app or CMake target and does not implement another solver. Explicit
  compilation needs the current oracle source and its configured include paths.

## Harnesses and limits

[harness](harness/README.md) contains the retained runners, fixtures, report tools
and their regression tests. They remain addressable by explicit paths and CTest;
results stay under ignored `outputs/fluid`. Many need dated optional recordings,
private runtimes or third-party packages. Do not run every study as a batch or
interpret an old “next step” as the current backlog.

Existing registered helper tests are retained; this is not a promise that every
historical capture phase can be rerun against today's source tree. Sealed studies
may require their original revision, in addition to their exact optional inputs.
Their dated source/algorithm assumptions are not rewritten to manufacture parity.

Current setup, site catalogues, process management and input serialization belong
in [Terrain Water tools](../../projects/terrain_water/tools/README.md), with the
separately licensed [SynxFlow handoff](../../projects/terrain_water/external_synxflow/README.md).
The [relocation map](../relocations.json) locates former entry paths. Two frozen
narrative bodies and published media recipes remain unchanged; their old paths
are capture-time provenance, not newly validated commands.

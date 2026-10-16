# Fluid 2.5D — decision and evidence map

Project split: the featured external terrain experience is now
[Terrain Water](../../terrain_water/README.md). Fluid 2.5D retains the builtin
shallow-water experiments. Decisions below explain their common lineage, not a
claim that the builtin solver reproduces the featured external result.

The project grew through several useful but different experiments. This map
explains the pivots; it is not a new backlog. The [quickstart](README.md) and
[current reference](current_reference.md) describe what runs now. Historical
documents retain the build, assumptions and acceptance status of their own run.

## Current verdict

Keep the macro rain/runoff/collection demo on unmodified Terrain Diffusion
mountain terrain, using a separate SynxFlow worker and Cubey's Vulkan renderer.
Keep builtin solvers as explicit simpler diagnostics. Invest in readable water,
terrain and presentation rather than porting solver math or finding/generating a
perfect artificial river. The published Joining valleys film is a recorded
showcase selection, not a change to the interactive Mountain default.

## How we arrived here

| Question / pivot | Outcome now | Where the evidence lives |
| --- | --- | --- |
| Authored river, source/outlet, quiver, dye and deeper channels | Useful retained fixtures, but not the featured demo: a steady artificial river did not communicate the desired dynamic terrain flow. | [River / source-outlet history](README_HISTORY.md#river-v0), [transport study](../../../docs/notes/fluid-25d-transport-readability-v1.md). |
| Which generated site makes water interesting? | Pronounced hills and sustained supply made downhill flow legible. Use existing terrain; do not normalize/carve it to force a desired river. | [Terrain audition](../../../docs/notes/fluid-25d-terrain-water-audition-v2.md), [site expansion](site_expansion_v1.md). |
| Can builtin shallow water deliver the stronger rain/runoff behavior? | The retained finite-volume/dynamics studies exposed limits in that implementation/setup. They do not establish a fundamental impossibility for all shallow-water methods. | [Numerical contract](README_HISTORY.md#numerical-contract), [dynamics study](../../../docs/notes/fluid-25d-dynamics-study-v1.md). |
| Reimplement/port a reference solver, or reuse it externally? | Reuse separately prepared SynxFlow. Keep versioned field/control contracts, CPU-staged transport and explicit alternatives; no in-process CUDA/interop or solver port. | [Service/parity evidence](README_HISTORY.md#optional-gpl-external-service-worker), [current backend boundaries](current_reference.md#solver-contract-and-ownership). |
| Why do streams look like glossy ribbons? | Shared lighting, depth-limited absorption, rain/flow roughness and subtle moving whitewater improve the macro reading. Borrowed Water 3D/Ocean principles, not their full simulations. | [Scenic lineage](scenic_rendering_v1.md), [retained lake/rainfall experiments](README_HISTORY.md#dry-start-rain-formed-collections). |
| How do we hide cell-scale banks? | Paired B-spline ground/water and continuous wet-ground coverage are the baseline. Keep reference and recorded marching-squares comparisons; noise remains optional, not promoted. | [Bank-edge refinements](../../../docs/notes/fluid25d-bank-edge-v1.md), [offline boundary study](shoreline_boundary_study_v1.md), [three-mode comparison](README_HISTORY.md#recorded-three-mode-bank-comparison). |
| Can the solver and renderer coexist? Which grid? | Measured 1k / 16 m with a 60 Hz viewer cap is the preferred live setup. 2k is much slower at the tested pace. Corrected offscreen evidence supersedes V1 Xvfb frame interpretations. | [Corrected performance V2](offscreen_perf_v2.md), [historical V1](live_perf_terrain_v1.md). |
| Can water enrich terrain beyond this demo? | Yes: solver-neutral captured rain-on water has an opt-in shared Terrain consumer. Terrain owns wider backdrop/LOD; Fluid returns to the detailed patch only. | [Terrain consumer](../../terrain/README.md), [prepared-world water](world_water_v1.md), [patch-only scope](patch_only_v1.md). |
| Why rain off or a finite timeline? | The qualified Mountain guide demonstrates response and delayed collection, then ends guidance without stopping the solver. Manual supports sustained rain. Live mode is continuous; the prepared 20-hour horizon is for explicit finite diagnostics. | [Rain response](rain_response_v1.md), [managed UI/lifecycle](terrain_demo_ux_v1.md). |
| Which film represents the project? | Owner-selected Joining valleys: eight seconds, 15° view, orbit, dry-to-streams-to-lake and sunset. Published inline in the main gallery. Old 36-second Mountain rain-off film is a separate legacy study. | [Current production/publication guide](presentation_v1.md), [presentation history](presentation_history.md). |

## Status words and authority

- **Default** needs context: prepared launcher, bare application, or selected
  film. They intentionally differ; see the current reference's entry table.
- **Retained option** means available for comparison, not pending promotion.
  Examples: bank noise, Readable, reference banks, recorded marching squares,
  builtin solvers and the explicit prepared-world collection.
- **Rejected / unpromoted study** keeps its actual result. A failed VTK smoothing
  trial or native-solver rain case does not become accepted because a later,
  different approach works.
- **Validated** must name its scope: CPU contracts, exact GPU upload, offscreen
  performance, browser media review, native GUI behavior and owner visual
  acceptance are different gates. Old passing counts describe old checkpoints.
- **Pending** inside a sealed report or immutable recipe is capture-time history.
  Current publication is recorded separately in the
  [receipt](../../../docs/media/showcase/fluid-25d-publication.json).

## Preserved history, not current instructions

[README_HISTORY.md](README_HISTORY.md) preserves the prior long README, including
old section anchors and reproduction commands. [Presentation history](presentation_history.md)
preserves the former mixed V1/V2 narrative and completed promotion checklist.
Their dated paths refer to optional local evidence, not fresh-checkout assets.

The older terrain-water integration plans and alignment failure remain in
[the notes index](../../../docs/notes/README.md). The accepted paired-surface
consumer is documented in Terrain; those plans are not a second active project.
Cloud-enabled elevated-view studies also remain historical: the current Fluid
view uses [clear sky/day-night without clouds](shared_environment_v1.md).

New work should begin with a visible problem in the current demo and a bounded
comparison, not a historical document's unqualified “next step.” This pass only
reconciles documentation; it does not reopen solver, hydrology or terrain scope.

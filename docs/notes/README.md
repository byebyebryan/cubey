# Notes

Current behavior belongs in the [project guides](../README.md#project-docs),
[architecture](../architecture/README.md) and [roadmap](../roadmap.md). Notes
record operating workflows, decisions and point-in-time evidence; an old
“current”, “next” or “plan” is not an active backlog or a present-build promise.
Promote stable contracts into their owning guide when behavior changes.

For Terrain Water, begin with the [current behavior reference](../../projects/terrain_water/current_reference.md)
and [decision/evidence map](../../studies/fluid_25d/notes/development_map.md).
The [study index](../../studies/README.md) owns retained harnesses and their
optional inputs; the [archive](../archive/README.md) owns retired applications
and completed foundation phases.

<a id="entries"></a>

## Operating guides

- [Performance Profiling Notes](performance-profiling.md): repeatable host/GPU profiling workflow; compare matched runs, not GUI FPS.
- [Ocean Visual Captures](ocean-visual-captures.md): canonical capture matrix for the current Ocean renderer.
- [glTF Staged-Loading Profile](gltf-loading-profile.md): pinned corpus and release-profile workflow; recorded timings belong to their measured source.
- [Procedural Shader Parity](procedural-shader-parity.md): CPU/GLSL parity contracts for mechanical helper migrations.

## Decisions and integration checkpoints

These records explain why the current contracts exist. Their measurements,
manual-acceptance limits and historical sections retain their original scope;
the architecture and project guides remain current authority.

- [SDF implementation evidence](sdf-implementation-evidence.md): native field, traversal and batch checkpoints for the boxes-only port.
- [SDF authored content evidence](sdf-authored-content-evidence.md): the first survey robot, grouped geometry/material contracts and native visual/performance qualification.
- [SDF authored history evidence](sdf-authored-history-evidence.md): edit-by-edit robot construction, cut previews, step controls and native state/replay qualification.
- [SDF robot detail evidence](sdf-robot-detail-evidence.md): the mechanical detail variant, matched visual comparisons and atlas-sampling precision correction.
- [SDF robot field-kit evidence](sdf-robot-fieldkit-evidence.md): further authored assemblies, torus/cone/smooth-subtraction contracts and next vocabulary decisions.
- [SDF robot joint review](sdf-robot-joint-review.md): pose-derived hinge frames, continuous leg centerlines and independent modeling-intent checks after user review.

- [glTF Viewer V1 Closure](gltf-viewer-v1-closure.md): closed single-asset milestone, accepted limits and reopen gates.
- [Dynamic Environment Probe Coherence](dynamic-environment-probe-coherence.md): coherent full-cubemap publication and consumer handoff.
- [Surface Ocean V1](ocean-surface-v1.md): accepted local-water product boundary.
- [Ocean Cloud Lighting V1](ocean-cloud-lighting-v1.md): explicit cloud lighting/reflection products and surface-only limits.
- [Ocean Surface Foundation Promotion](ocean-surface-foundation-promotion.md): shared runtime promotion; performance claims retain their recorded qualification scope.
- [Cloud Foundation Integration Checkpoint](cloud-foundation-integration-checkpoint.md): shared surface-cloud ownership; old Planet/reference integrations remain historical.
- [Sky and Celestial Current State](sky-celestial-current-state.md): shared celestial checkpoint; Planet-specific sections describe the retired integration.
- [Procedural Star Field V2](star-field-v2.md): analytic star field and diagnostic contract.
- [Terrain Project Map](terrain-project-map.md): current product versus frozen/paused lanes.
- [Terrain Product Promotion](terrain-product-promotion.md): far-backdrop promotion and ownership boundary.
- [Terrain And Foundation Closure](terrain-foundation-closure-2026-07-22.md): source/product caches and generated-atlas adoption.
- [Terrain Backdrop Foundation Promotion](terrain-backdrop-foundation-promotion.md): second-consumer promotion and acceptance gates.
- [Terrain Source Preset Contract](terrain-source-preset-contract.md): explicit source recipes, availability states and cache boundary.
- [Terrain Raster Backdrop V1](terrain-raster-backdrop-v1.md): external-raster product and natural placement checkpoint.
- [Terrain Rendering Envelope V1](terrain-rendering-envelope-v1.md): far-backdrop geometry/detail limits.
- [Terrain Visual Fidelity Strategy](terrain-visual-fidelity-strategy.md): bounded evidence required before reopening the accepted backdrop.

## Exploration candidates

These records retain proposed experiments and their scope. They do not imply
an implemented project or a new runtime dependency.

- [projectR architecture and source review](projectr-architecture-review.md):
  edit representation, hierarchy and brick construction, renderer generations,
  interaction, persistence and source-level correctness findings.
- [projectR development history](projectr-development-history.md): the original
  commit-message journey through meshing, brick filtering, hybrid tracing,
  incremental updates and the renderer fork.
- [SDF / projectR port exploration](sdf-projectr-port-exploration.md): a bounded
  procedural rendering demo first, with learned-content and authoring
  experiments retained for later selection.
- [SDF project implementation plan](sdf-projectr-port-plan.md): proposed renderer
  boundary, complete recipe model, native milestones, resource limits and
  visual/performance acceptance for the first procedural demo.
- [SDF authored content plan](sdf-authored-content-plan.md): a first stylized
  robot, early native blockout review, grouped composition and plain materials;
  implemented through native presentation, with user visual review pending.

## Historical research and study records

Completed, rejected, superseded and optional experiments are grouped below.
Read each record's verdict and source/input boundary before reproducing it.
In particular, the cloud pre-merge and terrain process plans describe removed
implementations; they do not reopen frozen reference studies. Current cloud
work uses Atmosphere; current terrain work uses the promoted Terrain product
or a separately chosen new product.

### Sky and cloud research and superseded capture plans

- [Atmosphere Rendering Research](atmosphere-rendering-research.md)
- [Celestial Rendering Research](celestial-rendering-research.md)
- [Cloud Pre-Merge Checkpoint](cloud-pre-merge-checkpoint.md)
- [Cloud And Weather Rendering Research](cloud-weather-rendering-research.md)
- [Procedural Milky Way V2 Research](milky-way-v2-research.md)
- [Geometry Moon Migration](sky-geometry-moon-migration.md)
- [Geometry Moon Migration Captures](sky-moon-geo-migration-captures.md)
- [Moon Surface Detail Captures](sky-moon-surface-detail-captures.md)
- [Moon Surface Detail Plan](sky-moon-surface-detail-plan.md)
- [Sky Validation Baseline](sky-validation-baseline.md)
- [Sky Visual Baseline Review](sky-visual-baseline-review.md)
- [Sky Visual Iteration Plan](sky-visual-iteration-plan.md)
- [Sky Visual Pass 001 Post-Sun Review](sky-visual-pass-001-post-sun-review.md)
- [Sky Visual Pass 001 Review](sky-visual-pass-001-review.md)

### Ocean and liquid performance or presentation studies

- [Ocean Cloud Reflection Strategy](ocean-cloud-reflection-strategy.md)
- [Ocean Performance Notes](ocean-performance.md)
- [Scene Ocean Performance Study](ocean-scene-performance-study.md)
- [Water 3D Profiling Notes](water-3d-profiling.md)

### Builtin Fluid and terrain-water study lineage

- [Fluid 2.5D dynamics study V1](fluid-25d-dynamics-study-v1.md)
- [Fluid 2.5D sustained-headwaters bankfull study V1](fluid-25d-headwaters-bankfull-v1.md)
- [Fluid 2.5D headwaters resolution and speed study V1](fluid-25d-headwaters-resolution-speed-v1.md)
- [Fluid 2.5D headwaters supply and speed study V1](fluid-25d-headwaters-supply-speed-v1.md)
- [Hillside conservation and transport V3](fluid-25d-hillside-conservation-transport-v3.md)
- [Macro hillside-flow study V1](fluid-25d-hillside-flow-study-v1.md)
- [Hillside motion and readability V4](fluid-25d-hillside-motion-readability-v4.md)
- [Hillside Rain V2: camera bounds and downhill transport diagnosis](fluid-25d-hillside-rain-v2-transport-diagnosis.md)
- [Hillside Rain V3: bounded CPU transport correction study](fluid-25d-hillside-rain-v3-cpu-transport-study.md)
- [Hillside Rain V4: shared-geometry CPU study](fluid-25d-hillside-rain-v4-geometry-study.md)
- [Sustained hillside flow V2](fluid-25d-hillside-sustained-flow-v2.md)
- [Native Terrain Diffusion flow-site survey V1 — 2026-09-25](fluid-25d-native-flow-site-survey-v1.md)
- [Native Terrain Diffusion sustained-flow preflight V1 — 2026-09-25](fluid-25d-native-sustained-flow-preflight-v1.md)
- [Fluid 2.5D natural-reach study V1 — 2026-09-23](fluid-25d-natural-reach-study-v1.md)
- [Fluid 2.5D rain catchment study V1 (2026-09-24)](fluid-25d-rain-catchment-study-v1.md)
- [Fluid 2.5D rain-pool terrain screen V1 (2026-09-24)](fluid-25d-rain-pool-terrain-screen-v1.md)
- [Fluid 2.5D River V0 checkpoint](fluid-25d-river-v0-checkpoint.md)
- [Fluid 2.5D sustained-headwaters control V1](fluid-25d-sustained-headwaters-v1.md)
- [Fluid 2.5D Terrain Diffusion hydrology read V1 — 2026-09-24](fluid-25d-terrain-hydro-read-v1.md)
- [Fluid 2.5D terrain-site water study V1 — 2026-09-23](fluid-25d-terrain-site-water-v1.md)
- [Fluid 2.5D Terrain-Water Audition V1 — 2026-09-15](fluid-25d-terrain-water-audition-v1.md)
- [Fluid 2.5D Terrain-Water Audition V2 — 2026-09-15](fluid-25d-terrain-water-audition-v2.md)
- [Fluid 2.5D Transport Readability V1](fluid-25d-transport-readability-v1.md)
- [Fluid 2.5D quiet water-edge spike](fluid25d-bank-edge-v1.md)
- [Fluid 2.5D opt-in cascade cues V1](fluid25d-cascade-cues-v1.md)
- [Fluid 2.5D climate-informed procedural surface V1](fluid25d-climate-surface-v1.md)
- [Fluid 2.5D bare macro-terrain and rainfall V4](fluid25d-macro-terrain-v4.md)
- [Optional shallow-water treatments](fluid25d-shallow-water-options-v1.md)
- [Fluid 2.5D opt-in steep-flow presentation spike](fluid25d-steep-flow-v1.md)
- [Fluid 2.5D terrain material study V3](fluid25d-terrain-material-v3.md)
- [Fluid 2.5D water-film presentation V5](fluid25d-water-film-v5.md)

### Terrain research, completed experiments and superseded plans

- [Backdrop Surface Placement](backdrop-surface-placement.md)
- [Terrain Anisotropic Mountain Profile Plan](terrain-anisotropic-mountain-profile-plan.md)
- [Terrain Backdrop Foreground Clearance](terrain-backdrop-foreground-clearance.md)
- [Terrain Backdrop Presentation Study](terrain-backdrop-presentation.md)
- [Terrain Cached Backdrop Pivot](terrain-cached-backdrop-pivot.md)
- [Terrain Cached Backdrop V1 Review](terrain-cached-backdrop-v1-review.md)
- [Terrain Cached Radial Integration](terrain-cached-radial-integration.md)
- [Terrain captured water alignment result](terrain-captured-water-alignment-v1.md)
- [Captured-water landscape context V1](terrain-captured-water-context-v1-plan.md)
- [Terrain captured water integration plan](terrain-captured-water-v1-plan.md)
- [Terrain captured water paired surface plan](terrain-captured-water-v2-plan.md)
- [Terrain Climate Calibration V1](terrain-climate-calibration-v1.md)
- [Terrain Climate Surface Model Research](terrain-climate-surface-model-research.md)
- [Terrain Coherent Height Rule](terrain-coherent-height-rule.md)
- [Rejected Terrain Daylight-Form Study](terrain-daylight-form-v1.md)
- [Terrain Diffusion Desert And Canyon Study](terrain-diffusion-desert-canyon-study.md)
- [Terrain Directional Backdrop Study](terrain-directional-backdrop-study.md)
- [TerrainEngine Reference Port Plan](terrain-engine-reference-port-plan.md)
- [Terrain Erosion Filter Generalization](terrain-erosion-filter-generalization.md)
- [Terrain External Generator Bakeoff Review](terrain-external-generator-bakeoff-review.md)
- [Terrain External Generator Bakeoff](terrain-external-generator-bakeoff.md)
- [Terrain Far-Field V1](terrain-far-field-v1.md)
- [Terrain Graph-First River Plan](terrain-graph-first-river-plan.md)
- [Terrain Landscape Evolution V1 Review](terrain-landscape-evolution-v1-review.md)
- [Terrain Landscape Evolution V1](terrain-landscape-evolution-v1.md)
- [Terrain Landscape Variations V1](terrain-landscape-variations-v1.md)
- [Terrain Lighting And Material V1](terrain-lighting-material-v1.md)
- [Terrain Material V2](terrain-material-v2.md)
- [Terrain Midground Correction V4](terrain-midground-correction-v4.md)
- [Terrain Midground Detail V3](terrain-midground-detail-v3.md)
- [Terrain Mountain Driver Plan](terrain-mountain-driver-plan.md)
- [Terrain Mountain Gully Diagnostic Plan](terrain-mountain-gully-diagnostic-plan.md)
- [Terrain Mountain Hierarchy Plan](terrain-mountain-hierarchy-plan.md)
- [Terrain Mountain Macro Shape Plan](terrain-mountain-macro-shape-plan.md)
- [Terrain Mountain Peak Readability Plan](terrain-mountain-peak-readability-plan.md)
- [Terrain Mountain Profile Correction Plan](terrain-mountain-profile-correction-plan.md)
- [Terrain Mountain Source Process Loop Plan](terrain-mountain-source-process-loop-plan.md)
- [Terrain Mountain Thermal Talus Plan](terrain-mountain-thermal-talus-plan.md)
- [Terrain Mountain Visual Source Pivot](terrain-mountain-visual-source-pivot-plan.md)
- [Mountains-Guided Terrain Source Correction](terrain-mountains-guided-source-correction.md)
- [Terrain Natural Raster Staging](terrain-natural-raster-staging.md)
- [Terrain Orbit Stage Plan](terrain-orbit-stage-plan.md)
- [Terrain Peak-First Mountain Skeleton Plan](terrain-peak-first-mountain-skeleton-plan.md)
- [Terrain Placement Controls](terrain-placement-controls.md)
- [Terrain Process Roadmap](terrain-process-roadmap.md)
- [Terrain Quality Tile Field](terrain-quality-tile-field.md)
- [Terrain Radial Backdrop Macro Baseline](terrain-radial-backdrop-macro-baseline.md)
- [Terrain Radial Backdrop Product V1](terrain-radial-backdrop-product-v1.md)
- [Terrain Radial Fidelity Ablation](terrain-radial-fidelity-ablation.md)
- [Terrain Reboot Current Captures](terrain-reboot-current-captures.md)
- [Terrain Ref Closure](terrain-ref-closure.md)
- [Terrain Ref Presentation Port Plan](terrain-ref-presentation-port-plan.md)
- [Terrain Ref ShaderToy Heightfield Plan](terrain-ref-shadertoy-heightfield-plan.md)
- [Terrain Ref Split Plan](terrain-ref-split-plan.md)
- [Terrain Renderer Preview Plan](terrain-renderer-preview-plan.md)
- [Terrain Rendering Acceptance V1](terrain-rendering-acceptance-v1.md)
- [Terrain Rendering Quality Reset](terrain-rendering-quality-reset.md)
- [Terrain Rendering Refinement](terrain-rendering-refinement.md)
- [Terrain Resolution And Bandwidth Checkpoint](terrain-resolution-bandwidth-checkpoint.md)
- [Terrain Resolution And Bandwidth Prototype](terrain-resolution-bandwidth-prototype.md)
- [Terrain Resolution Scene-Readiness Plan](terrain-resolution-scene-readiness-plan.md)
- [Terrain River Degrid And Stress Pruning Plan](terrain-river-degrid-stress-pruning-plan.md)
- [Terrain River Graph Routing Attempt](terrain-river-graph-routing-attempt.md)
- [Terrain River Stream-Order Corridor Plan](terrain-river-stream-order-corridor-plan.md)
- [Terrain River Terrain Coupling Plan](terrain-river-terrain-coupling-plan.md)
- [Terrain Routing Repair Plan](terrain-routing-repair-plan.md)
- [Terrain ShaderToy Biome Reference Map](terrain-shadertoy-biome-reference-map.md)
- [Terrain ShaderToy Erosion Filter Reference](terrain-shadertoy-erosion-filter-plan.md)
- [ShaderToy Mountains Fidelity Study](terrain-shadertoy-mountains-fidelity.md)
- [Terrain ShaderToy Operator Extraction](terrain-shadertoy-operator-extraction.md)
- [ShaderToy Terrain Source-Shape Studies](terrain-shadertoy-source-shape-studies.md)
- [Terrain Source Bakeoff V1](terrain-source-bakeoff-v1.md)
- [Terrain Source Model Study](terrain-source-model-study.md)
- [Terrain Source v2.1 Refinement Plan](terrain-source-v2-1-refinement-plan.md)
- [Terrain Source V3 Far-Field Hierarchy](terrain-source-v3-hierarchy-plan.md)
- [Terrain Stress Basin Network Reach Plan](terrain-stress-basin-network-reach-plan.md)
- [Terrain Stress Branch Distinctness Plan](terrain-stress-branch-distinctness-plan.md)
- [Terrain Stress Promotion Organic Cleanup Plan](terrain-stress-promotion-organic-cleanup-plan.md)
- [Terrain Stress Trunk Hierarchy Plan](terrain-stress-trunk-hierarchy-plan.md)
- [Terrain Surface Semantics Study](terrain-surface-semantics-study.md)
- [Terrain V1 Baseline Review](terrain-v1-baseline.md)
- [Terrain V1 Reboot Decision](terrain-v1-reboot.md)
- [Terrain V1 Runtime Checkpoint](terrain-v1-runtime-checkpoint.md)
- [Terrain V1 Visual Closure](terrain-visual-closure-v1.md)
- [Terrain Water Bodies Scope](terrain-water-bodies-scope.md)

### Procedural research and working history

- [Procedural Consumer Inventory](procedural-consumer-inventory.md)
- [Procedural Terrain Reference Review](procedural-terrain-reference-review.md)
- [Working Notes](working-notes.md)

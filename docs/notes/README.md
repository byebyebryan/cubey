# Notes

Notes are living scratch context: progress logs, gotchas, rough ideas, and
temporary investigation records that are useful but not polished enough to be
current design guidance.

When a decision stabilizes, promote it into the current docs under `docs/` or
the detailed foundation notes under `docs/architecture/`.

## Entries

- [Working notes](working-notes.md): broad implementation history and lessons
  learned.
- [Atmosphere rendering research](atmosphere-rendering-research.md): early
  notes on clear-sky scattering approaches and the first `projects/atmosphere`
  implementation direction.
- [Celestial rendering research](celestial-rendering-research.md): historical
  pivot note from atmosphere-owned celestial disks toward shared visible body
  geometry with atmosphere as a consumer.
- [Planet gap closure checkpoint](planet-gap-closure.md): current planet
  project state, missing foundation gaps, and the intended split-commit batch.
- [Planet surface quality pass](planet-surface-quality.md): procedural terrain,
  material, LOD, and capture goals from the planet surface batch.
- [Planet terrain field v2](planet-terrain-field-v2.md): current terrain field
  sample/tile vocabulary for later ocean, biome, cache, and streaming work.
- [Planet atmosphere v1](planet-atmosphere-v1.md): immediate planet-local
  scattering, transmittance, and aerial-perspective direction.
- [Sky and celestial current state](sky-celestial-current-state.md): accepted
  Moon, Milky Way, and Star Field V2 ownership, render paths, and deferred
  feature boundaries.
- [Procedural star field v2](star-field-v2.md): accepted shared analytic star
  field, isolated diagnostics, failure modes, and review workflow.
- [Sky validation baseline](sky-validation-baseline.md): historical focused sky
  build, unit-test, and PNG-smoke baseline from the unified-sky migration.
- [Sky visual baseline review](sky-visual-baseline-review.md): historical capture
  findings behind removing the legacy planet `SkyFrame` backend.
- [Sky visual iteration plan](sky-visual-iteration-plan.md): historical first
  post-cleanup tuning plan for unified atmosphere sun work.
- [Sky visual pass 001 review](sky-visual-pass-001-review.md): historical clean
  pre-tuning capture review for the first unified atmosphere sun pass.
- [Sky visual pass 001 post-sun review](sky-visual-pass-001-post-sun-review.md):
  historical post-halo capture review for the first unified atmosphere sun pass.
- [Geometry moon migration](sky-geometry-moon-migration.md): migration plan for
  making explicit geometry the canonical app-visible moon path.
- [Geometry moon migration captures](sky-moon-geo-migration-captures.md):
  reproducible capture commands and observations for the migrated moon paths.
- [Moon surface detail plan](sky-moon-surface-detail-plan.md): procedural
  spherical lunar surface map references, implementation outcome, and remaining
  material tuning caveat.
- [Moon surface detail captures](sky-moon-surface-detail-captures.md):
  reproducible capture commands and observations for the lunar surface-map
  routing pass.
- [Procedural Milky Way v2 research](milky-way-v2-research.md): accepted
  atlas-layer research, source/reference takeaways, and field-based direction
  for replacing hand-stamped Milky Way landmarks with procedural structure.
- [Planet visual capture recipes](planet-visual-captures.md): repeatable
  orbit, surface, atmosphere, LOD, celestial, and surface-field capture matrix.
- [Water 3D profiling notes](water-3d-profiling.md): current solver profiling
  captures and optimization candidates.
- [Performance profiling](performance-profiling.md): repeatable host/GPU
  profiling workflow and the cloud-vs-atmosphere comparison harness.
- [Ocean performance notes](ocean-performance.md): current spectral ocean FFT
  cost model, observed map-size tradeoffs, and optimization guardrails.
- [Surface Ocean V1](ocean-surface-v1.md): accepted local-ocean runtime,
  sea-state, lighting, LOD, removal, review, and terrain/planet boundaries.
- [Ocean cloud lighting v1](ocean-cloud-lighting-v1.md): projected cloud shadow,
  planar reflection, and cached fallback contracts, review matrix, measured cost, and
  explicit surface-only limits.
- [Ocean visual capture recipes](ocean-visual-captures.md): repeatable ocean
  debug, cascade isolation, far-field, and reference comparison matrix.
- [Cloud and weather rendering research](cloud-weather-rendering-research.md):
  historical cloud research, reference-code passes, and checkpoints that led to
  the current production direction in `docs/architecture/cloud-rendering.md`.
- [Cloud pre-merge checkpoint](cloud-pre-merge-checkpoint.md): current
  production cloud capture recipe and known-good read before syncing cloud work
  into other worktrees.
- [Cloud foundation integration checkpoint](cloud-foundation-integration-checkpoint.md):
  current shared runtime ownership, accepted surface consumers, direct/cached
  product boundaries, and deferred aerial/orbit scope.
- [Planet cloud integration checkpoint](planet-cloud-integration.md): current
  planet cloud capture baseline, depth-composition issue list, and deferred
  integration work.
- [Procedural terrain reference review](procedural-terrain-reference-review.md):
  current pass over `3DWorld`, `Planet-Generator`, `TerraForge3D`, and
  `terrain-diffusion`, with lessons for code-centric terrain recipes, field
  sets, tile contracts, diagnostics, and ML non-goals.
- [Terrain reboot current captures](terrain-reboot-current-captures.md):
  archived `projects/terrain_workbench_legacy` PNG review set, what to
  inspect, and river driver limitations.
- [Terrain project map](terrain-project-map.md): current active, frozen, paused,
  and legacy lanes plus the directly sampled terrain v1 spine.
- [Terrain v1 reboot](terrain-v1-reboot.md): decision to archive analytical
  hydrology work and reboot terrain as a directly sampled CPU/GPU runtime.
- [Terrain v1 runtime checkpoint](terrain-v1-runtime-checkpoint.md): completed
  CPU/GPU source, traversable clipmap renderer, fixed review pack, measured
  preset baseline, and the boundary before external-consumer integration.
- [Terrain rendering refinement](terrain-rendering-refinement.md): completed
  ground-level rendering checkpoint with heightfield shadows, atmosphere
  composition, procedural materials, LOD handoff fixes, and a frozen source.
- [Terrain backdrop presentation](terrain-backdrop-presentation.md): opt-in
  distant vegetation-coverage and deterministic framing study, with an explicit
  close-range negative-control boundary.
- [Terrain backdrop foreground clearance](terrain-backdrop-foreground-clearance.md):
  strict 150 m AGL and 300 m lower-frustum contract for keeping backdrop framing
  out of unsupported surface-detail range.
- [Terrain rendering quality reset](terrain-rendering-quality-reset.md):
  completed native-resolution correction for final-height shading, seeded
  procedural geology, bounded landform lighting, and honest reference controls.
- [Terrain resolution and bandwidth prototype](terrain-resolution-bandwidth-prototype.md):
  opt-in adaptive mountain geometry, source-v2 spectrum, and generated material
  texture contract while source v1 and the control renderer remain stable.
- [Terrain resolution and bandwidth checkpoint](terrain-resolution-bandwidth-checkpoint.md):
  implemented control/quality comparison, measured review pack, and remaining
  terrain-rendering limits.
- [Terrain midground detail v3](terrain-midground-detail-v3.md): implemented
  opt-in layered materials, explicit backdrop/midground tiers, measured A/B
  review pack, and frozen near-ground boundary.
- [Terrain midground correction v4](terrain-midground-correction-v4.md): frozen
  source/geometry contract for separating macro classification from shading
  detail and rejecting near-frame camera occluders.
- [Terrain source v2.1 refinement plan](terrain-source-v2-1-refinement-plan.md):
  implemented source-only scale separation that preserves v2 above a 64 m
  footprint while bounding sub-110 m relief outside the nonlinear profile.
- [Terrain far-field v1](terrain-far-field-v1.md): superseded directional
  3.2 km backdrop checkpoint and crack-free quality-rendering evidence.
- [Terrain orbit stage plan](terrain-orbit-stage-plan.md): current panoramic
  detached/grounded placement contract with unrestricted orbit yaw and no
  terrain-source conditioning.
- [Terrain quality tile field](terrain-quality-tile-field.md): correction of
  fixed-factor and mixed-LOD quality geometry with a finite world-aligned,
  adaptively tessellated far-field tile contract.
- [Terrain cached backdrop pivot](terrain-cached-backdrop-pivot.md): current
  decision to replace per-frame procedural tessellation with a fixed-focus,
  setup-time cached mesh and a terrain-only sub-millisecond GPU budget.
- [Terrain cached backdrop v1 review](terrain-cached-backdrop-v1-review.md):
  accepted multi-seed fixed-focus product, ownership envelope, setup cost, and
  1440p sub-millisecond terrain-pass evidence.
- [Terrain radial backdrop product v1](terrain-radial-backdrop-product-v1.md):
  promoted radial far-field profile, runtime ownership, exact study parity,
  maintained capture pack, and explicit performance/detail debt.
- [Terrain radial backdrop macro baseline](terrain-radial-backdrop-macro-baseline.md):
  accepted expanded-domain composition and camera target, with explicit cached
  integration, detail, performance, and stop boundaries.
- [Terrain radial fidelity ablation](terrain-radial-fidelity-ablation.md):
  frozen-macro `2 x 2` study separating coherent radial source detail from
  filtered backdrop material detail before either lane can be promoted.
- [Terrain source model study](terrain-source-model-study.md): controlled
  clean-room comparison of scalable mountain source operators through the
  accepted cached-backdrop renderer, with fixed calibration and provenance
  boundaries.
- [Terrain external generator bakeoff](terrain-external-generator-bakeoff.md):
  pinned Terrain Diffusion offline-field comparison, deterministic mountain
  region selection, raster study boundary, and promotion gates.
- [Terrain external generator bakeoff review](terrain-external-generator-bakeoff-review.md):
  completed Terrain Diffusion evidence, determinism corrections, measured
  source-quality gain, backdrop-composition failure, and reference verdict.
- [ShaderToy Mountains fidelity study](terrain-shadertoy-mountains-fidelity.md):
  optional external-source control that compares the unchanged Mountains
  raymarch against an exact-source Cubey mesh transfer and staged ablations.
- [ShaderToy terrain source-shape studies](terrain-shadertoy-source-shape-studies.md):
  external Swiss Alps, Mountain Peak, and erosion-filter comparisons in the
  shared mesh and orbit harness.
- [Mountains-guided terrain source correction](terrain-mountains-guided-source-correction.md):
  bounded clean-room correction of the mountain scale hierarchy, external
  component diagnostics, and the measured rejection before any production v4.
- [Terrain directional backdrop study](terrain-directional-backdrop-study.md):
  rejected directional placement/shaping comparison plus the radial follow-up
  evidence that led to the accepted macro baseline.
- [Terrain source v3 hierarchy plan](terrain-source-v3-hierarchy-plan.md):
  retained hierarchy diagnostics and measured rejection of the smooth massif
  composition as a promotion candidate.
- [Terrain ref closure](terrain-ref-closure.md): final multi-seed reference
  matrix, recipe status, freeze policy, and explicit carry/reject decisions for
  the production terrain reboot.
- [Terrain v1 baseline review](terrain-v1-baseline.md): archived CPU terrain
  patch product, multi-seed captures, measured generation cost, and the basin
  and routing artifacts exposed before river selection or carving.
- [Terrain source bakeoff v1](terrain-source-bakeoff-v1.md): corrected contour
  baseline versus broad OpenSimplex control, fixed-scale measurements, regional
  captures, and the decision to move next toward uplift plus erosion.
- [Terrain landscape evolution v1](terrain-landscape-evolution-v1.md): regional
  analytical stream-power candidate, fixed model parameters, oracle/license
  boundary, product fields, and macro-terrain acceptance contract.
- [Terrain landscape evolution v1 review](terrain-landscape-evolution-v1-review.md):
  clean-room implementation evidence, three-seed metrics, guarded oracle
  comparison, visual findings, and remaining four-neighbor routing artifacts.
- [Terrain process roadmap](terrain-process-roadmap.md): current reset point
  after river/mountain experiments, reusable process-field gaps, and the
  near-term order before adding more biome slices.
- [Terrain ShaderToy operator extraction](terrain-shadertoy-operator-extraction.md):
  deeper ShaderToy terrain/hydro review, what to borrow as clean-room process
  operators or visual cues, and what to keep out of river topology.
- [Terrain ShaderToy erosion filter reference](terrain-shadertoy-erosion-filter-plan.md):
  final `terrain_ref` experiment for a clean-room, slope-aware procedural
  erosion filter with explicit non-hydraulic boundaries and review criteria.
- [Terrain erosion filter generalization](terrain-erosion-filter-generalization.md):
  cross-biome positive/negative control matrix for deciding whether the
  slope-aware filter is a reusable selective process or a narrow mountain
  effect, plus its recommended boundary from regional hydrology.
- [Terrain ShaderToy biome reference map](terrain-shadertoy-biome-reference-map.md):
  classification of local ShaderToy terrain files for clean-room visual/source
  recipes in `terrain_ref`, starting with alpine, dunes, and lake-basin refs.
- [TerrainEngine reference port plan](terrain-engine-reference-port-plan.md):
  isolated recipe plan for porting TerrainEngine's shader-side height/material
  model into the terrain product contract, plus capability review notes for
  tessellation, water, materials, hydrology gaps, and foliage gaps.
- [Terrain ref split plan](terrain-ref-split-plan.md): split the current
  terrain workbench into `terrain_workbench_legacy` and start a clean
  `terrain_ref` visual reference lane before more TerrainEngine or ShaderToy
  rendering work.
- [Terrain ref presentation port plan](terrain-ref-presentation-port-plan.md):
  first rendering pass for the clean reference lane: TerrainEngine-style
  material textures, lighting/fog, captures, and explicit water/tessellation
  deferrals.
- [Terrain ref ShaderToy heightfield plan](terrain-ref-shadertoy-heightfield-plan.md):
  first clean-room ShaderToy-style heightfield recipe in `terrain_ref`, with
  source-specific material response and explicit non-goals.
- [Terrain mountain gully diagnostic plan](terrain-mountain-gully-diagnostic-plan.md):
  revision 24 plan for diagnostic-only gully / erosion fields over the mountain
  stress recipe before any height-affecting erosion pass.
- [Terrain mountain macro shape plan](terrain-mountain-macro-shape-plan.md):
  revision 25 plan for preview surface selection and stronger mountain mass,
  shoulder, and summit source fields.
- [Terrain coherent height rule](terrain-coherent-height-rule.md): revision 26
  guardrail against building visible terrain from independently pasted feature
  masks.
- [Terrain anisotropic mountain profile plan](terrain-anisotropic-mountain-profile-plan.md):
  revision 27 target for replacing straight ridge bands and round summit blobs
  with curved crests, elongated summit support, and saddle suppression.
- [Terrain mountain profile correction plan](terrain-mountain-profile-correction-plan.md):
  revision 28 target for correcting bulgy summits, fin-like crests, and stepped
  shoulders while keeping the coherent height rule.
- [Terrain mountain thermal talus plan](terrain-mountain-thermal-talus-plan.md):
  revision 29 target for adding a bounded thermal/talus process diagnostic
  before more local mountain source tuning.
- [Terrain mountain source/process loop plan](terrain-mountain-source-process-loop-plan.md):
  revision 30 target for comparing mountain source/product/process stages and
  improving the source profile before more erosion or biome polish.
- [Terrain resolution scene-readiness plan](terrain-resolution-scene-readiness-plan.md):
  fixed-extent `513`/`1025`/`2049` audit for separating sample density limits
  from source/process model failures before terrain LOD work.
- [Terrain river stream-order corridor plan](terrain-river-stream-order-corridor-plan.md):
  next river-quality pivot: promote `stream_order` and `flow_accumulation` into
  connected corridor selection while avoiding direct graph-edge rendering.
- [Terrain routing repair plan](terrain-routing-repair-plan.md):
  priority-flood epsilon fill pass for repairing local routing sinks before
  broader hydrology or erosion work.
- [Terrain river degrid and stress pruning plan](terrain-river-degrid-stress-pruning-plan.md):
  revision 11/12 outcomes for moving rendered river centerlines away from raw
  D8 graph paths, pruning low-order support branches, and recovering active
  network coverage.
- [Terrain stress trunk hierarchy plan](terrain-stress-trunk-hierarchy-plan.md):
  revision 13 plan and outcome for promoting major stress support/order-seed
  paths into trunk so tributaries stop carrying most of the visible network.
- [Terrain stress branch distinctness plan](terrain-stress-branch-distinctness-plan.md):
  revision 14 plan and outcome for rejecting near-parallel promoted stress
  trunks so trunk branches add distinct visible drainage area.
- [Terrain stress basin network reach plan](terrain-stress-basin-network-reach-plan.md):
  next stress-river correction: move from sparse promoted branches to a broad
  connected basin tree with explicit reach and continuity acceptance checks.
- [Terrain stress promotion organic cleanup plan](terrain-stress-promotion-organic-cleanup-plan.md):
  revision 16 outcome for rendered trunk-connectivity gating, anti-straight
  stress support filters, and the remaining broad-network driver gap.
- [Terrain graph-first river plan](terrain-graph-first-river-plan.md):
  revision 17/18 river-quality pivot: generate a non-grid river graph first,
  then rasterize graph mainstem, major tributaries, and minor tributaries into
  existing terrain process fields.
- [Terrain river terrain coupling plan](terrain-river-terrain-coupling-plan.md):
  revision 23 correction that makes river fields carve final terrain height
  instead of reading as material-only overlays in 3D previews.
- [Terrain water bodies scope](terrain-water-bodies-scope.md):
  river/lake/wetland/coast/ocean boundaries before moving from river work to the
  next terrain driver.
- [Terrain mountain driver plan](terrain-mountain-driver-plan.md):
  revision 19 implementation for explicit mountain support, ridge, peak, and uplift
  fields in an isolated mountain-range stress recipe.
- [Terrain mountain hierarchy plan](terrain-mountain-hierarchy-plan.md):
  revision 20 plan for range spine, ridge hierarchy, and peak-candidate source
  fields before alpine biome polish.
- [Terrain peak-first mountain skeleton plan](terrain-peak-first-mountain-skeleton-plan.md):
  revision 21 plan for envelope, peak anchors, peak prominence, ridge skeleton,
  and ridge influence fields.
- [Terrain mountain peak readability plan](terrain-mountain-peak-readability-plan.md):
  revision 22 plan for making the peak-first mountain stress recipe visibly
  build from broad support into high peaks.
- [Terrain renderer preview plan](terrain-renderer-preview-plan.md): plan for
  adding a renderer-backed perspective consumer of the rebooted terrain product
  so peaks, basins, and slopes can be reviewed in 3D.
- [Procedural consumer inventory](procedural-consumer-inventory.md):
  current inventory of atmosphere, cloud, ocean, fluid, planet, and future
  terrain procedural consumers that should shape shared foundation work.
- [Procedural shader parity](procedural-shader-parity.md): current
  CPU-vs-GLSL helper parity scope for shared procedural shader migrations.

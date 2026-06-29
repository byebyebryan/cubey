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
- [Sky and celestial current state](sky-celestial-current-state.md): current
  ownership, render paths, and cleanup checklist for the `sky-rendering`
  worktree.
- [Sky validation baseline](sky-validation-baseline.md): focused sky label,
  build, unit-test, and PNG-smoke baseline for the `sky-rendering` worktree.
- [Sky visual baseline review](sky-visual-baseline-review.md): capture review
  findings behind removing the legacy planet `SkyFrame` backend.
- [Sky visual iteration plan](sky-visual-iteration-plan.md): first post-cleanup
  visual tuning scope and commit sequence for unified atmosphere sun work.
- [Sky visual pass 001 review](sky-visual-pass-001-review.md): clean
  pre-tuning capture review for the first unified atmosphere sun pass.
- [Sky visual pass 001 post-sun review](sky-visual-pass-001-post-sun-review.md):
  post-halo capture review for the first unified atmosphere sun pass.
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
- [Planet visual capture recipes](planet-visual-captures.md): repeatable
  orbit, surface, atmosphere, LOD, celestial, and surface-field capture matrix.
- [Water 3D profiling notes](water-3d-profiling.md): current solver profiling
  captures and optimization candidates.
- [Ocean performance notes](ocean-performance.md): current spectral ocean FFT
  cost model, observed map-size tradeoffs, and optimization guardrails.
- [Ocean visual capture recipes](ocean-visual-captures.md): repeatable ocean
  debug, cascade isolation, far-field, and reference comparison matrix.
- [Cloud and weather rendering research](cloud-weather-rendering-research.md):
  historical cloud research, reference-code passes, and checkpoints that led to
  the current production direction in `docs/architecture/cloud-rendering.md`.
- [Cloud pre-merge checkpoint](cloud-pre-merge-checkpoint.md): current
  production cloud capture recipe and known-good read before syncing cloud work
  into other worktrees.
- [Procedural terrain reference review](procedural-terrain-reference-review.md):
  current pass over `3DWorld`, `Planet-Generator`, `TerraForge3D`, and
  `terrain-diffusion`, with lessons for code-centric terrain recipes, field
  sets, tile contracts, diagnostics, and ML non-goals.
- [Terrain reboot current captures](terrain-reboot-current-captures.md):
  current `projects/terrain` 513 PNG review set, what to inspect, and river
  driver limitations.
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
- [Terrain water bodies scope](terrain-water-bodies-scope.md):
  river/lake/wetland/coast/ocean boundaries before moving from river work to the
  next terrain driver.
- [Procedural consumer inventory](procedural-consumer-inventory.md):
  current inventory of atmosphere, cloud, ocean, fluid, planet, and future
  terrain procedural consumers that should shape shared foundation work.
- [Procedural shader parity](procedural-shader-parity.md): current
  CPU-vs-GLSL helper parity scope for shared procedural shader migrations.

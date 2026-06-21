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
  pivot note from atmosphere-owned sun/moon disks to planet-owned celestial
  bodies with atmosphere as a consumer.
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
  findings and the decision to remove the legacy planet `SkyFrame` backend.
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
- [Procedural terrain reference review](procedural-terrain-reference-review.md):
  current pass over `3DWorld`, `Planet-Generator`, `TerraForge3D`, and
  `terrain-diffusion`, with lessons for code-centric terrain recipes, field
  sets, tile contracts, diagnostics, and ML non-goals.
- [Procedural consumer inventory](procedural-consumer-inventory.md):
  current inventory of atmosphere, cloud, ocean, fluid, planet, and future
  terrain procedural consumers that should shape shared foundation work.
- [Procedural shader parity](procedural-shader-parity.md): current
  CPU-vs-GLSL helper parity scope for shared procedural shader migrations.

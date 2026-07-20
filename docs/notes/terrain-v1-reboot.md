# Terrain V1 Reboot Decision

Date: 2026-07-10

## Context

The first terrain workbench spent substantial effort on authored-looking driver
shapes, river topology, and mountain process models. The follow-up terrain
project made those products explicit and inspectable, but regional hydrology and
landscape evolution became the center of the design before the base terrain was
visually credible. The analytical candidate is expensive, bounded to a finite
regional graph, and still carries grid-shaped drainage artifacts.

Meanwhile, `terrain_ref` demonstrated a more useful v1 midpoint. Compact
world-space noise formulas, mesh displacement, procedural materials, and a
small clipmap produce varied terrain immediately and at interactive rendering
cost. Its recipe gallery is not a production architecture, but its simple
TerrainEngine source is a stronger visual control than the active analytical
candidate.

## Decision

Reboot terrain v1 as a coherent noise-field runtime with optional local
weathering. Render directly on the GPU and expose a matching CPU point sampler
for traversal, tests, and future consumers. Start with mountain, upland, and
plains parameter sets over one source model.

Archive the existing `projects/terrain` wholesale as
`studies/terrain/hydrology`. This keeps the patch products, exports,
regional routing, analytical oracle, and accumulated tests available without
binding the new runtime to their contracts.

## Lessons Carried Forward

- Hand-authored masks and isolated lines do not scale into procedural terrain.
- Coherence matters more than the number of named features or fields.
- Macro mass, structural relief, and local detail need distinct frequency and
  amplitude roles but must compose into one heightfield.
- A known-good reference is more useful than repeatedly inventing a new driver.
- Neutral height, slope, multi-seed, and surface views prevent materials from
  hiding source defects.
- CPU/GPU duplication needs a parameterized evaluator and parity tests, not a
  growing pair of recipe switches.
- Local weathering can improve detail but is not hydrology.
- Direct evaluation avoids long generation steps and multi-gigabyte artifacts;
  bounded diagnostic grids retain the useful inspection workflow.

## Initial Product Boundary

The first complete checkpoint is a traversable standalone planar scene. It uses
shared atmosphere lighting, procedural materials, camera-centered LOD, and CPU
height queries. No external scene adapter is included yet. No river, water,
foliage, planet, persistence, or baked heightfield contract is implied.

The next architectural decision happens only after this checkpoint: integrate
one real external consumer and use that experience to decide which terrain
interfaces, if any, belong in engine foundation.

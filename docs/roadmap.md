# Roadmap

Cubey is a native C++20/Vulkan desktop workbench for focused rendering and
simulation projects. This page records current readiness, product boundaries
and the evidence needed to reopen work. Completed implementation phases live
in the [foundation checkpoint archive](archive/foundation-checkpoints.md).

## Current Direction

Projects own their scenes, simulation and presentation; the shared foundation
owns recurring resource, rendering, configuration and host contracts. Select
new work from a concrete product need. The [design](DESIGN.md),
[architecture index](architecture/README.md) and
[project guides](README.md#project-docs) describe those ownership boundaries.

## Current Readiness Checkpoint

Reviewed against runtime source `86885a4d` on 2026-10-17:

| Area | Current boundary | Reopen when |
| --- | --- | --- |
| Host, GPU ownership and capture | Windowed/headless hosts, queued GPU work, owner-ordered cleanup, bounded/failure-safe PNG and video capture. | A consumer demonstrates a lifecycle, synchronization or capture failure. |
| Configuration | Project-owned typed Config V2 facades; the global legacy registry is removed. | A repeated consumer requirement needs a shared schema contract. |
| Build and library boundaries | Compiler-tracked shader dependencies, configured link/include guards, standalone public-header compilation. | A new library or build variant exposes a missing contract. |
| Resource construction | Terrain and all five Fluid resource families support partial-failure cleanup and direct retry. | A new resource owner or failure path needs qualification. |
| glTF Viewer V1 | Closed single-asset milestone: staged loading, bounded incremental uploads, atomic activation, animation/deformation and the represented required-use material extensions. | A real asset exposes a compatibility failure or a product needs better total loading latency or multi-asset streaming. |
| Terrain V1 | Closed external-heightfield far backdrop with cached geometry and shared saved-water consumers. | A concrete consumer fails or a bounded new terrain product is selected. Close terrain remains separate. |
| Ocean | Local/curved-surface renderer with spectral waves, fixed-cost default cascades and shared environment lighting. | A local-water consumer supplies a concrete rendering or composition need. |
| Atmosphere and clouds | Shared sky/celestial foundation and surface-only Cloud V1; explicit visible, shadow, reflection and cached PBR products. | Look development improves an accepted surface consumer. Aerial/orbit products need a separate decision. |
| Fluid 2.5D | Standalone builtin Vulkan shallow-water experiments and numerical diagnostics. | A bounded solver or presentation question justifies another experiment. |
| Terrain Water | Dedicated live external SynxFlow and recorded terrain-water frontend, with explicit preparation and worker qualification. | An admitted producer, site or integration requirement changes. Builtin diagnostics are not an equivalent mountain-flow fallback. |
| Planet | Paused since 2026-10-15; retained under `studies/planet` behind an opt-in build. | A separately selected product has a concrete visual goal. Earlier scoped closure is not visual acceptance. |

The latest full runtime qualification belongs to `d713512b`: 252 dev groups,
six alternative Fluid domain groups, seven configure variants, focused
sanitizers, 19 virtual-X11 windowed groups and six synchronization groups. The
later Ocean owner-order correction at `86885a4d` has a separate focused
qualification. These are different source scopes; neither establishes physical
desktop interaction, renewed human visual acceptance or external-worker
requalification. The [checkpoint archive](archive/foundation-checkpoints.md)
retains the implementation narrative and original measurements; executable
validation entry points are in [getting started](getting-started.md#configure-build-and-test).

The separately scoped [SDF guardian checkpoint](notes/sdf-guardian-closeout.md)
is settled by user review on 2026-10-22: organic mirrored geometry, limestone
and subtle stylized marble are retained as an authored rendering/replay demo.
Further modeling and material realism are deferred. A refreshed construction
film is optional presentation work; continuous brush editing remains separate.
Its native receipts do not extend the repository-wide runtime qualification
above.

## Material and SDF demo direction

The [material and SDF roadmap](notes/material-demo-roadmap.md) separates a mesh
PBR material workshop, an SDF material workshop using the same recipes, and an
SDF authored-content showcase led by construction playback.

The first two stages are implemented and [qualified](notes/material-workshops-evidence.md):
`pbr_materials` provides an ordinary-mesh gallery and parameter workshop;
`sdf --workshop sphere|block|csg` provides matched specimens and CSG inspection.
[Shared recipes and procedural evaluation](architecture/material-recipes.md)
now sit outside SDF. PBR editing uses bounded retired-frame tables; existing
immutable material consumers and accepted SDF content retain their behavior.

The bounded [material library expansion](notes/material-library-expansion-plan.md) is
implemented and [qualified](notes/material-library-evidence.md),
informed by [surface research](notes/material-library-surface-research.md): wood,
rusted steel, woven cloth, kraft cardboard and frosted glass, with useful
specimens and shared catalog/persistence support. Twelve shared recipes now work
in both workshops; the new report includes matched specimens, components and
bounded optical comparisons. Stage C and deeper material realism remain deferred. The workshop
evidence is a bounded native checkpoint, not a repository-wide performance or
artistic acceptance claim.

The [material presentation pass](notes/material-presentation-evidence.md)
delivers separate PBR and SDF short stories, native presets, committed media and
a compact report. Technical/delivery checks pass; new shot choices await user
review. New materials and the guardian construction-film refresh remain separate.

## Public presentation

The [public presentation site](https://dev.byebyebryan.com/cubey/) now owns the
visual demo collection, audience-facing introductions and project navigation.
The compact root README owns repository entry and development links. The
[site guide](../site/README.md) describes the authored Astro build and Pages
publication; [the polish record](notes/cubey-site-polish.md) records the reviewed
seven-subject overview and its media/interaction boundaries.

The first detailed chapter, [Building and cutting a solid](notes/cubey-site-sdf-story.md),
connects native SDF construction and material cuts with small interactive 2D
illustrations and a short development history. Further detailed stories remain
editorial choices. The chapter does not reopen native brush, incremental-field,
GI or material-realism experiments.

The earlier five-film [media package](media/showcase/README.md),
[manifest](media/showcase/manifest.json) and [showcase plan](showcase.md) retain
their exact media, recipes, attachments and license boundaries as historical
publication evidence. The glTF composite holds one saved water state; Terrain
Water shows a recorded timelapse. Neither is a live solver-performance claim.

## Next work and deferred boundaries

The [documentation foundation pass](evidence/foundation-2026-10/README.md) is
complete: current authority, historical context, navigation and mechanical
checks have explicit owners. New engine abstractions still need repeated
consumer pressure:

- Use the shared dynamic environment handoff for future atmosphere/PBR
  consumers. Expand render-graph or command ownership only around repeated
  setup or synchronization needs.
- Reopen glTF decode throughput, concurrency or streaming from product evidence.
  Single-asset upload responsiveness is closed. A specialized sheen environment
  needs a real directional-quality failure first.
- Keep shoreline/bathymetry, shallow-water coupling, close terrain and
  planet-surface navigation as separately chosen products.
- Keep timeline semaphores, split queues, parallel recording, graph aliasing,
  partial residency and general resource scheduling deferred until a measured
  workload needs them.
- Preserve frozen reference studies and rejected experiments as evidence.
  Their old plans are not an active implementation queue.

## Historical phase entry points

These anchors preserve the former roadmap entry points. Each links to the
recorded phase rather than a current work commitment.

<a id="phase-0-repo-foundation"></a>

- [Phase 0: repo foundation](archive/foundation-checkpoints.md#phase-0-repo-foundation)

<a id="phase-1-windowed-vulkan-runtime-skeleton"></a>

- [Phase 1: windowed Vulkan runtime](archive/foundation-checkpoints.md#phase-1-windowed-vulkan-runtime-skeleton)

<a id="phase-2-headless-output-and-runtime-boundary"></a>

- [Phase 2: headless output](archive/foundation-checkpoints.md#phase-2-headless-output-and-runtime-boundary)

<a id="phase-3-fractal-project"></a>

- [Phase 3: Fractal](archive/foundation-checkpoints.md#phase-3-fractal-project)

<a id="phase-35-particle-cube-example"></a>

- [Phase 3.5: particle example](archive/foundation-checkpoints.md#phase-35-particle-cube-example)

<a id="phase-4-threading-and-async-runtime-boundary"></a>

- [Phase 4: threading and async](archive/foundation-checkpoints.md#phase-4-threading-and-async-runtime-boundary)

<a id="phase-5-first-real-project"></a>

- [Phase 5: project portfolio](archive/foundation-checkpoints.md#phase-5-first-real-project)

<a id="phase-6-runtime-extraction"></a>

- [Phase 6: runtime extraction](archive/foundation-checkpoints.md#phase-6-runtime-extraction)

<a id="deferred-until-concrete-pressure"></a>

- [Original deferred-work record](archive/foundation-checkpoints.md#deferred-until-concrete-pressure)

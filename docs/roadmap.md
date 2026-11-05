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

The next selected rendering direction is a dedicated ray-tracing project. The
[first-pass plan](notes/raytracing-first-pass-plan.md) proposes a native hardware
triangle renderer around a static Stanford dragon, followed by a matched SDF
comparison and later light-transport work. This is planning, not an implemented
renderer or a new runtime qualification.

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

The current [authored CSG closeout](notes/sdf-csg-closeout.md) selects the hill
observatory, field-kit robot, canal mill and terraced courtyard. Their models
are approved; the site presents native posters, films and a matched observatory
section. Guardian is a [retired study](../studies/sdf/guardian/README.md);
its earlier checkpoint and stress measurements remain historical evidence.
These native receipts do not extend the repository-wide runtime qualification
above. The [deployment metadata](https://dev.byebyebryan.com/cubey/metadata/deployment.json)
identifies the public site's served source revision.

The bounded SDF
[tracing/cache/history comparison](notes/sdf-tracing-comparison-plan.md), following
the [representation review](notes/sdf-representation-design-review.md), now has
implemented opt-in analytic/hybrid controls, exact region programs and scripted
undo/publication evidence. The [decision report](notes/sdf-tracing-comparison-results.md)
records Robot's quality/memory alternative and Guardian's failed scaling gate.
These are dated comparison results. Cached is the current authored-scene default;
the [SDF guide](../projects/sdf/README.md#rendering-modes) describes the explicit
alternatives. The subsequent
[shader selection loop](notes/sdf-shader-selection-results.md) qualifies prebuilt
lit/debug and published-material selection on the opt-in source paths. Guardian
1080p rear GPU p95 improves from about 51 ms to 21 ms; its three measured views
still exceed 16.7 ms. Startup shader compilation, remaining source execution and
region-builder/edit latency retain separate investigation boundaries.

## Material and SDF demo status

The dated [material and SDF roadmap](notes/material-demo-roadmap.md) separates
the mesh PBR workshop, the SDF workshop and authored SDF content. Both native
workshops are implemented and have bounded
[qualification evidence](notes/material-workshops-evidence.md):
`pbr_materials` provides ordinary-mesh specimens and parameter editing;
`sdf --workshop sphere|block|cut-block|swatch|csg` provides matched specimens,
material inspection and CSG examples. The shared recipe and procedural
evaluation contract lives in [material recipes](architecture/material-recipes.md).
PBR edits use bounded retired-frame tables; existing immutable material
consumers and accepted SDF content retain their behavior.

The bounded [material library expansion](notes/material-library-expansion-plan.md)
is implemented and [qualified](notes/material-library-evidence.md), informed by
[surface research](notes/material-library-surface-research.md). Wood, rusted
steel, woven cloth, kraft cardboard and frosted glass extend the accepted seven
looks; all twelve shared recipes work in both workshops with catalog and
persistence support. These evidence notes are scoped native checkpoints, not a
repository-wide performance or general artistic-acceptance claim.

The [native material presentation record](notes/material-presentation-evidence.md)
retains its presets, captures and technical/delivery checks. Its 2026-10-23 note
records the native shot review as open; publishing the later web chapter did not
recapture those scenes or change that evidence scope.

## Public presentation

The [public presentation site](https://dev.byebyebryan.com/cubey/) owns the
current visual collection, audience-facing introductions and project
navigation. The compact root README owns repository entry and development
links. The [site guide](../site/README.md) describes the authored Astro build
and Pages publication; the [polish record](notes/cubey-site-polish.md) retains
its source-pinned review scope.

The SDF web chapter, [Building and cutting a solid](https://dev.byebyebryan.com/cubey/demos/sdf/inside-a-solid/),
was published on 2026-10-25. It connects existing native construction and
material-cut captures with interactive 2D explanations and a short history; it
does not claim live sculpting or new native runtime capabilities. This initial
web chapter is published. The current presentation expands it with the
approved quartet, robot construction and a matched observatory cutaway.
Continuous brush editing, sustained complex-scene local updates, GI and deeper
material realism remain deferred choices. Finite private local-update research
is retained in the [edit-service record](notes/sdf-edit-service-results.md).

The earlier five-film [showcase archive package](media/showcase/README.md),
[manifest](media/showcase/manifest.json) and [storyboard](showcase.md) retain
their exact media, recipes, attachments and license boundaries. The glTF
composite contains one saved water state; Terrain Water shows recorded SynxFlow
fields. Neither is a live solver-performance claim.

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
- Keep general SDF desktop brush sculpting, sustained complex-scene editing,
  GI, terrain coupling and deeper rough refraction deferred until a concrete
  experiment is selected. VR is not a current target. Mesh-to-field conversion
  remains a separate exploration from the closed authored CSG milestone.
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

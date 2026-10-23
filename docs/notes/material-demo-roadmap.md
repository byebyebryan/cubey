# Material and SDF demo roadmap

Date: 2026-10-23

Status: implementation of Stages A and B authorized on 2026-10-23. Plans committed
as `b35d1a89`; local milestone commits and native qualification are in progress.
Stage C remains deferred. No push is included in this loop.

## Purpose

Develop three demonstrations with different questions:

| Demo | Question | Main presentation |
| --- | --- | --- |
| PBR materials | How do material parameters and procedural surfaces respond to lighting? | Ordinary mesh specimens, a curated material gallery and a small parameter workshop. |
| SDF materials | How do the same material recipes behave on implicit surfaces? | Matched specimens plus fused forms, cuts, material ownership and geometric thickness. |
| SDF sculpting and authored content | What can we make, and how was it constructed? | Recognizable authored pieces, edit-by-edit construction and finished presentation. |

Cubey is an experimentation workbench. Learning and explaining the technique,
making reusable capabilities and delivering a pleasing demo are worthwhile
outcomes without requiring a novel rendering algorithm.

The SDF work already contains parts of all three demos. Separate their purpose
and ownership before deciding which source directories or executables to split.
The first implementation candidate is the [PBR materials demo](pbr-material-demo-plan.md).

## Starting point

The [shared PBR renderer](../architecture/pbr-ibl.md) already provides a material
data model as well as shading: factors, texture inputs, optical properties,
render resources, environment lighting and display transforms. The
[PBR furnace](../../projects/pbr_furnace/README.md) provides diagnostics, and
the glTF viewer provides an asset-oriented consumer.

The [SDF project](../../projects/sdf/README.md) has accepted material content:
neutral gray, limestone, subtle Balanced marble, granular terracotta, ivory
ceramic, satin bronze and tinted glass. Its sphere and guardian share those
values through a project-local authoring catalog. The procedural evaluators,
rough-diffuse response and authored material serialization remain SDF-local.

Shared atmosphere/cloud runtimes already supply visible backgrounds, sun data,
diffuse SH, coherent reflection probes and foreground-aware composition. The
[latest cloud correction](sdf-environment-presentation-evidence.md#follow-up-restore-shared-cloud-horizon-quality)
restores the existing horizon handoff and improves the bounded presentation.
Those services can be reused without creating another sky or cloud renderer.

The guardian and robot establish a working authored-content route independent
of VR input or learned geometry. The guardian has 1,011 explicit primitive
leaves and a 65-operation mirrored history. Rendering, replay and material
editing work; continuous brush sculpting remains a separate interaction and
field-update problem.

## Shared material content

The foundation addition is a portable material recipe and procedural surface
evaluation layer. It should let projects select a named material, override a
few parameters, serialize the resolved recipe and apply it to different geometry.

Keep four responsibilities distinct:

| Owner | Responsibility |
| --- | --- |
| Shared material content | Validated constant/optical properties, parameterized procedural patterns, deterministic sampling and explicit material coordinates. |
| Shared rendering | PBR response, safe material publication, environment products, frame composition and display transforms. |
| Geometry adapter | Mesh attributes and shading basis, or SDF hit data, CSG ownership, blend weights and interface tracing. |
| Demo | Specimen layout, cameras, lighting presets, workshop controls, explanations and presentation. |

Glass primarily needs a reusable optical recipe. Its thickness source and
transport approximation belong to the geometry/renderer adapter. Marble also
needs a spatial evaluator producing color, roughness and bump detail. A recipe
must not contain a guardian tree, a mesh, a camera, cloud weather or exposure.

The current SDF content is the reference for the first catalog. Promote it
mechanically before changing its appearance. Use the new mesh demo and the
existing SDF consumer to prove the shared boundary; avoid a general material
graph, asset database or renderer-wide material-manager redesign.

## A PBR material workshop

Proposed working front door: `projects/pbr_materials`, executable `pbr_materials`.
It uses `ForwardPbrRenderer3D` and ordinary meshes, with no SDF runtime dependency.

Provide a curated gallery that looks useful immediately and a focused specimen
view for learning and editing. Start with the existing seven looks, a sphere and
a small beveled block. Retain a fixed studio environment for comparison and
reuse clear low sun and frozen clouds for presentation. Parameters and component
views should make roughness, metalness, bump, color variation and absorption
understandable.

Success means the catalog exists outside SDF, its procedural content works on
meshes, its parameters can be edited and saved safely, and the demo has native
visual/cost evidence. The furnace stays a diagnostic application. The glTF
viewer stays an asset viewer. This stage does not implement every glTF material
extension as another workshop experiment.

The detailed [PBR plan](pbr-material-demo-plan.md) specifies the material contract,
presentation, resource-update gap, checkpoints and proposed acceptance gates.

## B SDF material workshop

Reuse the same recipe files, parameter meanings, seed, material coordinates,
specimen size, light and display settings. Begin with a sphere and rounded box
matching the mesh specimens, then add one compact CSG specimen with fused forms,
a shallow groove and a through-cut.

The workshop should expose what SDF contributes: procedural continuity over
fused forms and newly exposed surfaces, explicit cut-material ownership, smooth
material blending, geometric versus shading normals, and measured in-medium
distance for glass. Keep those mechanisms visible in targeted controls and
diagnostics rather than requiring a long sculpting film to explain them.

Compare opaque material samples and response under matched conditions. Do not
require mesh/SDF silhouette or shadow pixels to match. Raster and SDF glass use
different thickness/transport approximations; share the optical intent and
label those differences instead of claiming identical transport.

Keep SDF v2 inline materials and old fingerprints readable. A shared-recipe
adapter must preserve CSG ownership, model-space patterns, history behavior and
geometry caching. Whether this becomes a separate executable or an explicit
workshop mode/front door on the current SDF runtime is a Stage B decision.
Avoid duplicating the SDF renderer merely to obtain a second application name.

## C SDF sculpting and authored content showcase

Keep `projects/sdf` and its existing scenes as the working content reference.
Lead with the settled guardian and the accepted robot, with the vessel/arch as
secondary experiments and driftwood retained only as historical evidence.

Make construction the main story: large forms join, cuts open cavities and
relief establishes expression. Use a few curated materials from the shared
catalog and the retained environment presets to present the result. Workshop
controls can remain available as inspection tools without driving the showcase.

A focused presentation pass could consolidate the best construction sequence
with current finished materials and lighting. Preserve the original films and
their source identities; the complete earlier guardian construction film uses
an earlier marble appearance. A finished statue is also possible with meshes;
the saved implicit construction and inspectable authoring process explain this
experiment's value.

Interactive brush editing, localized/incremental field rebuilds, larger CSG
budgets and learned/imported content need separately selected goals. The
roadmap title does not imply that a desktop sculpting editor already exists or
that building one is required to finish the content showcase.

## Sequence and completion boundaries

1. Review this split and the PBR V1 scope. Preserve the accepted SDF scenes,
   catalog values, reports and native artifact identities.
2. Build the PBR workshop from the existing renderer and environment services.
   Promote the smallest material-content layer required by it, with SDF
   compatibility checks as the second-consumer guardrail.
3. Consolidate the SDF material workshop around the shared recipes. Give its
   CSG-specific examples their own presentation and qualification.
4. Assemble the authored-content showcase using the settled model/history and
   selected catalog looks. Reopen geometry or realism work only for a concrete
   failure or an explicitly chosen new subject.

These are demo completion boundaries, not a prerequisite chain for a broad
engine rewrite. Existing SDF scenes remain runnable during every stage. Shared
code moves require a real second consumer and preservation evidence. Completed
reports remain immutable; new work gets its own report and native receipts.

Terrain/ocean backgrounds, new cloud models, global illumination, caustics,
nested media, UV/image authoring, node graphs, VR and AI-generated geometry are
retained as separate experiments. They are not required by this roadmap.

## Retained references

- [SDF material catalog and contrast evidence](sdf-material-study-evidence.md)
  and [contrast pass](sdf-material-contrast-pass.md).
- [Environment and cloud integration evidence](sdf-environment-presentation-evidence.md).
- [Guardian closeout](sdf-guardian-closeout.md) and [mirrored-stroke evidence](sdf-mirror-evidence.md).
- [Reference-first rendering workflow](../architecture/rendering-feature-workflow.md).

The latest retained implementation is `039fdb9f`. This roadmap adds no runtime
capability or new visual acceptance claim.

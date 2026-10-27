# Cubey site structure specification

Date: 2026-10-24

Status: recommended design, updated by the completed workshop-alignment goal. Study A's dark
visual direction is selected. This specification supersedes the six-demo roster
and optional-glTF framing in the first visual study. It does not start production
implementation at this checkpoint or claim user approval of every proposed
label/view; the authored site was built and published later, as linked below.

[Open the aligned mockups and review](http://starship:8787/reports/6339d2350e5953a016b2dbc209720f5f93e78b8664646f2a7f6b1c1b58657922/).

The authored implementation followed this specification under `site/` and is
now published. The [implementation progress record](cubey-site-implementation-progress.md)
retains the release history; the [site guide](../../site/README.md) owns current
routes, media policy and publication details. The mockups above remain design
history, including the proposed eight-subject roster.

## Structure decision

Organise around **subjects → demos/features → scene variants**. Keep native
executables and capture files as implementation/evidence identities behind
those records. Home is curated; it does not define the whole collection.

Three structures were considered:

| Model | Finding |
| --- | --- |
| Project/executable first | Fits repository folders, but makes shared capabilities and multiple visual stories awkward. |
| Existing clip first | Repeats the README limitation and excludes supported work without current gallery films. |
| Subject, scene and variant | Recommended: familiar visual subjects, useful choices, and depth without a page per setting. |

Global navigation is Demos, About and Source. Explore is a visual subject
directory, with direct links into scenes/chapters. It groups Simulation, Land/
sea/sky, Objects/surfaces and Further explorations as editorial browse bands;
these are not new canonical subjects or extra navigation levels. Seven subjects
are primary; Fractals remains discoverable with quieter prominence.

The [refinement review](cubey-site-refinement.md) retains the broad content model
while removing the flat mixed-level catalogue, default filter/preset panels,
duplicate Terrain child pages and hidden feature-tour modes.

## Editorial framing

Cubey is the author's personal graphics/simulation workshop and its supporting
engine. The current project is a spiritual successor to the earlier C++/OpenGL
learning project. Both follow the same engine/experiment feedback loop. The
reboot brings more professional experience, Vulkan and Codex, making broader
and deeper exploration practical. Interests drive the experiments; experiments
and the shared engine improve one another. The subject structure organises
today's collection, not a predetermined roadmap or curriculum.

Keep demos as the main way in. Home adds a brief personal introduction; About
explains the origin, coding-agent collaboration and the care taken with
established techniques. Relevant demo captions can show concrete connections:
2.5D fluid work feeding Terrain Water, or atmosphere/cloud work supporting
material lighting and backdrop. Use the existing related-demo links, with a
reason to follow them. Detailed branches, restarts and discarded approaches
remain later development-journey content.

The [implementation brief](cubey-site-plan.md#project-identity-and-editorial-tone)
records the author's fuller context and proposed copy. The linked mockups now
implement that framing: a workshop introduction beside the leading Ocean image,
a personal About page with an experiment/foundation feedback figure, two visual
connection sketches on Home, and explained reciprocal links on the relevant
demo pages. The [alignment review](cubey-site-workshop-alignment.md) records the
checks and refinements.

## Proposed route/page map

These are semantic production routes, not the static mockup filenames.

| Route | Model and responsibility |
| --- | --- |
| `/` | Editorial visual opening, brief workshop identity, complete subject paths and selected work. |
| `/demos/` | Visual subject directory with direct scene/chapter links; no internal registry counts or default filters. |
| `/demos/simulation/` | Six visible peers with 2D/3D/Surface flow badges; no filter needed for six demos. |
| `/demos/simulation/<scene>/` | Water 3D, Fire 3D, Explosion, Water 2D, dye/smoke 2D, builtin shallow water. |
| `/demos/terrain/` | One ordered visual experience: dry terrain, then water, with named chapter anchors. |
| `/demos/ocean/` | Direct ocean surface experience with saved sea-state/light views. |
| `/demos/atmosphere/` | Three visible chapters: day/night, clouds and celestial sky; ordinary jump links. |
| `/demos/materials/` | Distinct mesh and SDF material front doors and selected recipe views. |
| `/demos/sdf/` | Authored-form collection. |
| `/demos/sdf/<scene>/` | Guardian sculpture or robot/construction. |
| `/demos/gltf/` | Four visible chapters: materials/light, optics, animation/deformation and texture/detail. |
| `/demos/fractals/` | Direct focused procedural-field experience. |
| `/about/` | Personal origin, coding-agent collaboration, experiment/foundation relationship, build/source entry and credits. |

Use a query/view state or named sections for saved examples; do not invent a
new page for every recipe/time/preset. Single-experience subjects do not need
an extra overview page repeating the same media. The mockup uses a generic
`demo.html?id=...` adapter only to evaluate these page models.

## Subject identities

| Subject | Required identity in the design |
| --- | --- |
| Simulation | Water, Fire and Explosion are peers; 2D is normal collection content, not history. Builtin shallow water remains distinct from external Terrain Water. |
| Terrain | Terrain has a dry-rendering identity before water. Prepared sources, detail and shadows belong here; the terrain-water story adds its own flow/rendering boundary. |
| Ocean | Horizon-scale surface, sea states, whitecaps and environment response. No unimplemented shoreline, wakes or underwater promises. |
| Atmosphere & light | Standalone sky: solar time, scattering, surface clouds, stars, moon and Milky Way. It is more than scenery behind other demos. |
| Materials | Shared procedural recipes on distinct ordinary-mesh and SDF routes. Imported glTF materials remain a different visitor story. |
| SDF forms | Sculpture and authored construction. Replay of building the object is a demo result; development history is later editorial content. |
| glTF rendering | Importer/renderer capabilities lead. The helmet/terrain composite is an optional existing reference, not the identity or proof of every feature. |
| Fractals | Preserve the independent smaller experiment, with quieter homepage prominence if appropriate. |

The [content map](cubey-site-content-map.json) accounts for all fourteen native
owners and twenty-one presentation entries. The number is a useful current
inventory, not a required twenty-one-page architecture.

## Page composition rules

Home curates media with compact subject links; it does not repeat the directory.
Explore owns the complete visual subject overview and direct scene access.
Simulation shows all six scenes together. Terrain is one land/flow experience.
Atmosphere, Materials and glTF expose their content as visible chapters rather
than hiding it behind feature tabs. SDF uses separate sculpture and construction
scene pages; Ocean/Fractals are direct focused experiences.
The dark palette and media language hold these pages together without forcing
one layout on every subject.

Related-demo captions should explain an actual connection where one is useful.
The shared foundation is a thread through the work, introduced through examples
and About rather than a new top-level feature checklist. Keep the personal,
curiosity-led tone alongside precise claims about what a visual demonstrates.
The source content map owns two selected connection annotations and their
evidence/scope. Terrain/shallow water explain their exploration/shared-rendering
relationship while keeping recorded SynxFlow and builtin solvers distinct.
Atmosphere/Materials show reuse of shared environment services. On shallow water,
fold the context into its existing related-work section to avoid repeating the
same Terrain Water image. On Home, use distinct images for the selected-work and
connection sections.

Keep headings compact. Let a recognisable native image or labelled design
placeholder enter the first viewport on narrow screens. Preserve complete wide
frames and appropriate square sculpture views. Put lengthy limitations,
extension identifiers and implementation contracts in secondary details rather
than above the main media.

Use ordinary links, scroll and film controls for primary browsing. Do not generate
an Overview/preset panel from every entry's variant list. Retain selection only
when it compares real, meaningful media: the matched wood solid/preview/cut and
Guardian marble/limestone. These selections survive reload and change the shown
image; a comparison pauses moving media. Guardian's selector changes the hero,
without repeating the initial image in another large block.

Chapter links use immediate native anchor jumps. The review found animated
scroll could leave a linked chapter out of position during navigation/reload.
Media stays dominant when alternating left/right chapter layouts. The variant
inventory remains a capture/editorial backlog, not a public control schema.

## glTF visual feature mapping

| Visual entry | Current contract coverage |
| --- | --- |
| Materials & light | Unlit, emissive strength, specular, clearcoat, anisotropy, iridescence and sheen. |
| Transmission & volume | IOR, transmission, volume/thickness/attenuation and dispersion, within the bounded current renderer. |
| Animation & deformation | Core node animation, skins and morph targets. |
| Texture & detail | UV selection, texture transforms and BasisU KTX2 handling. |

These map all thirteen supported required-use extensions from
[the current asset contract](../architecture/gltf-assets.md). The tour can use a
few representative native assets to show visual groups, with the complete
supported set linked secondarily. Do not claim complete glTF compatibility,
certification, arbitrary extensions or exact general transparent transport.
Metadata bounds/loading cage, staged loading and atomic activation are useful
secondary capability context. No browser file importer/editor is proposed.

## Media readiness and later visual layers

The design study initially selected native inputs for eight entries and used
schematic placeholders for thirteen. The implementation now supplies native web
media for all twenty-one entries; `site/media/manifest.json` owns that selection
and provenance. Extra proposed variants remain an editorial backlog. Do not use
the old gallery's attachment availability as the architecture boundary.

The variant lists express presentation possibilities, not a fixed campaign of
new captures. Pick useful variants and matching images/films during implementation.
Look for qualified retained media before running existing native capture recipes.
New native features are outside that work unless separately selected.

Technique content can later attach annotated frames, comparison controls,
particle/grid illustrations, material/lighting breakdowns and measured plots.
Development journeys can attach timelines, failed-attempt comparisons and
source history. Keep those optional layers out of the first-release navigation
until useful content exists. The complete SDF/projectR and other project context
remains available in repository evidence and closeout records.

## Visitor scenarios and design acceptance

The design audit checks these observable paths:

1. Home → Simulation → Explosion, as a peer to Fire/Water.
2. Simulation shows all six peers without operating a filter: Water/Fire/Explosion 3D, both 2D demos and distinct builtin surface flow.
3. Terrain presents base rendering before Terrain Water, with the right native owners.
4. Atmosphere exposes day/night, clouds and celestial chapters without mode changes; deep links survive reload and placeholders remain labelled.
5. glTF exposes material/optics/motion/texture groups, with all thirteen supported
   extensions mapped and the general helmet reference secondary.
6. Materials keeps mesh and SDF chapters distinct; the cut comparison and Guardian hero selection match their native images and restore on reload.
7. Explore reaches all twenty-one presentation entries, all eight subjects and
   all fourteen native owners without depending on homepage prominence.
8. Desktop/mobile layouts, keyboard selection, actual film playback, source
   routes and placeholder labels remain usable and internally coherent.

The report contains bounded browser/static receipts for these checks. They
establish a reviewable design proposal, not final user artistic acceptance or
production/native runtime qualification.

The aligned mockups add an editorial review against the author's project framing:
the personal workshop and coding-agent collaboration are clear, connections
between demos are concrete, and no fixed roadmap is invented. The current report
covers all twenty-one entry identities, twenty-eight narrow-layout cases and the
new connection paths; the later implementation must recheck its final copy/media.

## Remaining user-review choices

- Subject names and relative homepage prominence, including how quietly to place Fractals.
- The final opening scene/capture and curated feature selections within the broad structure.
- Which native scene variants are worth publishing first; illustrative placeholders never imply a new runtime feature.
- Hosting and the publication/cutover endpoint. The local implementation uses Astro.

The [implementation brief](cubey-site-plan.md) and
[progress record](cubey-site-implementation-progress.md) carry this design into
the user-launched implementation goal. Public release remains separately scoped.

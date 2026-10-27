# Cubey presentation-site implementation brief

Date: 2026-10-24

Status: this implementation brief and review preview are complete. Public release
and README cutover followed in the
[implementation progress record](cubey-site-implementation-progress.md); the
[site guide](../../site/README.md) owns current behavior and publication details.
Study A's dark visual direction is user-selected. The subject/page structure
below remains the reviewed design record, not a current release checklist or a
claim that every label, capture or release choice was approved.

[Open the workshop-aligned design review and navigable mockups](http://starship:8787/reports/6339d2350e5953a016b2dbc209720f5f93e78b8664646f2a7f6b1c1b58657922/).

The [structure specification](cubey-site-structure.md) owns the navigation and
page models. The [content map](cubey-site-content-map.json) records twenty-one
presentation entries, their fourteen native owners, proposed variants, source
contracts and scope limits. The [design-loop record](cubey-site-design-progress.md)
records the work and bounded verification.

The linked mockups now carry the author's context through Home, About and
connected demo pages. The [alignment review](cubey-site-workshop-alignment.md)
records the changes and bounded checks. Public wording and final shots remain
reviewable proposals within the mostly accepted design direction.

## User direction

The site should use the freedom of a proper website to present Cubey as a whole.
The README highlights are useful media inputs, not the roster or information
architecture. The user explicitly called for broader simulation (Water, Fire,
Explosion and 2D), base Terrain before water, Ocean, Atmosphere/lighting/day-night/
clouds, and glTF's importer/renderer capabilities rather than a composite
helmet/terrain overview.

The content sequence remains:

1. Showcase current demos and what looks good.
2. Explain techniques through visual content and concise supporting prose.
3. Tell the development journey, including experiments and failed approaches.

This brief starts with showcase. Deep technique chapters and development stories
remain later editorial layers. Native construction playback can be a compelling
result demo without requiring the development-history narrative.

Use properly authored component/page layouts and rich media. Markdown may remain
useful for some prose; it does not determine the visual structure. Keep build/run,
API/contracts, qualification and raw history in their repository authorities.

## Project identity and editorial tone

The author's framing is a personal graphics/simulation playground and workshop,
and the engine that supports it. Today's Cubey is a new project in the spirit of
the original, rather than a restoration of its code. The original C++/OpenGL
learning project already followed the same feedback loop: build an engine to
try an idea, learn from the experiment, then improve the engine.

The reboot brings a decade of software engineering experience, including more
than five years in graphics, a Vulkan renderer and collaboration with Codex.
Familiar subjects such as fluid simulation and SDF still invite exploration.
Codex makes it practical to follow more of those interests in depth and refine
the supporting foundation. Projects such as Atmosphere also contribute reusable
capabilities back to the engine. Keep the continuity of purpose distinct from
code continuity; do not suggest that careful engineering began only with agents.

Topic selection remains curiosity-led. Familiar subjects such as glTF and fluid
simulation sit alongside whatever caught the author's interest: ocean, clouds,
terrain and more. Fluid work branched in several directions; procedural terrain
became a deep exploration. Do not rewrite that history as a planned product
roadmap or a curriculum. The subject directory is a useful way to browse the
results now; it does not imply a predetermined development sequence.

Connections emerged through the work. The 2.5D fluid exploration improved the
terrain-water experience; atmosphere/cloud work became useful lighting and
backdrop for materials. Explain such connections with a short caption and a
related-demo link where there is a concrete current example. Preserve the
specific integration boundaries in the content map; sharing a foundation does
not imply every experiment is combined into one scene or renderer.

The author owns the interests, direction and judgment, with coding agents as
development collaborators. The approach aims to understand and implement
established techniques carefully. Checking assumptions, refining results,
restarting an approach and cutting unsuccessful branches are all part of the
work. Novel mathematics or original research are not the project's objective.

Use a personal, curious and technically precise voice. Mention coding agents
plainly in the introduction/About text. Demonstrate care through selected
comparisons, useful explanations and honest limits rather than defensive claims
about AI or broad claims of correctness. The site should feel like visiting the
author's workshop and seeing what has been made and learned.

Opening copy used in the aligned mockup, for review:

> A graphics workshop.
>
> Rendering and simulation experiments, built with coding agents. I follow what
> catches my interest and build on what I learn.

For the first showcase release:

- Home gives this identity in a short introduction alongside the leading render.
  Selected motion/material/form media leads into two visual connection sketches;
  the concise origin link follows. Keep the origin/process story out of the way
  of viewing the work.
- About tells the concise origin, the change in scale/process with coding agents,
  and the reciprocal relationship between experiments and shared foundation.
  The mockup pairs native Ocean/Guardian imagery with the personal account and
  a small experiment/foundation feedback figure. It creates no dated timeline.
- Demo pages can include one useful connection beside relevant imagery, such as
  fluid work feeding Terrain Water or sky/cloud work supporting materials.
  Use captions and ordinary related links. Terrain links back to shallow water;
  shallow water explains the relationship beside its existing related cards.
  Atmosphere and Materials link to each other. Keep the recorded SynxFlow result
  distinct from the builtin solver; retain the existing environment/optics limits.
  No extra navigation layer or dependency-graph interface is needed.
- Technique explanations and detailed development journeys remain later work.
  Preserve failed branches and restarts in their evidence records without
  making the first release document every experiment.

## Recommended structure

Global navigation is compact: **Demos, About, Source**. Home provides visual
curation with compact subject links; Explore owns the complete visual directory.
Simulation, Land/sea/sky, Objects/surfaces and Further explorations are browse
bands, not new URL levels. Seven subjects are primary; Fractals remains quieter.
Keep direct scene/chapter links and remove internal content counts/default filters
from the public flow.

| Subject | First-release presentation intent |
| --- | --- |
| Simulation | Six peers: Water 3D, Fire 3D, Explosion 3D, Water 2D, Smoke/dye 2D and retained builtin shallow water. All six visible, with dimension badges. |
| Terrain | One visual experience: terrain shape/material/detail/shadows, then the distinct terrain-water chapter. |
| Ocean | A focused surface experience with sea state, whitecaps and sky/light views; no redundant single-demo overview page. |
| Atmosphere & light | Day/night, clouds and celestial sky as three visible, scrollable chapters. |
| Materials | Separate mesh and SDF workshop front doors sharing recipes; native material specimens and solid/interior views. |
| SDF forms | Guardian sculpture and authored robot/construction as visual experiences. |
| glTF rendering | Visible chapters for materials/light, transmission/volume/dispersion, animation/deformation and texture/detail. Loading/formats stay secondary. |
| Fractals | The smaller procedural-field experiment remains discoverable, without requiring equal homepage prominence. |

Eight subjects and twenty-one entries are the current design map, not a rigid
page/capture quota. Several entries are sections or views of one page. The
native-project count and the number of public pages are different concepts.
Do not silently narrow this structure to match the seven existing gallery films.

The old six-destination recommendation and optional-glTF framing are superseded.
The earlier [dark/light review](cubey-site-design-review.md) remains historical
visual evidence; only its selected dark direction carries forward as a decision.

## Page and content contracts

Use four primary content records:

- Subject: visitor purpose, editorial order and its collection/experience model.
- Demo or feature: visual result/question, canonical route, native owner,
  selected media, concise technique cues, scope and related work.
- Variant: named scene/material/view with real media, provenance and a caption.
- Media: source revision/recipe, bytes/hash, derivative parent, license, role,
  framing, dimensions and public-readiness state.

Optional connection annotations explain why particular demos are related. The
version-4 content map records the two selected relationships, participating
entries, source evidence, display contexts and scope. It also carries the
workshop identity and selected related-link captions. These are editorial
relationships, not additional subjects, a runtime dependency graph or public
configuration controls.

A renderer can supply several feature showcases; one film can support a scene
without defining the subject's identity. Keep primary entry ownership canonical
and use related links rather than duplicating maintained content. For example,
SDF material fields live under Materials and authored forms under SDF, even
though both use `projects/sdf`.

Use collections for Simulation and authored SDF scenes, one ordered chapter
experience for Terrain, and direct rich experiences for Ocean, Atmosphere,
Materials, glTF and Fractals. Give independent scenes stable demo pages where useful. Named
sections or saved-view states can provide deep links for feature tours.
The mockup's generic `demo.html?id=...` adapter is design scaffolding, not the
production routing recommendation.

The [refinement review](cubey-site-refinement.md) defines the reduced interaction
budget: visible chapters, normal links, film controls and two purposeful native
comparisons. Do not recreate generic feature tabs or preset panels.

Each demo starts with media and concise copy: what the viewer sees, a few useful
technique cues, related experiences and source/run access. Put detailed scope,
contracts, provenance and extension identifiers in optional secondary panels.
Keep runtime setup, logs and qualification language out of the primary visual
flow. Do not add empty Technique/Journey tabs in V1.
Where related experiments share a concrete mechanism or result, explain that
relationship in the link caption rather than presenting arbitrary related cards.

## Visual and interaction direction

Retain A: charcoal surfaces, warm white type, restrained olive details, generous
imagery and colour supplied by the native renders. Subject pages have their own
compositions within this shared language. Compact headings keep imagery visible
on narrow screens; the subject name remains clear in navigation/breadcrumbs.

Home is an editorial opening plus subject paths and selected work, not a fixed
reel or a list limited to current attachments. Ocean is the proposed opening
subject. Simulation, materials, sculpture and atmosphere provide broader routes
below it. Homepage emphasis and final hero shot remain reviewable choices.
The aligned composition uses each native homepage image once: SDF material and
Guardian imagery form the object section; the mesh-material image appears in
the sky/material connection. Avoid adding another result card where an existing
related-work card can carry the explanation.

Preserve full native wide frames for films/tanks/landscapes and square views for
sculpture where appropriate. Review any thumbnail crop against the complete
image. A poster is a separate presentation choice from the last frame of a film.

Use poster-first players, ordinary play/pause/seek/fullscreen and one active film.
The existing eight-second lighting arcs are not designed as seamless loops.
Keep important copy outside changing footage. Pause media on navigation and when
switching views. Keyboard operation, visible focus and reduced motion are part
of the interaction design.

Most content needs no selector. Use visible chapters and direct links, with
immediate anchor navigation. The two proposed selection experiences are the
matched wood-cut states and Guardian marble/limestone on the hero. Both need
actual media and an obvious visual purpose. The variant lists are editorial/
capture ideas, not a UI specification. Omit unavailable controls from release;
never relabel one reference film as several states. Preserve meaningful selection
on reload, pause motion for comparison, and keep captured playback distinct from
a browser-native renderer/editor.

## Media plan and release coverage

The design study used selected native inputs for eight entries and schematic
placeholders for thirteen. The implementation now selects real native web media
for all twenty-one entries, recorded in `site/media/manifest.json`: original
captures, parent hashes, current/historical identities, imported model notices
and web derivatives. All six simulation demos are included. No placeholder is
used in the built site.

New captures use existing native routes and one serial GPU lane; they do not
expand renderer capability. Representative glTF fixtures have pinned bytes and
per-asset credits. The Fox clip and separate morph cube show distinct mechanisms.
The source/outlet builtin flow recording remains distinct from the recorded
SynxFlow Terrain Water result. Historical dry Terrain retains an unknown captured
Git HEAD and a known native binary/input identity; it is not relabelled current.

The variant lists remain editorial possibilities rather than a requirement for
dozens of captures or public controls. New variants need their own selected media
and provenance. The table below retains selection intent for future curation.

| Asset work | Selection/capture intent |
| --- | --- |
| Simulation | Keep current Water/Fire films; select or capture Explosion, 2D water/dye and builtin shallow-water views showing their own effects. Motion is especially useful here. |
| Terrain | Select a dry terrain overview and useful detail/shadow views before the wet landscape film. Do not use the helmet composite as the base terrain identity. |
| Atmosphere | Select standalone day/twilight/night, cloud-weather/light and celestial views. Retain surface-only cloud scope. |
| glTF | Choose representative licensed feature assets for material layers, optics, motion and texture handling. A general model orbit does not prove each feature. |
| Materials/SDF | Retain corrected PBR media, distinct SDF material cut views, accepted guardian appearance and corrected robot history. Keep their identities and source limits. |
| Ocean/Fractals | Curate sea-state/light views and a suitable native fractal view; avoid implying unsupported ocean features or live browser rendering. |

Keep source/media provenance and per-asset attribution through derivatives.
The existing Damaged Helmet media is a CC-BY-NC-4.0 exception to Cubey's MIT
source/original media. New imported examples need their own checked notices;
do not infer their media license from the renderer source license.

Construction playback does not prove low-latency sculpting. Terrain rendering,
external/live SynxFlow and recorded states remain distinct. Shared mesh/SDF
material recipes do not prove equivalent optical transport. glTF's supported
required-use set is thirteen bounded extensions, not complete glTF compatibility
or certification. Keep these distinctions concise where they affect a visual.

## Implementation milestones

At this plan's closeout, the user had launched the implementation goal. This was
its execution sequence; later publication/cutover results are in the
[progress record](cubey-site-implementation-progress.md) and current
[site guide](../../site/README.md).

1. Adopt the structure/release scope and implementation stack. Astro 7.4.2 is
   selected for the authored static site; work is on the scoped local branch.
   Hosting/public endpoint remain open. Preserve inherited commits/concurrent
   work and maintain the selected-media ledger and named capture gaps.
2. Select/capture and package the agreed native media. Use one GPU job at a
   time, retain receipts and completed outputs, and investigate failures before
   retrying. Do not reopen sculpture/material realism, solver research, GI,
   training or new rendering capabilities to fill a presentation slot.
3. Implement the component-authored site, canonical routes, visual subject
   directory and subject-specific page models. Use tracked public assets so a clean
   site build needs no Vulkan, CUDA, native build, private host or ignored output.
   Astro components now implement the page models; no browser UI framework is
   required for the existing media/comparison behavior.
4. Fill every included entry with finished media/copy and verify the visitor
   scenarios in the structure specification. Check responsive layout, keyboard,
   media/state recovery, routes, attribution, source availability and loading.
   Establish a measured byte/loading budget from the chosen production assets;
   do not infer performance from this unoptimised mockup export.
5. Deliver the selected review/release endpoint. Publication and README cutover
   require their selected scope. Publish a working site before removing the
   gallery; verify the final public revision and served media, then apply the
   compact README/project-guide migration with appropriate recovery points.

Maintain a durable progress/evidence record and scoped local checkpoints during
that run. Routine choices within the adopted design should not need user replies.
New failures or changed requirements justify revisiting completed work; arbitrary
polishing after the acceptance audit does not.

## Implementation acceptance and delivery boundaries

- The included subject/scene breadth follows the selected release plan, with
  Explosion, 2D, base Terrain, standalone Atmosphere and feature-led glTF given
  their intended identities. No hidden six-film roster limit.
- Each included page/state has honest native visuals, useful captions, correct
  source/run routes and media attribution. No public placeholder ships without
  an explicit scope decision; no scene is silently removed for convenience.
- Homepage, Explore, collections, rich subject experiences and scene pages form
  coherent visitor paths. Deep links and saved state survive reload.
- Media remains prominent and readable on desktop/mobile. Player controls,
  focus, keyboard/touch operation and motion preferences work. Visual/user
  acceptance remains separate from automated checks.
- Home/About identify the personal workshop, the author's role and coding-agent
  collaboration without implying a fixed product roadmap. Selected connections
  between demos explain actual reuse; detailed journeys remain a later layer.
- Initial viewing does not fetch every film/master; images have suitable
  derivatives and dimensions, and lower-page media is deferred. No font CDN is
  required. Clean static delivery is a hosting property, not a Markdown constraint.
- Documentation/layout/media gates stay meaningful. At the planning baseline,
  the media checker required the five root README players. The later README
  cutover migrated that contract while preserving hashes, attribution, source
  navigation and native recipes. Test native code only when native behaviour
  or capture tools actually change.
- Recheck exact local/remote refs, tracking/dirty state and complete push range
  before any later source publication. At this design checkpoint, local `main`
  had twelve inherited commits beyond local `origin/main`; this design work did
  not publish them.

At plan closeout, the proposed destination remained an in-repo site at
`dev.byebyebryan.com/cubey/` using GitHub Pages, and its endpoint was not yet
configured. The later public release is recorded in the implementation progress
record; the current endpoint and operating details are in the site guide.

## Evidence and retained context

The workshop-alignment report contains thirteen navigable mockup/review pages. Static
content/source/route checks account for eight subjects, twenty-one entries,
fourteen native owners and all thirteen supported required-use glTF extensions.
The alignment checks all twenty-one route identities and twenty-eight narrow
layout cases at 320/390 pixels, including About and shallow water. Both selected
connections are navigable in both directions. Keyboard chapter navigation,
native playback, the two comparisons and reload/state recovery pass. Source and
native-asset identities remain unchanged. These are mockup design receipts,
not a production/native qualification or artistic pass.

The [SDF thread closeout](sdf-thread-closeout.md),
[original site handoff](cubey-site-handoff.md) and
[first visual study](cubey-site-design-review.md) retain the prior context.
At this design-plan checkpoint, the README gallery remained unchanged and source
push/public hosting/cutover were still separate. Later implementation and
publication are recorded in the progress record and site guide. The
previous broad structure/refinement reports remain historical evidence; at this
design-plan checkpoint, the linked workshop-alignment report was the latest design
review.

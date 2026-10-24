# Cubey workshop-alignment design review

Date: 2026-10-24

Status: design-only goal complete. The user is mostly comfortable with the dark
design and supplied the project's fuller identity. This pass carries that context
through the actual mockups and planning authorities. Final wording/shots remain
proposals; production implementation has not started.

[Open the aligned design review and navigable mockups](http://starship:8787/reports/6339d2350e5953a016b2dbc209720f5f93e78b8664646f2a7f6b1c1b58657922/).

## Design conclusion

Keep the visual subject directory and broad demo coverage. Frame Cubey as the
author's graphics/simulation workshop: curiosity chooses the subjects, coding
agents expand the scale/process, and experiments and foundation improve one
another. The collection can have a clear presentation without implying that its
development followed a predetermined roadmap or curriculum.

## Changes in the mockups

- Home introduces “A graphics workshop” and the coding-agent collaboration in
  first-person copy beside the native Ocean image. Motion, materials and authored
  form remain prominent before the connection/context section.
- About explains the old-engine revival and earlier C++/OpenGL learning, current
  expanded work with coding agents, the author's direction/review, and careful
  application of established techniques. Refinements, restarts and cuts are part
  of the account. A small two-way figure explains experiment/foundation feedback.
  Native Ocean/Guardian imagery keeps the page visually connected to the work.
- Two visual connections appear on Home and are explained on About and the
  relevant demos: 2.5D fluid exploration into terrain-water work, and shared
  atmosphere/cloud services supporting material lighting/backdrop.
- Navigation stays Demos, About, Source. Deep technique explanations and detailed
  development journeys remain later layers. No extra Foundation/Journey menu,
  dependency browser, generated preset controls or dated timeline was introduced.
- The version-3 content map owns the editorial identity, connection membership,
  evidence/scope, display contexts and selected related-link explanations.

Review removed two avoidable repetitions: shallow water now explains its
connection above existing related cards rather than repeating Terrain Water in
another large image; Home uses the SDF-material image in its object section and
the mesh-material image in the sky connection, so native images appear once.
The homepage wording also keeps 2D demos and surface flow distinct.

## Connection meanings and evidence

| Connection | Meaning and current boundary | Sources |
| --- | --- | --- |
| Water experiments meet terrain | The 2.5D exploration led into terrain-water work, with shared terrain rendering carrying useful changes between projects. The featured film replays SynxFlow fields; the builtin shallow-water demo remains a distinct solver/scene. | [Current Terrain Water reference](../../projects/terrain_water/current_reference.md), [Terrain guide](../../projects/terrain/README.md), [development map](../../studies/fluid_25d/notes/development_map.md). |
| A sky becomes a light source | The material workshops reuse shared atmosphere/cloud environment services. Existing environment and mesh/SDF transport limits remain; no GI or identical-optics claim. | [Material workshops evidence](material-workshops-evidence.md), [environment evidence](sdf-environment-presentation-evidence.md), [cloud architecture](../architecture/cloud-rendering.md). |

The paired placeholder/native views on Home illustrate editorial relationships;
they are not matched experiments or new native results. Thirteen entries still
use explicit schematic placeholders; eight have selected native review inputs.

## Verification and delivery

- All eight subjects, twenty-one entries, fourteen native owners and thirteen
  supported required-use glTF extensions remain covered. Native asset hashes,
  source paths, related-entry membership and labelled placeholders pass.
- All twenty-one routes show their expected scene/chapter identity. Twenty-eight
  narrow-layout cases across fourteen views pass at 320/390 pixels without
  horizontal overflow or broken loaded images. Home's leading render enters
  the first viewport at both widths; About also retains its opening native media.
- Both new connection paths work in both directions. Keyboard chapter links,
  native film loading/playback, wood-cut pause/selection/restoration and Guardian
  hero selection/restoration pass. No duplicate homepage images or repeated
  Terrain Water card was introduced.
- The final report packages five browser screenshots and the static/browser
  receipts around the checked mockups: 111 files, 22,304,200 bytes. Every served
  file matches local bytes from Starship; sixteen core mockup/data/style/script
  files match the browser-checked candidate. Delivery is recorded under the
  authoring root. Technical design checks do not imply final artistic acceptance
  or production/native qualification.

Authoring and receipts:
`outputs/cubey-site/workshop-alignment-2026-10-24/`.
The [previous structure refinement](http://starship:8787/reports/79d2574724a79c2bfe6bd3bfbf6151d8ab48c7259cd271805967cb1f97a28de9/)
remains available for comparison. The earlier architecture and reduced interaction
budget carry forward; this pass changes their framing and connective content.

The [implementation brief](cubey-site-plan.md),
[structure](cubey-site-structure.md), [content map](cubey-site-content-map.json)
and [progress record](cubey-site-design-progress.md) incorporate this goal.
Remaining choices are final public wording/homepage emphasis, selected native
captures and later production stack/hosting/release scope. No native renderer
changes/captures, production framework, README cutover, source push or public-site
deployment occurred.

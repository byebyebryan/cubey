# SDF thread closeout

Date: 2026-10-24

Status at the 2026-10-24 closeout: implementation and evidence were captured and
this thread closed as an active work stream. The site migration was the next
direction at that point; it was later implemented and published. See the
[site implementation record](cubey-site-implementation-progress.md),
[current site guide](../../site/README.md) and
[published SDF chapter](cubey-site-sdf-story.md#published-outcome). The original
handoff remains at [cubey-site-handoff](cubey-site-handoff.md).

## What this thread delivered

Cubey's SDF work began as a reviewed port of projectR, then progressed through
procedural test objects, authored content, materials, shared lighting and two
material workshops. Exploration and education are valid outcomes; a novel
renderer or a complete sculpting product was never required.

| Stage | Retained outcome | Main record |
| --- | --- | --- |
| projectR review | Architecture, the earlier per-brick renderer and the original commit-message journey; donor branches/source assumptions distinguished from native evidence. | [Architecture](projectr-architecture-review.md), [history](projectr-development-history.md), [port exploration](sdf-projectr-port-exploration.md) |
| Procedural port | Deterministic additive/subtractive box recipes, sparse sampled bricks, checked traversal, hit depth, construction playback, hard self-shadows and shared sky. | [Implementation](sdf-implementation-evidence.md), [procedural closeout](sdf-procedural-prototype-closeout.md) |
| Authored robot | Recognizable mechanical content, grouped CSG/materials, edit-by-edit playback, added detail and corrected joint/leg frames. | [Authored content](sdf-authored-content-evidence.md), [history](sdf-authored-history-evidence.md), [joint review](sdf-robot-joint-review.md) |
| Sculpture experiments | Guardian selected as strongest; organic shape, scripted strokes, mirrored atomic operations and roughly one thousand primitive leaves. Driftwood dropped from active presentation. | [Sculpture study](sdf-sculpture-study-evidence.md), [organic guardian](sdf-guardian-organic-evidence.md), [mirror](sdf-mirror-evidence.md), [guardian closeout](sdf-guardian-closeout.md) |
| Materials and environment | Settled limestone/subtle marble, matte clay and distinct ceramic/metal/glass studies; shared atmosphere, sun, reflection probes and frozen clouds. | [Material study](sdf-material-study-evidence.md), [contrast](sdf-material-contrast-pass.md), [environment](sdf-environment-presentation-evidence.md) |
| Shared content/workshops | Geometry-independent recipes and CPU/GLSL evaluation; mesh PBR and SDF workshops share twelve materials, bounded controls, persistence and useful specimens. | [Material architecture](../architecture/material-recipes.md), [workshop evidence](material-workshops-evidence.md), [library evidence](material-library-evidence.md) |
| Presentation checkpoint | Native PBR group and SDF wood-cut presets, two eight-second clips, posters, 1080p masters and an inspectable construction-state report. | [Presentation evidence](material-presentation-evidence.md), [committed media](../media/material-workshops/README.md) |

The [three-demo roadmap](material-demo-roadmap.md) keeps mesh PBR materials,
SDF materials and authored SDF construction distinct. Their reader-facing pages
can be separate even though the latter two use the same native SDF executable.

## Accepted boundaries

- The procedural stick/carved-ball scenes remain useful technical prototypes.
  They do not replace a recognizable authored subject or prove an exclusive
  advantage over mesh methods.
- The robot is the successful first authored experiment. The guardian's organic,
  mirrored form and limestone/subtle Balanced marble are settled by user review.
  The guardian has 1,011 leaves, 2,021 instructions and 65 authored operations;
  sampled stroke segments are not independent artistic decisions.
- The twelve-material library is accepted for its bounded visual scope. Wood,
  rust and frosting provide useful distinct cues; cloth is coarse weave and
  kraft remains a stylized directional-fiber surface. Further marble realism
  is deferred. The newest presentation shot choices still await user review.
- Keep hard self-shadows and the shared atmosphere/cloud/environment services.
  Terrain and a ground plane are not requirements of the settled SDF scenes.
- Authored primitives are box, sphere, capsule, cylinder, torus, capped cone and
  rounded cone, with union/subtraction and smooth variants. Scripted authoring,
  mirroring, saved history and replay are implemented; freehand desktop sculpting
  is separate work.
- Material edits reuse geometry caches. Geometry edits still rebuild the field
  synchronously. The current default field allocation is about 871 MiB, with
  authored budgets of 1,024 leaves, 2,048 instructions and sixteen materials.
  Guardian replay is not evidence of low-latency continuous brush editing.
- Glass is bounded environment transport. SDF measures in-medium segments and
  approximates rough spread; mesh uses its forward thickness/refraction route.
  Shared recipes do not establish equivalent transport, nested media or caustics.

## Qualification and publication state at the 2026-10-24 closeout

Before these closeout notes, local `main` was clean at `5525445f` and eleven
commits ahead of the verified GitHub `main` at `b72c7c5c`. All SDF/shared-material
runtime work, scenes, tools, documentation and the selected presentation media
were already committed. This wrap-up adds documentation; it does not push.

The presentation evidence records eighteen passing scoped CTest entries,
35 passing workshop/presentation CLI recipes, native Vulkan/input/resize checks,
video decoding, browser interaction/playback and Starship/Carbon delivery checks.
Those are dated, scoped receipts, not a repository-wide runtime qualification.
This closeout rechecks committed media hashes and documentation rather than
rerendering settled material/sculpture matrices.

The latest presentation report at this closeout was
[Material, light, and a cut](http://starship:8787/reports/5ddba21d5b0e6a6e2ff2a88ccb788cce0a4e2c0527ea8c5bc89b4a0c0bf8d71e/).
It is hosted on the shared Starship report server. Its publication is separate
from GitHub source push and a public Cubey site. At this checkpoint the root
gallery still contained the five earlier highlights; the new material films had
not been promoted there. Later site publication and README cutover are recorded
in the linked site implementation and guide.

The six selected material media files match their committed hashes. Their
[manifest](../media/material-workshops/manifest.json) and
[capture recipes](../media/material-workshops/capture.json) retain clean PBR
source `1654e825` and SDF source `37838ac8`. The PBR recapture includes the shared
camera transform correction; do not substitute the earlier report/media revision.
Raw receipts and larger comparisons remain under `outputs/material-presentation`,
`outputs/material-library`, `outputs/material-workshops` and `outputs/sdf`.
Those ignored outputs are retained review evidence, not uncommitted source.

## Deferred experiments and reopen conditions

The [retained experiment list](sdf-projectr-port-exploration.md#retained-experiments-for-later)
captures pretrained image-to-shape/SDF content, fitting a neural SDF to an
existing object, constrained image-to-SDF training and broader fine-tuning.
No weights, inference pipeline or training run were qualified by this work.
Generated geometry would not recover an editable primitive history by itself.

Desktop/VR authoring, incremental field updates, GI, terrain coupling, deeper
rough-refraction work and new sculptures require separately selected goals.
Refresh a guardian construction film only as an explicit presentation task:
the older complete film matches the current geometry/history but uses an earlier
marble appearance. A finished object alone does not explain the saved SDF process.

Reopen runtime work for a concrete failure, a selected new experiment or a
presentation requirement demonstrated by the site work. At this closeout, the
next owner was to start from the committed results and their scoped limitations,
then develop the site migration with the user. That migration was later
completed; see the site implementation and publication records linked above.

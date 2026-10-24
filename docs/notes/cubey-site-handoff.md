# Handoff to cubey-site

Date: 2026-10-24

Recipient: the existing `cubey-site` thread in `/home/bryan/code/cubey`.
Source: the long-running SDF/material thread. Read the
[SDF closeout](sdf-thread-closeout.md) for the complete stage/evidence map.
This transfers context for the site's next planning phase; it does not implement
the migration or request a push/deployment.

## User direction

The user wants an aggressive shift to a full presentation site. Move most of
the current demo gallery from the README onto the site, including clips,
posters, descriptions and the project directory. The site becomes Cubey's main
public home for seeing and understanding the experiments. A small extra landing
page pointing back at the existing README gallery is insufficient.

Cubey is about exploration and experimentation. Explain what was tried, how it
works, what changed and what was learned, as well as showing finished results.
The long SDF journey is worthwhile even without a novel renderer. Native media,
construction playback and inspectable comparisons already make useful teaching
material. Native technical success and artistic acceptance remain separate.

The README should become a compact repository entry: short description, prominent
site link/optional cover, quick build/run, developer-doc links and licensing.
Move audience-facing project stories out of long project READMEs as appropriate.
Build/API/contracts/raw qualification documentation can stay in the repository;
Markdown can still author site content. Maintain one source for each piece.

Use [Codex Plus](https://dev.byebyebryan.com/codex-plus/) and
[Fleet Plus](https://dev.byebyebryan.com/fleet-plus/) as existing presentation
references. The suggested destination is `dev.byebyebryan.com/cubey/` using a
small static site inside this repository. That URL, GitHub Pages/build approach
and exact site architecture are recommendations to settle in the new thread,
not a configured deployment or a selected framework.

## Content and navigation to plan

- A strong visual opening and curated homepage, with a full experiment/project
  directory. Root README currently describes fourteen runnable projects; cover
  the collection, not just the two new material films. Keep active experiments,
  internal validation, paused studies and historical evidence distinguishable.
- Project stories combining the visual result, exploration question, mechanism,
  iteration/limitations and a route to run the native preset/source.
- Rich explanations drawn from report controls: matched material views, cut
  states, component comparisons and authored construction/history. Native clips
  and saved snapshots already support these without a browser renderer port.
- Preserve the [three-demo distinction](material-demo-roadmap.md): ordinary-mesh
  PBR materials, SDF materials and authored SDF construction. The latter two share
  `projects/sdf`; they need not become separate rendering implementations.
- Publish selected media/comparisons with the public site. Internal Starship
  report URLs are useful references and review history, not the only way for a
  public visitor to see the work.

Choose the detailed information architecture, migration inventory, visual
language and publication workflow with the user. The earlier bounded
[material presentation plan](material-presentation-plan.md) describes existing
clips/presets; its deliberately limited scope is not the scope ceiling for the
site migration. Completion should include the live site and README gallery
cutover, with preservation of the selected media and evidence identities.

## Existing material and SDF content

| Entry point | What to reuse / what to preserve |
| --- | --- |
| [Root README](../../README.md), [five-highlight media package](../media/showcase/README.md) | Ocean, glTF + Terrain, Water 3D, Fire 3D and Terrain Water; committed videos/posters, exact capture recipes and published attachment mapping. |
| [New material media package](../media/material-workshops/README.md) | Two eight-second 720p/60 clips, 1080p masters, posters and provenance. `pbr.mp4` shows wood/rust/cloth/bronze; `sdf.mp4` shows the wood through-cut. |
| [Material presentation evidence](material-presentation-evidence.md) | Final report, native launch presets, same-camera solid/tool/cut snapshots, measured costs and validation/delivery receipts. |
| [Material library evidence](material-library-evidence.md) | Twelve shared recipes, 57 native stills, three short films, mesh/SDF views, frosting strips, component views and resolved snapshots. |
| [Guardian closeout](sdf-guardian-closeout.md) | Accepted organic mirrored sculpture, limestone and restrained Balanced marble. It has 1,011 leaves and 65 authored operations. Modeling and realism are settled. |
| [Mirror evidence](sdf-mirror-evidence.md), [procedural material evidence](sdf-procedural-materials-evidence.md) | Saved construction and mirrored-stroke presentation. The older complete 80-second guardian film matches current geometry/history but uses earlier marble; label it or capture a new film if selected. |
| [Robot joint review](sdf-robot-joint-review.md), [authored history](sdf-authored-history-evidence.md) | Successful first authored experiment and edit-by-edit story. Prefer the corrected field-kit pose and red subtraction previews. |
| [Procedural closeout](sdf-procedural-prototype-closeout.md), [projectR history](projectr-development-history.md) | Positive sticks to carved sphere to authored content; the original brick-renderer journey is documented from source/commit messages. These are useful learning chapters, not the strongest finished hero assets. |

The latest two-story review is
[Material, light, and a cut](http://starship:8787/reports/5ddba21d5b0e6a6e2ff2a88ccb788cce0a4e2c0527ea8c5bc89b4a0c0bf8d71e/).
The full [twelve-material report](http://starship:8787/reports/97f536f0e315dd8cb3fa726846f782fbb832ea85ddefa3b7a35b956b33e9d63f/)
and [original workshop report](http://starship:8787/reports/2fcdfdaa2541d39e220a1cc6533bb5213a8d563c429e06bbe674b5f2bc2a7d0c/)
retain broader comparisons. The guardian closeout links current accepted stills
and the older complete film separately.

## Source, artifacts and publication

At intake, local `main` is clean at `5525445f`, eleven commits ahead of GitHub
`main` at `b72c7c5c`; these notes add a separate documentation commit. The earlier
SDF/shared-material milestones are pushed through `b72c7c5c`. The eleven later
commits contain the presentation plan/runtime/media/report work. Preserve them;
recheck current HEAD/status before editing. No source push is included in this
handoff. There is no pending source work owned by the departing thread after
the documentation commit and handoff send.

Useful local inputs:

- `docs/media/material-workshops/`: six committed media files plus manifest and
  capture recipes; clean capture identities are PBR `1654e825` and SDF `37838ac8`.
- `outputs/material-presentation/report-final/`: complete sixty-file export,
  9,411,230 bytes, including seven state stills, two posters and four video files.
- `outputs/material-presentation/qualification/`: native commands, input snapshots,
  profiles and receipts; `outputs/material-library/report/` and
  `outputs/material-workshops/report/` hold the larger report bundles.
- `outputs/sdf/`: guardian, robot, material, history and procedural investigations.
  Exact directories and source identities are linked from their evidence notes.
- `projects/pbr_materials/tools/presentation_report.html` and the other report
  templates provide working interaction examples. Reuse their native assets and
  semantics in maintained pages rather than publishing the full output tree.

The final material presentation passed eighteen scoped CTests, 35 documented
workshop/presentation CLI recipes, native input/Vulkan checks, media decoding,
browser interaction/playback and full served-byte verification from Starship
and Carbon. Report hosting, GitHub push and a public site are separate states.
The new shot choices still await user review. GPU figures are dated 1080p/1×
RTX 5070 Ti measurements, with setup/field construction excluded from steady
costs; retain their scope when presenting them.

Cubey source/original media are MIT. The existing Damaged Helmet-derived glTF
showcase film/masters/poster are a media-only CC-BY-NC-4.0 exception with required
attribution recorded in the [showcase manifest](../media/showcase/manifest.json).
Carry that metadata into any public asset package.

No new materials, guardian remodeling, learned generation, GI, terrain background
or desktop sculpting work is required to build the site. Learned-content options
are already captured in the [deferred experiment list](sdf-projectr-port-exploration.md#retained-experiments-for-later).
Start the new phase from the user's presentation migration direction and the
committed results, then settle a concrete site plan.

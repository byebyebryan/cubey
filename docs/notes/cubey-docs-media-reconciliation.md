# Documentation and media reconciliation

Date: 2026-10-26

Status: post-site reconciliation of repository guides, historical notes and
tracked-media ownership. The [presentation site](https://dev.byebyebryan.com/cubey/)
owns the visual collection; the root README remains the compact repository
entry point with its approved composite banner. This pass changes Markdown,
not native behavior or selected site media. Two archival-media assertions are
updated to follow the new documentation owners while preserving their original
hash and publication-receipt checks.

Baseline: `50622640f4d7fa1ffcfc21bce773a69ca3db0d2b`. The inventory covers
345 repository-owned Markdown files, including 219 working notes, using the
same tracked-file boundary as the documentation gate. Vendored documentation,
ignored private outputs and build trees are outside that corpus. Classification
and source/reference review do not renew every historical runtime result.

## Current guides and retained records

The [documentation index](../README.md), [roadmap](../roadmap.md), setup guide,
design overview and changelog now distinguish current behavior, completed
presentation work and deferred experiments. Seven project guides link directly
to their public subject pages: SDF, PBR Materials, glTF Viewer, Terrain Water,
Ocean, Water 3D and Fire 3D. Old posters, film links and raw GitHub attachment
embeds leave those current guides; useful technical references remain local.

The [SDF guide](../../projects/sdf/README.md) now leads with runnable modes,
saved-history controls, material editing, optical limits and deferred work.
Its [technical reference and study record](../../projects/sdf/docs/technical-reference.md)
retains the detailed recipes, diagnostics and chronology. Existing README
anchors still lead to the appropriate entry or reference. Four smaller
project-local history pages retain the original glTF, Ocean, Water 3D and Fire
3D capture commands without duplicating the public gallery.

The [working-note index](README.md) gives the ten previously indirect-only
records explicit entries. Site design/intake notes now point to the implemented
site and release outcome; material proposals link implemented contracts and
evidence. Earlier Guardian candidates link the settled verdict, and the robot
plan links its completed detail/joint studies. Historical bodies and checkpoint
receipts remain dated rather than silently rewritten as present-day results.

The authority boundaries remain:

- [Shared material architecture](../architecture/material-recipes.md) and
  current native guides own implemented behavior. Pre-expansion research and
  V2 scene descriptions retain their original historical scope; they are not
  current material-library gaps or replacements for the V3 contract.
- The [first SDF web chapter](cubey-site-sdf-story.md#published-outcome) is
  delivered. The separately scoped native authored-content package and Guardian
  construction-film refresh remain deferred. Saved replay is not live brush
  editing; geometry rebuild remains synchronous and material updates reuse the
  geometry cache. Shared recipes do not establish equal mesh/SDF transport.
- Terrain Water presents external recorded SynxFlow fields; builtin Fluid 2.5D
  remains a separate solver experiment. Fractal 2D remains a native project
  despite its deferred public-site presentation.
- Source-pinned foundation/runtime evidence and sealed study narratives keep
  their original inputs, dates and manual-acceptance limits. The two top-level
  terrain study records now explicitly identify their dated checkpoint and
  current native owner.

## Media audit and retention decision

The new [tracked-media guide](../media/README.md) maps current site assets,
the README banner, original showcase archive and initial material-story
archive to their owners, provenance and retirement rules. The two archive
READMEs state their historical status and link current presentation owners.
The setup/tool guides distinguish the retained gallery integrity check from
the site's selected-media checks.

| Collection | Media files | Bytes | Decision |
| --- | ---: | ---: | --- |
| Selected site media | 121 | 28,781,222 | Retain current selections and ledger. |
| Original README showcase | 14 | 33,930,632 | Retain films, posters, native masters and publication provenance. |
| Initial material stories | 6 | 3,875,794 | Retain the earlier four-specimen PBR and wood-only SDF captures. |
| Approved README banner | 1 | 437,297 | Retain approved five-panel composition and recipe. |

All 121 site-media files are accounted for by the existing ledger, with no
missing or unlisted selections. The broader audit traces Markdown consumers,
manifest entries, capture/derivative references, preparation tools and checks.
No media file was established as disposable by removing current-guide embeds.

Four archive/site pairs are byte-identical:

| Archive path under `docs/media/showcase/` | Selected path under `site/public/media/` | Bytes per copy |
| --- | --- | ---: |
| `fire-3d.mp4` | `fire-3d-film.mp4` | 877,564 |
| `fluid-25d.mp4` | `terrain-water-film.mp4` | 1,429,568 |
| `ocean.mp4` | `ocean-surface-film.mp4` | 6,718,736 |
| `water-3d.mp4` | `water-3d-film.mp4` | 1,308,520 |

These account for 10,334,388 duplicate checkout bytes. Each pair already shares
one Git blob. Archive captures stay pinned while a current site selection can
change, so their different lifetimes justify retaining both paths. Removing a
path would require coordinated changes to provenance/reference graphs and
checks, without reclaiming a corresponding duplicate blob from Git history.
No binary deletion, history rewrite or new media encoding is selected.

Older GitHub attachments remain archival delivery references in their owning
records; the current site self-hosts its selected media. External uploads and
immutable Starship reports remain intact. Unique native masters, hashes,
capture receipts and imported-asset notices remain useful evidence. In
particular, the old Damaged Helmet composite retains its media-only
`CC-BY-NC-4.0` attribution boundary; it is absent from current site selections.

## Validation and publication scope

The reconciled corpus contains 352 owned Markdown files, including 220 working
notes and their index. All original anchors in the seven changed project
READMEs remain available, and all 61 baseline project-guide fenced code blocks
remain byte-for-byte present in their guides or supporting records. No native
command correction was required. The 249 protected files retain their baseline
SHA-256 identities, covering all 142 tracked publication/site media files,
existing media manifests/receipts, site implementation inputs and sealed study
records.

`ctest --preset dev-docs -E documentation_cli_recipes --output-on-failure`
passes all eight selected groups: project layout/rules, full Markdown
navigation, retained media integrity, documentation rules and the Fluid/glTF
presentation records. `git diff --check` passes. Supporting project details
live under their owning `docs/` directories, as required by the layout gate.
Integration receipts and the complete inventory remain under ignored
`outputs/docs-media-reconciliation-2026-10-26/`.

Eighteen current SDF/PBR guide recipes pass parser-only checks against the
existing release binaries, with display variables empty and Vulkan driver paths
disabled. These exit through configuration-template output without rendering.
The development SDF binary is older and rejects the workshop options; the other
twenty applications in the development manifest have no built executables.
Consequently the full `documentation_cli_recipes` gate is excluded from this
documentation-only pass. This does not establish fresh native GPU, GUI, solver,
performance or visual qualification.

The public site's known deployed source remains
`48fa1ed1c7c7d02d0d6892466fecc2dec0afeb42`, Pages run `40244161355`.
That release's receipt covers 148 served files and 29,120,488 bytes. This
documentation-only pass does not alter its build inputs or select a new site
deployment.

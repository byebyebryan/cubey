# Cubey structure and design refinement

Date: 2026-10-24

Status: this follow-on design review was complete before production began. The
recommendations were later implemented and the site published; see the
[implementation progress](cubey-site-implementation-progress.md) and current
[site guide](../../site/README.md). This document retains the mockup-era review.

The [workshop-alignment review](cubey-site-workshop-alignment.md) is the current
design package following the author's fuller project context. The browsing and
interaction refinements recorded here carry forward.

[Open the refined mockups and findings](http://starship:8787/reports/79d2574724a79c2bfe6bd3bfbf6151d8ab48c7259cd271805967cb1f97a28de9/).

## Conclusion

Keep the subject/demo/variant ownership model and broad coverage. Simplify the
public presentation: a visual subject directory, visible chapters, fewer repeated
pages and only purposeful media-backed selection. The earlier mockup still asked
visitors to operate too much interface before seeing the work.

## Findings and changes

| Priority | Finding | Refined design |
| --- | --- | --- |
| High | Flat catalogue mixed subjects, scenes and feature entries, exposing internal counts and nine filters. | A grouped visual directory with direct scene links. Editorial browse bands introduce no extra URL levels. |
| High | Feature tabs plus generic Overview/variant panels made the experience resemble configuration UI. | Visible Atmosphere, Materials and glTF chapters, ordinary section links and no generated preset panels. |
| Medium | Terrain overview and child pages repeated essentially the same media. | One ordered dry-terrain/terrain-water page with chapter anchors. |
| Medium | Home repeated the directory and gave Fractals equal subject-card prominence. | Media curation and compact subject links on home; a complete directory; quieter Further explorations for Fractals. |
| Medium | Six Simulation entries did not need another filtering task. | All six visible together with 2D/3D/Surface flow badges. |
| Medium | Layered materials was too narrow a label for unlit/emissive response and IOR. | Materials & light; move IOR into the optics mapping. All thirteen supported extensions remain accounted for. |
| Low | Guardian hero and a separate comparison repeated the same initial image. | Change the hero directly between marble and limestone. |
| Low | Animated chapter jumps did not reliably settle during navigation/reload in review. | Immediate native anchor jumps, verified by keyboard and reload. |
| Low | Alternating chapter columns could give text more width than imagery. | Keep media dominant on either side and first in the narrow-screen composition. |

## Presentation model

The directory groups:

- Simulation: Water, Fire, Explosion, both 2D demos and builtin surface flow.
- Land, sea and sky: Terrain, Ocean, Atmosphere & light.
- Objects and surfaces: Materials, SDF forms, glTF rendering.
- Further explorations: Fractals.

These are browse bands, not new canonical subjects or extra navigation steps.
All eight subjects, twenty-one content slots and fourteen native owners remain
reachable. The homepage's selected imagery does not cap the collection.

Primary interaction is watch, scroll and follow links. Retain custom selection
for two clear visual comparisons: matched wood solid/preview/cut states and
Guardian marble/limestone. Both have actual native images, keyboard operation
and restorable URL state. Comparing a static view pauses moving media.

The proposed variants remain a capture/editorial backlog. They do not require
forty-eight new public controls, a capture for every native setting, or a browser
renderer. Placeholder media stays explicit in design studies and must be replaced
or explicitly excluded by release scope before public implementation.

## Evidence

- Source-guided content audit still covers all fourteen native owners and all
  thirteen supported required-use glTF extensions. Native asset hashes, related
  links, source/route paths and placeholder labels pass.
- All twenty-one proposed content routes resolve without broken loaded images
  or horizontal overflow.
- Twenty-four narrow-layout cases across twelve page/scene views pass at 320
  and 390 pixels. Media retains the dominant desktop chapter column.
- Browser checks cover visible feature chapters, no glTF tab controls, keyboard
  chapter jumps, native playback, wood-cut/Guardian media identity and reload.
- The final report adds receipts/screenshots around the same checked mockup
  files. This is design verification, not production/native qualification or
  user artistic approval of every shot.

Authoring and receipts are retained under
`outputs/cubey-site/structure-refinement-2026-10-24/`.
The [previous structure study](http://starship:8787/reports/150ff20554307a90bc7a5f9b0b6d41f12cea4777bd582b56dffff9e75671d9f3/)
remains available for comparison.

The [structure specification](cubey-site-structure.md),
[implementation brief](cubey-site-plan.md) and
[content map](cubey-site-content-map.json) incorporate this pass. Final captures,
homepage emphasis and later stack/hosting/release decisions remain visible review
choices. No runtime changes, native capture, production site, source push,
public deployment or README cutover occurred.

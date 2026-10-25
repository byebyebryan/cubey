# Cubey showcase polish — 2026-10-25

This review starts from published source `b45de617448bb32ba1b45c4dbcd3062f56a0e901`.
It covers the full presentation site: copy, content hierarchy, navigation,
layout and styling. The accepted dark visual direction and personal workshop /
supporting-engine identity remain the baseline. Current demos lead; deeper
technique chapters and development journeys remain later editorial layers.

## Refinements

- Tightened Home, subject introductions and scene descriptions. Public scene
  notes now explain scope in ordinary language, while retaining the recorded
  SynxFlow / in-engine solver distinction, cloud viewpoint limits, glTF support
  limits and different mesh/SDF light paths.
- Removed repeated subject labels beside breadcrumbs and duplicate chapter
  source links. Breadcrumbs now have a navigation label and current-page state.
- Reduced the space before the material-library player, including the two
  renderer / lighting control groups on narrow screens. All twelve materials,
  both renderers and both lighting phases remain available.
- Made main navigation targets 44 pixels high, added an active-page underline,
  and refined heading wrapping, card spacing, focus and hover feedback.
- Reorganised Credits around six readable source groups, followed by an
  expandable complete capture table. All 88 capture credits, author/license
  values and fragment targets remain available. Browser fragment navigation
  opens the disclosure for a capture-row link.
- Retained the original YouTube recordings on About, their 2018 upload labels
  and direct watch links. They remain separate from the self-hosted native
  media ledger and its preview policy.

The public copy layer is `site/src/data/catalog.ts`; native identities and
technical evidence remain in their existing source records. There is no new
renderer, numerical change, native capture or generated media in this pass.

## Review evidence

The published 20-page baseline was reviewed through the browser. The refined
build was checked across 20 routes at five frame widths: 320, 390, 768, 1024
and 1600 pixels. These are desktop browser layout checks, not physical mobile
device acceptance. The measured inner widths account for the browser's scrollbar.
All 100 cases have one main heading, no page-level horizontal overflow and no
failed loaded images. Main navigation targets are at least 44 pixels high.

The smallest material-layout check exposed excessive content before the player.
The next refinement moved the player from 841 to 716 CSS pixels from the top of
that review frame. The early figure was a review candidate, not a measurement
of the published baseline.

Keyboard checks cover Guardian's material selector with visible focus, the SDF /
dawn material combination and the through-cut comparison. Real browser playback
covers the silent robot preview, inline activation of its 1280×720 film and
manual-playback priority. Credit deep links and the 88-row disclosure were
checked separately. The existing media-policy tests cover motion/data fallbacks
and cancellation; no new player script or playback policy was introduced.

All 119 pre-existing public media and thumbnail files match their baseline
hashes. The current collection remains seven subjects, 21 entries, 13 native
owners, 13 supported required-use glTF extensions and 15 film previews.

Review fixtures and detailed receipts are under
`outputs/cubey-site/polish-2026-10-25/`; those local fixtures are excluded from
publication. Final gates are `npm run verify` with the public origin/base and
the scoped documentation suite, excluding `documentation_cli_recipes`. Native
CLI qualification is a separate scope. Publication follows the existing Pages
workflow; verify the deployed source identity and served bytes separately from
local acceptance.

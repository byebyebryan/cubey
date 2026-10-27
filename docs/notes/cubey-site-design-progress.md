# Cubey site design-loop progress

Date: 2026-10-24

Status: the initial design-only goal, follow-on refinement and workshop-alignment
goal are complete. Production implementation and publication followed under
`site/`; the [progress record](cubey-site-implementation-progress.md) retains
milestone outcomes and the [site guide](../../site/README.md) owns current
behavior. The contracts and evidence below describe the completed design phases.

## Current contract

The user rejected treating the README highlight selection as the new site roster.
Design the presentation around the breadth and individual capabilities of Cubey:
simulation (including explosion and 2D), terrain then water, ocean, atmosphere/
lighting/day-night/clouds, materials, SDF, and a glTF importer/renderer capability
showcase. Retain Study A's dark direction. Mockups/placeholders are allowed.
Techniques and development journeys remain later layers. No runtime changes,
production site build, source push, public deployment or README cutover.

## Milestones

- Complete: source-guided capability/evidence inventory. The draft content map
  covers eight subject areas, 21 presentation entries, all fourteen native
  project owners and all thirteen currently supported required-use glTF extensions.
- Complete: proposed subject-led information architecture and page models.
  Separate subject, demo and variant; use direct subject experiences for Ocean,
  Atmosphere, Materials, glTF and Fractals without redundant single-scene pages.
- Complete: create responsive, navigable design mockups demonstrating breadth and
  representative families/scene views with clearly labelled asset placeholders.
- Complete: perform a documented content/usability/browser review, repair concrete
  issues and record remaining user decisions.
- Complete: publish the design review, update the implementation brief and audit
  the design goal before completion.

## Initial design-loop evidence

- Checkout: local `main` at `492fac84`; local `origin/main` at `b72c7c5c`.
- Existing planning work: plan, first design review and notes-index edits are
  owned by this thread. Preserve the native implementation and inherited commits.
- Existing dark/light studies are historical design evidence. A is selected;
  their six-destination content model is superseded by the latest direction.
- No new renderer captures or native jobs are running.
- Content registry: `docs/notes/cubey-site-content-map.json`.
- Mockup authoring/export: `outputs/cubey-site/structure-study-2026-10-24/`.
  Thirteen static mockup/review pages, a source-guided content model and selected
  native/placeholder media are prepared; no production framework was installed.
- Static content/route audit passes: all fourteen native owners and thirteen
  supported required-use glTF extensions are mapped; entry/source/asset paths,
  related links, placeholder labels and native hashes are coherent.
- Browser review passes: all 21 presentation routes, 22 narrow-layout checks
  across eleven page models at 320/390 pixels, Simulation keyboard/2D filtering,
  Terrain ordering, glTF feature/state recovery, native playback and material/SDF
  view identity. Receipts/screenshots are under the design export's `qa/` sibling.
- Refinements from review: shorter subject headlines; media-first narrow
  layouts; technical boundaries moved into secondary panels; explicit Overview
  state; matching native/placeholder variant views; view state survives reload.
- Eight entries currently have selected native review inputs; thirteen use
  explicit placeholders until appropriate showcase assets are selected/captured.
  This is a design-media gap, not an assertion that the native feature is absent.
- Next: package the reviewed mockups and receipts, write the final subject/page
  structure and implementation brief, verify documentation/delivery, and audit
  the design-only goal for completion. These actions are now complete; there is
  no pending design-loop execution task.

## Initial goal outcome

- [Open the final design review](http://starship:8787/reports/150ff20554307a90bc7a5f9b0b6d41f12cea4777bd582b56dffff9e75671d9f3/).
- [Structure specification](cubey-site-structure.md) and
  [implementation brief](cubey-site-plan.md) supersede the six-demo plan.
- Final report: thirteen mockup/review pages in a 113-file bundle, 22,902,943
  bytes. Every served file matched local bytes from Starship. Final report and
  screenshots opened in the connected browser.
- Documentation navigation, project layout, committed-media integrity, new-note
  links/whitespace and tracked-diff checks pass. Root README/native implementation
  is unchanged. All source work here is planning documentation and mockup artifacts.
- No production framework/dependencies, native capture, source push, public-site
  deployment or README cutover was performed. No native job is running.
- Remaining review choices are visible in the brief: labels/prominence, final
  scene/capture selection, and later stack/hosting/release endpoint. User artistic
  adoption is separate from this completed design proposal and its checks.

## Follow-on refinement review

- The subject/scene coverage is retained. Review findings: flat mixed-level
  catalogue, repeated homepage/index cards, too many generated selector modes,
  hidden feature-tour content, equal Fractal prominence and a misleadingly broad
  glTF "Layered materials" label.
- Refinement complete: a grouped visual subject directory; seven main subject
  links plus quieter Further explorations; six visible simulation scenes;
  scrollable atmosphere/material/glTF chapters; only purposeful media-backed
  comparisons. Proposed variant lists become capture/editorial possibilities,
  not automatic public controls.
- Current artifact root: `outputs/cubey-site/structure-refinement-2026-10-24/`.
- [Open the refined design review](http://starship:8787/reports/79d2574724a79c2bfe6bd3bfbf6151d8ab48c7259cd271805967cb1f97a28de9/).
- Terrain is one ordered dry-terrain-to-water chapter page. glTF's four visible
  chapters are Materials & light, Transmission & volume, Animation & deformation,
  and Texture & detail. All fourteen native owners and thirteen supported
  required-use glTF extensions remain covered by the 21-entry content registry.
- Purposeful comparisons are the matched wood-cut states and the Guardian's
  marble/limestone views. Their selected state survives reload. Variant lists
  remain editorial/capture possibilities rather than required captures or UI.
- Browser checks pass for all 21 presentation routes and 24 narrow-layout cases
  across twelve views at 320/390 pixels. Native film playback, comparison recovery,
  keyboard chapter navigation, image loading and horizontal bounds pass. The
  screenshots and receipt are under the refinement root's `qa/` directory.
- Final report: 111 files, 22,147,904 bytes. Every served file matched local bytes
  from Starship; `qa/final-delivery.json` records the bounded delivery check.
- Documentation navigation, project layout, committed-media integrity, new-note
  links/whitespace and tracked-diff checks pass. Remaining choices are final
  artistic adoption, showcase capture selection and later stack/hosting/release
  decisions; these checks do not establish native or public-site acceptance.
- No new persistent goal, production implementation, native capture or publication
  of the public site has been started by this review pass.

## Author's project framing after design review

- The author is mostly comfortable with the design and supplied a fuller account
  of Cubey's identity. This does not approve every proposed label or capture.
- Cubey is a personal graphics/simulation playground and workshop, revived from
  an old engine/learning project. Coding agents expanded the scope and enabled a
  more deliberate process, while topic selection remains curiosity-led.
- Experiments and foundation influence one another. Fluid/terrain-water and
  atmosphere/material lighting are concrete connections worth explaining.
  Branches, restarts and cuts belong to the retained history; they should not be
  rewritten as a predetermined product plan.
- The brief and structure now carry this framing: short home introduction,
  personal About, useful related-demo captions and evidence-led technical care.
  Proposed public wording remains reviewable. Deep techniques and journeys are
  still deferred, and the existing report predates these editorial additions.
- This follow-up changes planning documentation only. No mockup rebuild,
  production implementation, native capture, source push or deployment occurred.

Update this record at milestone boundaries. Keep bulky design artifacts under
ignored `outputs/cubey-site/` and retain exact report URLs and verification limits.

## Workshop-alignment goal

- Complete: carry the author's context through the actual mockups, public copy,
  subject connections, source content model and future implementation brief.
- Preserve the selected dark direction, all eight subjects/21 entries, the reduced
  interaction budget and showcase-first priority. Deep techniques/journeys remain
  deferred. No production implementation, native changes/captures, README cutover,
  source push or public-site deployment.
- Acceptance: workshop identity is legible on Home; About explains the origin,
  authorship, agent collaboration and experiment/foundation feedback; two concrete
  relationships are navigable in both relevant demo contexts; subject breadth,
  responsive layout and existing media/comparison behavior remain intact.
- Authoring: `outputs/cubey-site/workshop-alignment-2026-10-24/`.
- Work sequence: source-guided framing audit, revised Home/About and connected
  demo mockups, static/browser review, any necessary refinement, then publish a
  review report and synchronise the design/structure/implementation records.

### Workshop-alignment outcome

- [Open the final aligned review](http://starship:8787/reports/6339d2350e5953a016b2dbc209720f5f93e78b8664646f2a7f6b1c1b58657922/).
- Home/About now express the personal workshop, coding-agent collaboration and
  reciprocal experiment/foundation process. Both selected connections are
  visible on Home/About and navigable from the relevant demos. Detailed technique
  and development-journey layers remain deferred.
- Review removed repeated Terrain Water imagery on shallow water and duplicate
  material imagery on Home. All eight subjects, 21 entries, fourteen native
  owners and thirteen supported required-use glTF extensions remain covered.
- All 21 expected route identities and 28 narrow-layout cases at 320/390 pixels
  pass. New paths work both ways; keyboard chapter links, native film playback,
  cut comparison pause/restoration and Guardian hero restoration pass. Five
  screenshots and the browser/static receipts are included in the report.
- A single full-page mobile screenshot timed out; responsive checks resumed
  from recorded state and mobile proof uses viewport captures. No layout failure
  was inferred from that capture error.
- Final report: 111 files, 22,304,200 bytes. Every served file matched local bytes
  from Starship. Sixteen core mockup/data/style/script files are byte-identical
  to the browser-checked candidate; only review packaging changed afterward.
  `qa/final-delivery.json` records this check. The final report also opened and
  passed root layout checks at 320/390 pixels.
- Planning authorities are synchronised: structure, implementation brief,
  version-3 content map, alignment review and notes index. Documentation navigation,
  project layout, committed-media integrity and note-link/whitespace checks pass.
- Wording/homepage emphasis, final native media and later stack/hosting/release
  scope remain visible choices. These are design receipts, not final artistic
  approval or native/production qualification. No production implementation,
  native change/capture, README cutover, source push or public-site deployment.

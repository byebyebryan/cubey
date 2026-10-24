# Cubey showcase design review

Date: 2026-10-24

Status: historical first visual study. A was selected; the six-destination roster
and optional-glTF framing are superseded by the [broader structure study](cubey-site-structure.md).

Scope: review and refinement before implementation. This produced working design
studies in an ignored review export; no production site, dependency installation,
native capture, source commit/push or public-site deployment was performed.

## Review artifact

[Open the visual design and plan review](http://starship:8787/reports/ce3e6b31289a5bfe855683492bbf09204c8d577e35ab14952cf910f11890adb6/).

The report contains two proposed treatments using the same composition and real
native media, all eight visual candidates, desktop/mobile screenshots, exact
asset hashes and functional browser receipts. The dark and light studies have
working film/sculpture viewers; those review dialogs do not prescribe production
routing. The report is a review preview on Starship, separate from a public site.

Source: local `main` `492fac84`, with planning notes uncommitted. The report's
repository links use published `main`; the local source identity is recorded
separately because the inherited twelve commits have not been pushed by this work.

## Findings and refinements

| Finding | Refined proposal |
| --- | --- |
| Abstract requirements left roster and page structure open during unattended work. | Six primary viewing pages, optional glTF seventh, one curated homepage and brief source/about/credits. |
| Different subjects need different image shapes and scale. | Wide landscape/tank/film views; square guardian imagery. Full native frame on demo pages, reviewed crops on home only. |
| Eight-second solar arcs are not seamless ambient loops. | Stable posters and explicit playback; ordinary controls and one active film. No automatic carousel. |
| The committed Ocean poster is a busy dusk frame. | Recommend the exact 0.5-second daylight frame for the opening, with the full native film available on request. |
| Explanatory widgets and page quotas could expand V1. | Short captions and a few technique cues; comparisons only where they improve viewing. Keep techniques and journeys as later releases. |
| Unbounded asset polishing could reopen native experiments. | Existing films and accepted current guardian views suffice. Additional capture needs a named missing shot, existing preset and bounded completion. |
| A mobile headline can displace the opening image. | Compact the mobile heading; the first native image is visible within the 390×844 study viewport. |
| Pinned links to an unpublished local commit would be inaccessible publicly. | Report source links point to published main; exact local identities stay in provenance. Recheck source availability during public release. |

## Recommended design and roster

The user selected **Study A: dark gallery**, with restrained type/spacing and
colour supplied by the native work. Study B's warm light surround remains a
comparison reference. This adopts the visual direction; the roster and remaining
scope recommendations below still need selection.

The opening proposal is Ocean with a daylight poster and explicit film play.
The proposed six destinations are:

1. Ocean.
2. Terrain Water.
3. Water 3D.
4. Fire 3D.
5. Procedural materials, retaining separate mesh specimens and SDF cut films.
6. SDF sculpture, using current guardian marble/limestone views.

glTF + Terrain is a proposed seventh, useful for imported-object/PBR variety but
with some landscape overlap. Its helmet-derived media retains the separate
CC-BY-NC-4.0 attribution; this is a media-only exception to Cubey's MIT source.

Homepage order is a visual composition proposal rather than chronology:
Ocean opening, materials/sculpture, liquid/fire, Terrain Water, about/source.
The native media remains unmodified; exact video frames were extracted only for
poster comparison. No replacement artwork or new render feature was introduced.

## Evidence and limits

- Seven committed H.264 native films were probed: each is eight seconds,
  1280×720 at 60 frames/second. The report preserves their bytes/hashes.
- Connected Chrome opened both treatments. Screenshots cover desktop at the
  natural 1600-pixel viewport and mobile at 390×844; both treatments also had
  no horizontal overflow at 320 pixels. Temporary viewport overrides were reset.
- Ocean and the five other primary/related films decoded in the viewer;
  observed decoded frames/time advancement and no media error. The optional
  glTF film was probed and bundled; its browser playback was not separately tested.
- Viewer close stops playback. Guardian opens as a native still, Escape closes
  the dialog, and focus returns to its launch button.
- One close/playback timing issue and the mobile heading spacing were corrected
  during review. The final study files match those that were checked; the last
  report packaging adds screenshots/receipts around those files.
- All 34 final report files (21,877,554 bytes) matched served HTTP bytes from
  Starship. Local asset hashes and relative links pass. The final packaging
  receipt is retained in the local review evidence directory.
- These are bounded functional/layout receipts, not user artistic acceptance,
  a production-site test pass or a measured field-performance result.

Local evidence and report sources are retained under
`outputs/cubey-site/design-review-2026-10-24/`. The export contains only intended
review files/media; production must package its own selected tracked inputs.

## Decisions before launching

Visual direction A is selected. Roster/opening, framework/hosting and the first
run's endpoint remain proposed. Astro with authored components, GitHub Pages at
`dev.byebyebryan.com/cubey/`, and a complete local review candidate remain the
technical/delivery recommendations. No execution goal has been started.

The [current plan](cubey-site-plan.md) incorporates these refinements while
retaining the complete collection and journey context for later releases.

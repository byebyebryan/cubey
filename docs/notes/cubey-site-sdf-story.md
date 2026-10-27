# Building and cutting a solid — SDF presentation chapter

Date: 2026-10-25. Baseline: published site source
`2e2792e4a92f79b1cb4a593a6c74947f571a5319`.

This is the first detailed visual story following the overview-site polish.
It lives at `/demos/sdf/inside-a-solid/`, with front doors on SDF forms and
Materials. This bounded chapter does not select a site-wide detailed-story
framework or reopen native rendering/authoring experiments.

## Content and evidence

- Lead with the existing native robot construction film, explicitly saved
  replay rather than live sculpting, followed by the native material-cut
  comparison before the explanatory figures.
- Explain union/subtraction and distance-guided surface finding through two
  labelled, analytic 2D SVG illustrations. Their math and scale are independent
  of Cubey's native 3D field. Static default views are rendered at build time;
  controls appear only after the browser module initializes. No timer,
  automatic animation, media fetch or new playback policy is introduced.
- Relate the diagrams to the actual hierarchy, near-surface distance bricks,
  interpolation, finite-difference normals and shading pipeline. Preserve
  sampled-field tolerances and grazing-silhouette limits. Geometry edits
  synchronously rebuild the field; material edits can reuse geometry caches.
- Reuse the fixed-camera wood/marble/rust/glass comparison. The gallery's glass
  traces its own interfaces and environment, not the other displayed bodies.
  Shared material recipes do not imply matching optical transport.
  The story opens on the committed cut; Materials retains its solid-first
  default. Both keep saved-query restoration and direct no-JavaScript state links.
- Keep a four-part history: procedural cuts, authored robot, mirrored Guardian,
  then shared materials. The Guardian counts describe 65 grouped operations
  containing 1,011 primitive segments, not 1,011 independent brush actions.
  Driftwood's retirement and accepted stylized surfaces remain visible context.

The new historical sphere selection comes from
`outputs/sdf/oblique-closeout/captures/final-4096.png`, recorded by
`outputs/sdf/oblique-closeout/manifest.json` from clean native source
`10573c7af7bf40461bd05652b6cdf94fd8cd53c0`. Original PNG SHA-256:
`93f6e024020da9a3267f528d4d272a2c72863f27859fd4a38bffded8a31260b3`.
It is a 4,096-cut, seed-42 procedural prototype, explicitly labelled historical.
Only web image encoding/thumbnail preparation is new. No native render is run.
The selected-media ledger keeps the input and derivative hashes separately;
the [pinned receipt](../../site/media/receipts/sdf-prototype-history.json) retains
the original manifest identity, selected command and native qualification fields.

The implementation follows the [SDF guide](../../projects/sdf/README.md),
[native trace shader](../../projects/sdf/shaders/trace.glsl),
[implementation evidence](sdf-implementation-evidence.md),
[procedural closeout](sdf-procedural-prototype-closeout.md),
[Guardian closeout](sdf-guardian-closeout.md) and
[thread closeout](sdf-thread-closeout.md). No timing graph or current-performance
claim is inferred from historical native receipts.

## Acceptance and publication

Model tests cover operation signs, cutter movement, zero-contour approximation,
analytic hit convergence, misses and bounded grazing-ray work. Browser acceptance
covers the actual controls, keyboard focus, saved cut states and film behavior.
Narrow frames are desktop-browser layout checks, not physical device acceptance.

Review receipts belong under `outputs/cubey-site/sdf-story-2026-10-25/`.
Final gates are the public-origin/base `npm run verify` and the scoped
documentation suite excluding `documentation_cli_recipes`. Native runtime
qualification remains in the original records. Exact publication identity and
served-byte verification follow the Pages workflow separately from local checks.

The four changed public routes were reviewed at 320, 390, 768, 1024 and 1600
pixel frame widths: twenty cases, all with one main heading, no horizontal
overflow and no failed loaded images. The new chapter's player starts at 430
CSS pixels in the smallest frame. The no-JavaScript frame retains both figures
and hides their controls. Keyboard checks cover the shape modes, cutter slider,
ray steps/reset and a miss above the circle. The native 1280×720 construction
film plays to its 16-second end without media errors; choosing a saved cut state
pauses playback. Both SDF and Materials entry links reach the chapter.

All 119 baseline public media/thumbnail files retain their hashes. The new
historical still adds two WebP files. The collection remains seven subjects,
21 demo entries, 13 native owners and 15 film-backed selections; the chapter is
an editorial route, not another native demo identity.

<a id="published-outcome"></a>

## Published outcome — 2026-10-25

The chapter is live at
[`/cubey/demos/sdf/inside-a-solid/`](https://dev.byebyebryan.com/cubey/demos/sdf/inside-a-solid/)
from source `48fa1ed1c7c7d02d0d6892466fecc2dec0afeb42`. The
[Pages run](https://github.com/byebyebryan/cubey/actions/runs/40244161355)
passed; `outputs/cubey-site/sdf-story-2026-10-25/publication.json` records 148
files / 29,120,488 bytes and byte parity with the published build. Its
`live-browser.json` receipt records the native 1280×720, 16-second video and the
illustration/cut interactions. This completes publication of the web chapter;
it does not complete the separate native authored-content showcase or Guardian
construction-film refresh described in the
[material/SDF roadmap](material-demo-roadmap.md).

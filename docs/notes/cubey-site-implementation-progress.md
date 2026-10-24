# Cubey presentation-site implementation progress

Date: 2026-10-24

Status: implementation goal complete, explicitly launched by the user after the
completed workshop-alignment design. Work is on the local
`codex/cubey-site-implementation` branch, based on `492fac84`. The twelve inherited
commits beyond local origin/main remain unpublished. Prior planning edits belong
to this thread and are preserved.

## Contract and decisions

- Implement the reviewed dark A site in-repo under `site/`, using Astro 7.4.2,
  authored components, static HTML and a small media/comparison script. No
  Markdown-derived presentation or browser UI framework is needed.
- Preserve all eight subjects, six simulation demos, the twenty-one content
  entries, personal workshop identity and two concrete demo connections.
  Detailed technique explanations/development journeys remain deferred.
- Use relative internal/asset URLs so one build can be reviewed under a report
  prefix and later hosted at a selected root or `/cubey/`. The origin/base can be
  configured for later hosting metadata; no public endpoint is adopted now.
- Select retained native evidence first; capture named gaps with existing native
  routes in one sequential GPU lane. Do not expand renderer/solver/sculpting/GI
  scope. Asset research delegates are read-only and run no native jobs.
- Public-facing assets and provenance/attribution must be tracked. A clean site
  build must require only Node/package installation, with no native build, GPU,
  private host, download-time corpus or ignored capture output.
- Deliver a reviewable implementation preview and verification record. No source
  push, public-site deployment or README gallery cutover is included. Prepare
  later release instructions without changing the current gallery contract.

## Milestones

1. Complete: implementation scaffold, selected-media ledger, retained media audit
   and bounded native capture preparation.
2. Complete: reusable authored components, canonical routes, all subject-specific
   compositions, About/credits and the reduced media/comparison behavior.
3. Complete: fill every included entry with real selected media; keep licenses,
   source identities, capture recipes and scope through derivatives.
4. Complete: type/content/build checks, clean-build/static route/media audit,
   responsive/keyboard/browser review and loading evidence; repair observed gaps.
5. Complete: publish a reviewable implementation preview, synchronise documentation
   and record exact delivery/remaining release choices before closing the goal.

## Evidence locations

- Previous accepted design direction:
  [workshop-alignment review](cubey-site-workshop-alignment.md).
- Authoring: `site/`.
- Capture/build/browser receipts: `outputs/cubey-site/implementation-2026-10-24/`.
- Public/source media ledger and attribution will live with the site source.

Update at milestone boundaries; retain completed captures and inspect failures
before retrying. Do not translate local design/native evidence into a public
deployment or production-performance claim.

## Scaffold and media milestone

- Complete: Node/npm setup and pinned Astro/component scaffold. Astro's type check
  covers 27 files with no errors/warnings. The first production build/content audit
  is now running.
- Complete: seven existing capture targets built from unchanged native source.
  Sequential captures fill the simulation/sky/fractal/glTF gaps. Reused media and
  fresh captures retain distinct provenance; historical capture revisions remain
  unknown where the original evidence does not record them.
- Complete: all twenty-one entries have native posters; motion examples have
  silent MP4 recordings where useful. WebP derivatives retain aspect ratio and
  source hash identity in the ledger. Pinned glTF model hashes and full author
  notices are tracked. The morph example is distinct from the Fox skinning clip.
- Review refinements: a readable lamp/fox/UV framing, an early explosion-film
  poster, and the current authored builtin source/outlet view. Home curates the
  colourful Smoke/dye 2D capture alongside Water/Fire; Explosion remains a peer
  in the full Simulation collection and a direct homepage link.
- Retained failures: the older 16-substep finite-volume proposal hit its numerical
  CFL guard before updating state; the source-documented 32-substep protocol
  passes a two-frame preflight and full 600-frame recording. Exact camera scale
  .1 was rejected at the float-valued lower bound; .11 is accepted. No native
  renderer/solver changes were made to bypass either guard.
- Current implementation: authored Home/About, visual directory, six simulation
  scene pages, Terrain land/water chapters, Ocean, sky chapters, Materials and
  comparisons, SDF collection/scenes, four glTF chapters, Fractals and credits.

## Build and browser review milestone

- Complete: 21 authored static pages; 44 selected media assets (about 23.1 MB,
  including thumbnails). The content gate covers eight subjects, 21 entries,
  fourteen native owners and thirteen supported required-use glTF extensions.
- Complete: Astro type check, media identity/budget/attribution audit and built
  HTML route/anchor audit. The latter checks 489 references under an arbitrary
  `/mounted/cubey/` prefix. A root-path case in the auditor was corrected; actual
  site links were valid.
- Complete: isolated Node-only clean dependency install/build, using only site
  source/web media and the content-map JSON. Its output matches the reviewed
  build byte-for-byte. Final metadata source changes will be rechecked before
  final delivery; no native build/GPU/private capture paths are build inputs.
- Browser review: all 21 entry identities and all 21 pages at 320/390/768 pixels
  (63 layout cases) pass without horizontal overflow or broken loaded images.
  Both editorial connections work in both directions. Players pause each other;
  comparison selections pause playback and restore on reload. Fox and morph
  films load and advance as their distinct examples. Keyboard chapter links and
  the 13-extension disclosure work.
- Accessibility refinement: skip navigation now focuses the main landmark; the
  current Demos ancestor uses the location state on child pages. Visual checks
  retain native media within the first mobile viewport on Home.
- Preview uses owned Astro server port4321. The host initially rejected Starship;
  restarting that server with the explicit allowed hostname fixed it, without a
  permissive application-wide host configuration. Stop the owned preview server
  after immutable preview delivery; leave the shared report server running.
- All existing project/source guide paths exist at the verified remote main
  b72c7c5c; their newest native content still requires the inherited unpublished
  source range. Media identities are also downloadable from the built site so
  the implementation preview has no link to its unpublished `site/media` folder.
- An independent asset-ledger review found one misleading optics caption:
  Dispersion Test authors thickness but no attenuation color/distance. The copy
  now says volume thickness. Other fixture/native/web hashes and exact Fox/morph
  notices pass that bounded review.

## Implementation preview delivered

- [Open the immutable implementation preview](http://starship:8787/reports/88586fd156f27958d95c5f813f389b601c9bbb0c2aed901e62f704581f4c940a/).
- Delivery: 96 files, 23,301,823 bytes. All served bytes match the built output;
  nineteen unique route URLs (including directory aliases, metadata and 404)
  return correctly from Starship. The actual report prefix also passes browser
  playback and comparison reload checks.
- Browser receipt: eleven acceptance assertions pass; twenty-one entry identities,
  sixty-three page/width cases, meaningful controls/keyboard behavior, both-way
  connections and mounted media delivery. Six screenshots are retained in `qa/`.
- Native media audit: fourteen silent H.264/yuv420p films. All selected assets have
  checked dimensions/hash/credit records. Every primary content slot has actual
  native visuals; there are no built-site mockup placeholders.
- Loading policy: no autoplay/looping films, poster-first `preload=none`, responsive
  images and lazy lower-page images. The conservative maximum HTML/styles/scripts/
  all-full-size-image resource bound is 346,271 bytes (before responsive/lazy
  savings); the check enforces a 600 KB ceiling. Individual images/films enforce
  250 KB/8 MB limits. This is a byte-policy check, not a network-speed/FPS claim.
- A nested-error-route check exposed relative assets in the initial 404 page.
  The final error page is self-contained and recovers to the configured root;
  this path was verified in the browser. The final media identity JSON is static
  build output and contains no local capture paths.
- Final isolated dependency install/build output matches the delivered build.
  The resource auditor initially counted manual image-download links on Credits
  as eager resources; that auditor defect was corrected and rechecked in both
  the main and retained clean build, preserving the initial failure receipt.
- Source push, public-domain deployment and README cutover remain unperformed.
  The inherited twelve native commits remain part of any later source-publication
  review. Hosting and final artistic acceptance are distinct from these checks.

## Closeout

- Local implementation checkpoint: `54c8bf6` (`feat(site): implement the Cubey
  graphics workshop showcase`), including the preserved planning/design package.
  Changes are confined to `site/` and this thread's notes/index; native source
  and the current root README gallery are unchanged.
- [Implementation review, screenshots and verification receipts](http://starship:8787/reports/e0f32d64443f123e11ef735720de134953595f004f2dddbe81a3ed79158c24a2/).
  Its thirteen served files (546,240 bytes) match the review export. The main
  implementation preview remains the browser deliverable and is independent
  of the temporary port4321 server.
- Documentation navigation/layout/media, note links/whitespace and scoped source
  checks pass. The third-party notice files retain the exact legal sections,
  pinned upstream URLs and upstream/retained hashes without copying unrelated
  illustrated README prose.
- Once the new Markdown became tracked, the repository's navigation gate exposed
  missing front-door links. The main docs index now links the site guide, which
  links the media/attribution index and all five retained model notices. This
  indexing repair changes no built website bytes.
- The owned preview server is stopped. Temporary clean-build dependency folders
  were removed after success; source/build/receipt snapshots and all completed
  native captures remain. The shared Starship report server is left running.
- No required implementation work remains in this goal. The next release step
  needs the chosen public endpoint and explicit source/publication scope; the
  static build, selected media, attribution and review result are concrete.

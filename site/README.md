# Cubey presentation site

The component-authored showcase lives here. It presents Cubey as a personal
graphics/simulation workshop and its supporting engine: visual demos first,
with concise context and useful connections. The first detailed visual chapter,
“Building and cutting a solid,” connects SDF construction, materials and a short
development history. Further chapters remain editorial choices. The public site is hosted at
[dev.byebyebryan.com/cubey/](https://dev.byebyebryan.com/cubey/).

## Build and review

Use Node 22.23.4 or a newer supported even-numbered release, plus npm:

```sh
cd site
npm ci
npm run verify
npm run dev
```

`npm run verify` checks Astro types, content/native-owner coverage, media hashes,
attribution and the built routes/anchors, then builds `dist/`. Build inputs are
tracked source and selected web media. No native executable, GPU, Vulkan/CUDA,
private report host or capture output is needed. `npm run build` builds the static
site alone; `npm run preview` serves its output for review.

Astro 7's preview can run in the background. Stop the owned preview with
`npm exec astro preview stop`. For a custom LAN hostname, pass the specific name
with `npm run preview -- --allowed-hosts your-hostname`.

The implementation uses authored Astro components and plain CSS/JavaScript.
Films play directly in home, subject-directory, simulation and SDF overview
cards as well as scene pages. Navigation links stay outside native player
controls; starting a native film pauses the others. Guardian and Robot use the common
16:9 native landscape recaptures. Native images and videos fit without cropping the
subjects or changing their proportions.

Every selected film has a small hover preview in browsing cards; still-only
selections remain still. The current 15 film-backed selections each have a
validated preview. A 350 ms dwell on a fine hover pointer requests a separate,
silent 640x360 / 18 fps excerpt of up to 3.5 seconds. Each clip is H.264/yuv420p,
fast-start and at most 400 KB. The encoder raises CRF through 36 when needed and
fails if a clip still exceeds the budget. Clicking replaces the tile with the
full native player inline. Scene players and the home hero use native controls
from the start, without a separate play overlay. Touch, reduced-motion and
supported data-saving preferences retain native players. With JavaScript
disabled, the same native players remain available.

Preview URLs are assigned only after hover intent. Leaving the card, scrolling
it away, hiding the tab, Escape/Tab, or starting full playback stops previews.
An explicitly playing film blocks hover playback. The encoded preview remains
cached for reuse. `npm run check` verifies that every film-backed selection has
a valid, current preview and receipt; missing or stale parent identities fail
the check. The Node-only site build does not need FFmpeg. The hover policy tests
run with the check command, and browser review checks real playback.

About embeds two original OpenGL recordings in standard YouTube players, with
lazy loading, no autoplay and direct watch links. The 2018 labels are upload
dates. Their source records live in `media/sources/legacy-youtube.json`; these
externally hosted players are separate from the native playback/preview policy.

Credits groups the model and terrain sources first, with the complete capture
table in a native disclosure. Existing capture fragments remain usable. The
[polish review](../docs/notes/cubey-site-polish.md) records the latest copy,
navigation and responsive-layout acceptance scope.

The [SDF story record](../docs/notes/cubey-site-sdf-story.md) owns the first
detailed chapter's evidence and acceptance limits. Its two 2D SVG illustrations
are built into the static HTML, then enhanced with manual, keyboard-accessible
controls. The shared analytic model in `public/scripts/sdf-story.js` is used by
the build, browser and model tests. It is an explanatory illustration, not a
WebGL/native renderer or a live brush editor. The chapter reuses the native
robot film and cut comparison, plus a labelled historical procedural still.

Prepare all selected films automatically using a fresh staging directory. The
script discovers film-backed selections from the manifest, reuses current
previews without reencoding, and keeps successful staging files if a later
encode fails:

```sh
python3 site/tools/prepare-hover-previews.py \
  --output outputs/cubey-site/default-previews-2026-10-25 --import-media
```

Use repeatable `--entry demo-id` options for a subset; unrelated receipt entries
and selections remain intact. A second pass on the same inputs leaves the media
ledger and receipt unchanged. Optional start/duration/CRF overrides live in
[the trim settings](media/receipts/hover-preview-trims.json); unlisted entries
use the shared defaults. Exact trims, encodes, parent identities and hashes are
recorded in [the preview receipt](media/receipts/hover-previews.json).

The materials page shows all twelve spheres together, with Forward PBR / SDF
and dusk / dawn controls. Renderer changes preserve the lighting phase and
playhead; changing phase starts its film from the beginning. Individual frozen
stills remain available in a disclosure. A second native film shows matching
cuts through wood, marble, rusted steel and clear glass. The native presets and
qualification boundaries are documented
[here](../projects/pbr_materials/presets/presentation/README.md); capture identities
and web encoding commands are in [the receipt](media/receipts/native-showcases.json).
Atmosphere is grouped into scattering/light,
clouds, then night sky with a complete accelerated solar cycle. Fractals is
deferred from the public site; its native project remains documented.

Internal and asset URLs are relative, so the same `dist/` works beneath a hosting
prefix. `SITE_ORIGIN` and `SITE_BASE` may be supplied to emit canonical metadata
for a chosen public endpoint; leave the origin unset for an unannounced preview.
Set `SITE_BASE` to the actual mount path when configuring a host's nested 404
fallback. The self-contained error page needs no relative stylesheet/script and
also works when opened directly as `404.html` in a review export.

The checked budgets are 250 KB per image, 8 MB per film and a conservative 600 KB
per-page bound for HTML, CSS, JavaScript and all full-size poster images. Responsive
image selection and lazy loading reduce the actual image load. Films use
`preload="none"` and begin on request; they are excluded from that image-page bound.
Third-party YouTube player resources are also outside this local-file bound.

## Content and assets

- `src/pages/`, `src/components/`, `src/layouts/`: presentation and canonical routes.
- `src/data/catalog.ts`: public copy/scopes and typed access to the content map.
- [Content map](../docs/notes/cubey-site-content-map.json): subject/demo identities,
  native owners, supported extension groups and editorial connections. Its
  variant lists remain editorial possibilities, not automatically generated UI.
- `media/manifest.json`: selected media, byte hashes, parent identities,
  attribution and capture references. All twenty-one entries need native posters.
- `media/receipts/`, `media/sources/`, `media/licenses/`: pinned current/historical
  capture inputs, model downloads and exact imported-asset notices.
- [Selected media and attribution](media/README.md): ledger semantics and the
  imported model notice index.
- `public/media/`: tracked WebP derivatives and silent native MP4 films.
- `public/scripts/media.js`: ordinary players, scene comparisons and the matched
  material-film viewer.
- `src/pages/demos/sdf/inside-a-solid.astro`, `public/styles/sdf-story.css` and
  `public/scripts/sdf-story.js`: the first detailed SDF chapter and its figures.

Original media retains Cubey's MIT policy. Imported assets keep their own licenses
and authors, surfaced at `/credits/` and beside imported examples. The old Damaged
Helmet composite is not used in this site. Historical media is not relabelled as
a current-head renderer result: selection and capture identities remain separate.

The one-time editorial importer requires Python 3; poster imports also use the
installed site image dependency (Sharp), while film imports use FFmpeg/ffprobe
for inspection and preview encoding. Importing a film with `--entry` and
`--role film` stages the full film and its preview first,
then publishes the selected media and ledgers together. A preview failure leaves
the previous selected film/preview ledger in place. This importer is not part of
the build:

```sh
python3 site/tools/import-media.py asset-name path/to/native.png \
  --entry demo-id --role poster --caption 'What this capture shows.' \
  --recipe site/media/receipts/capture-name.json

python3 site/tools/import-media.py demo-film path/to/native-film.mp4 \
  --entry demo-id --role film --caption 'What this film shows.' \
  --revision 0123456789abcdef
```

Run it from the repository root. Native capture/reproduction is a separate engine
workflow; follow the referenced guide/recipe and preserve successful outputs.

## GitHub Pages publication

[The Pages workflow](../.github/workflows/site.yml) installs locked dependencies,
runs `npm run verify`, and uploads only `site/dist/`. Selected images and films
are served alongside the HTML; GitHub attachment uploads are unnecessary.
The endpoint inherits the account's existing `dev.byebyebryan.com` Pages domain
and uses the `/cubey/` project path. The workflow reads the origin and base path
from Pages rather than changing the account domain or adding a separate CNAME.

Pushes to `main` that change the site, content map, material catalog, glTF extension
contract, or workflow build and deploy automatically. Pull requests run the same checks and
build without deploying. `workflow_dispatch` on `main` republishes the current
source when needed. The workflow uses Node 26.12.1; local builds also support the
minimum version above.

The public `/metadata/deployment.json` records the source commit and workflow run;
`/metadata/media.json` records selected media identities. Check these and the
actual served pages/media after publishing. The host should return `video/mp4`
for films and support byte-range requests for partial downloads and seeking.
Native source/capture qualification remains separate from site publication.

The implementation progress/evidence record is
[here](../docs/notes/cubey-site-implementation-progress.md). The
[implementation brief](../docs/notes/cubey-site-plan.md) and
[structure](../docs/notes/cubey-site-structure.md) retain the reviewed intent.

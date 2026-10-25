# Cubey presentation site

The component-authored showcase lives here. It presents Cubey as a personal
graphics/simulation workshop: visual demos first, with concise context and useful
connections. Detailed technique chapters and development journeys remain later
editorial work. The public site is hosted at
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
controls; starting a film pauses the others. Guardian and Robot use the common
16:9 native landscape recaptures. Native images and videos fit without cropping the
subjects or changing their proportions.

A small hover-preview pilot covers water, fire, ocean, clouds and materials in
browsing cards. On a fine hover pointer, a 350 ms dwell requests a separate,
silent 640x360 / 18 fps / 3.5-second excerpt. The five files total 620,898 bytes;
none is over the 400 KB preview budget. Clicking replaces the tile with the full
native player inline. Scene players and the home hero use native controls from
the start, without a separate play overlay. Touch, reduced-motion and supported
data-saving preferences retain native players. With JavaScript disabled, the
same native players remain available.

Preview URLs are assigned only after hover intent. Leaving the card, scrolling
it away, hiding the tab, Escape/Tab, or starting full playback stops previews.
An explicitly playing film blocks hover playback. The encoded preview remains
cached for reuse. `node --test tools/hover-preview.test.mjs` checks these policies
with a controlled clock/media harness; browser review checks real playback.

Regenerate/import the pilot from existing selected films using a fresh staging
directory, preserving earlier outputs:

```sh
python3 site/tools/prepare-hover-previews.py --output outputs/hover-preview-review --import-media
```

`preview` selections in the media ledger carry their parent film identity. An
updated parent requires regenerated previews. Exact trims, encodes and hashes
are recorded in [the preview receipt](media/receipts/hover-previews.json).

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

Original media retains Cubey's MIT policy. Imported assets keep their own licenses
and authors, surfaced at `/credits/` and beside imported examples. The old Damaged
Helmet composite is not used in this site. Historical media is not relabelled as
a current-head renderer result: selection and capture identities remain separate.

The one-time editorial importer requires Python 3, installed site dependencies
(Sharp), and FFmpeg/ffprobe for film inspection. It is not part of the build:

```sh
python3 site/tools/import-media.py asset-name path/to/native.png \
  --entry demo-id --role poster --caption 'What this capture shows.' \
  --recipe site/media/receipts/capture-name.json
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

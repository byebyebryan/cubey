# Cubey presentation site

The component-authored showcase lives here. It presents Cubey as a personal
graphics/simulation workshop: visual demos first, with concise context and useful
connections. Detailed technique chapters and development journeys remain later
editorial work. The current root README gallery is retained until release/cutover.

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
- `public/media/`: tracked WebP derivatives and silent native MP4 films.
- `public/scripts/media.js`: ordinary players and two purposeful comparisons.

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

## Publication boundary

The site is implemented locally and delivered as a review preview before public
hosting is selected. No public deployment, source push or gallery cutover follows
from a local build. Before release, confirm the endpoint, the complete Git range
(including inherited native commits), source links and final media, then publish
and verify the served revision before compacting the root README.

The implementation progress/evidence record is
[here](../docs/notes/cubey-site-implementation-progress.md). The
[implementation brief](../docs/notes/cubey-site-plan.md) and
[structure](../docs/notes/cubey-site-structure.md) retain the reviewed intent.

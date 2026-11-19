# Selected media and attribution

[The ledger](manifest.json) owns the selected website assets: public filenames,
dimensions, byte hashes, captions, credits, parent identities and capture/source
references. It maps every presentation entry to a native poster and, where useful,
a recorded film. Public web files live in `../public/media/`; source fixture GLBs
and full-resolution capture outputs are not required for the site build.

Original Cubey media retains the repository's MIT policy. Imported models retain
their own licenses and authors. Their notices are preserved below and displayed
with the relevant captures on the site's Credits page.

## Presentation capture defaults

Use **1920×1080 at 30 fps** for new 16:9 native masters. Choose the frame count
from the shot's duration. From the repository root, a ten-second observatory
orbit uses:

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/csg-showcase-2026-11/observatory-film.json \
  --headless --capture video --width 1920 --height 1080 --fps 30 --frames 300 \
  --require-validation --anti-aliasing off --output native.mp4
```

Start with native rendering and the project's approved sample settings. Record the internal
render extent separately from the master dimensions, along with the effective
AA or upscaler, supersampling and any RT sample count. An SDF 2× supersampled
1080p master renders internally at 3840×2160. Fixed RT can trace several
spatial samples per render pixel at the master extent. Headless AA Auto
uses the direct path. Request `--anti-aliasing taa` explicitly on supported
views and check the effective mode in the log.

Deliver the full film at **1280×720 at 30 fps** as silent H.264/yuv420p MP4
with fast-start. Use the project's importer where it has one. For a new
encode, this is a starting point from a 1080p30 master:

```sh
ffmpeg -n -i native.mp4 -an -vf 'scale=1280:720:flags=lanczos' \
  -c:v libx264 -preset slow -crf 22 -pix_fmt yuv420p -movflags +faststart film.mp4
```

Keep the master, film and hover preview as distinct assets. The site's
[hover preview policy](../README.md#build-and-review) supplies the existing
640×360 at 18 fps excerpts. Record shot-specific cadence and encoder choices
in the capture receipt within the existing site media budgets.

For an AA comparison, match scene, lighting, camera path, frame rate and web
encode. Use direct 1× as a control when testing native TAA. Include the
approved-quality or published clip when considering a replacement. For DLSS,
include a spatial comparison at the same reduced render extent when judging
the reconstruction. Review motion at normal playback speed and at the site's
displayed size. Check the opening frames and any cuts for history artifacts.
Choose a replacement when it visibly improves the delivered film.

Keep the exact source and binary identities, scene and preset, commands, sample
settings and file hashes in the capture receipt. Existing selections retain
their original recipes and receipts.

The [November capture refresh receipt](receipts/standard-capture-refresh-20261119.json)
records the updated masters and web derivatives alongside the previous asset
identities. Simulation footage rendered at 60 fps keeps that render cadence.
The 30 fps derivative selects alternate frames and preserves playback duration.

## Imported model notices

| Model | Preserved notice | Source/license scope |
| --- | --- | --- |
| Anisotropy Barn Lamp | [Attribution notice](licenses/AnisotropyBarnLamp.md) | Wayfair / Eric Chadwick, CC BY 4.0. |
| Dispersion Test | [Attribution notice](licenses/DispersionTest.md) | AGI / Ed Mackey, CC BY 4.0. |
| Fox | [Attribution notices](licenses/Fox.md) | PixelMannen model, CC0; tomkranis rig/animation and AsoboStudio/scurest glTF conversion, CC BY 4.0. |
| Texture Transform Multi Test | [Attribution notice](licenses/TextureTransformMultiTest.md) | Analytical Graphics, Inc. / Ed Mackey, CC BY 4.0. |
| Animated Morph Cube | [Attribution notice](licenses/AnimatedMorphCube.md) | Microsoft, CC0. |
| Stanford Dragon | [Source and use notice](licenses/StanfordDragon.md) | Stanford Computer Graphics Laboratory; separate research/noncommercial usage terms. |

[Pinned source URLs and hashes](sources/gltf-sample-assets.json) identify the exact
downloaded GLBs and upstream notices. The glTF notice files retain the original legal
sections, linked to their pinned upstream README, with upstream/retained hashes.
The old Damaged Helmet composite is not included in the selected website media.

## Capture identities

The approved CSG quartet has a dedicated
[current-source capture receipt](receipts/csg-showcase.json). Six native landscape
stills cover four models and an aligned intact/section pair; four native films
show camera arcs and the robot's saved construction. The source JSON/preset
digests, native executable/shaders and web derivative identities are recorded
separately. Full-resolution masters remain in the retained capture packet;
selected 720p films, WebP posters/thumbnails and hover excerpts are tracked here.
Previous robot captures and their receipts remain historical assets.

Capture from the tracked native scenes using the
[SDF guide](../../projects/sdf/README.md#capture-and-qualification). Prepare and
select the web derivatives from a completed packet. The initial quartet import
used:

```sh
python3 -B site/tools/import-csg-showcase.py --native outputs/sdf/my-csg-capture --output outputs/sdf/my-csg-web --import-media
```

Use a fresh staging directory. The importer refuses reused asset IDs, including
on this already-imported checkout; a future media refresh needs new identities
and a reviewed receipt. Omitting `--import-media` prepares a candidate ledger
and derivatives without changing local selections on an unimported source.
This importer uses
FFmpeg/ffprobe and the existing hover-preview policy. The site build itself
needs neither native capture outputs nor FFmpeg. New identities preserve the
older selected-media evidence rather than rewriting its receipts.

About also embeds two author-selected recordings of the original OpenGL Cubey
from YouTube. [Their source records](sources/legacy-youtube.json) retain the video
IDs, original titles, author and verified upload dates. The 2018 upload dates do
not establish capture dates or code revisions. These externally hosted players
use YouTube's privacy-enhanced embed domain, load lazily without autoplay, and
retain direct watch links. They are separate from the current native captures
and their MP4/hover-preview ledger; no video was downloaded or reencoded.

Current captures have command/binary/source receipts under `receipts/`, referenced
per asset by the ledger. A source revision is populated only when the capture's
evidence records it. Historical media with an unknown capture revision keeps
that value empty; its later selection revision is a separate identity.

The SDF story reuses a historical 4,096-cut sphere still from the procedural
closeout at clean source `10573c7af7bf40461bd05652b6cdf94fd8cd53c0`. Its ledger
entry retains the original PNG digest and [pinned capture receipt](receipts/sdf-prototype-history.json). It is
labelled historical, not a new render. The chapter's interactive SVG figures
are authored analytic 2D illustrations, outside the native-capture ledger.

WebP images preserve the scene and aspect ratio while resizing/encoding the
native source. Films are silent H.264/yuv420p. Parent and derivative hashes remain
distinct. The website exposes a static, path-free media identity download; full
capture paths and reproduction references belong to this source ledger.

The complete twelve-recipe mesh/SDF specimen set retains its original accepted
qualification identities in `receipts/full-material-library.json`. These are
historical native captures, not renders from the site's Git revision. New sky
stills, accelerated cloud drift and a complete solar-cycle film retain commands,
binary hashes and frame-parent identities in their individual receipts. The
cloud film uses an explicitly accelerated wind parameter for visible motion.
`receipts/web-video-faststart.json` records lossless delivery remuxes. Their
encoded video-packet hashes match the native inputs; only MP4 container layout
changes to put the playback index first. The content gate enforces fast-start
layout for every film.

The current material library compares forward PBR, SDF and hardware RT under
the same clear-atmosphere dusk/dawn timelines. Its
[capture and derivative receipt](receipts/raytracing-materials.json) identifies
the qualified 1080p30 masters, unchanged recipes, 720p30 films and 960×540 stills.
All three use the shared environment after the applicable interfaces; local
specimen shadows, neighboring-object optics and diffuse GI are outside this
comparison. The earlier cloud-lit library media and cut-material films remain
preserved with their original identities.

Prepare a new selection from qualified native captures with
`site/tools/import-raytracing-materials.py`; its `--native`, `--qualified-bundle`
and fresh `--output` inputs are required. Add `--import-media` to install the
derivatives locally, then regenerate only the `mesh-materials` hover preview.
Existing identities are rejected. The static build needs neither capture
outputs nor FFmpeg, and local media import does not publish the site.

The Stanford Dragon comparison is a separate `/demos/raytracing/` demo; it does
not extend or replace the twelve-material workshop. Its three modes reuse the
certified dragon, convex marble slab and shared clear-atmosphere setup. The
current-source media and performance receipt
[`raytracing-dragon-demo.json`](receipts/raytracing-dragon-demo.json) binds the
native stills and normal-speed camera recordings to the renderer binary,
prepared model, capture inputs and web derivatives. The sampled mode is a
progressive diagnostic, not optical ground truth. Stanford's model attribution
and use terms remain separate from Cubey's original-capture license.

The [site guide](../README.md) owns authoring/build instructions; the
[implementation record](../../docs/notes/cubey-site-implementation-progress.md)
owns point-in-time checks and preview delivery.

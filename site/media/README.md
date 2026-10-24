# Selected media and attribution

[The ledger](manifest.json) owns the selected website assets: public filenames,
dimensions, byte hashes, captions, credits, parent identities and capture/source
references. It maps every presentation entry to a native poster and, where useful,
a recorded film. Public web files live in `../public/media/`; source fixture GLBs
and full-resolution capture outputs are not required for the site build.

Original Cubey media retains the repository's MIT policy. Imported models retain
their own licenses and authors. Their notices are preserved below and displayed
with the relevant captures on the site's Credits page.

## Imported model notices

| Model | Preserved notice | Source/license scope |
| --- | --- | --- |
| Anisotropy Barn Lamp | [Attribution notice](licenses/AnisotropyBarnLamp.md) | Wayfair / Eric Chadwick, CC BY 4.0. |
| Dispersion Test | [Attribution notice](licenses/DispersionTest.md) | AGI / Ed Mackey, CC BY 4.0. |
| Fox | [Attribution notices](licenses/Fox.md) | PixelMannen model, CC0; tomkranis rig/animation and AsoboStudio/scurest glTF conversion, CC BY 4.0. |
| Texture Transform Multi Test | [Attribution notice](licenses/TextureTransformMultiTest.md) | Analytical Graphics, Inc. / Ed Mackey, CC BY 4.0. |
| Animated Morph Cube | [Attribution notice](licenses/AnimatedMorphCube.md) | Microsoft, CC0. |

[Pinned source URLs and hashes](sources/gltf-sample-assets.json) identify the exact
downloaded GLBs and upstream notices. Notice files retain the original legal
sections, linked to their pinned upstream README, with upstream/retained hashes.
The old Damaged Helmet composite is not included in the selected website media.

## Capture identities

Current captures have command/binary/source receipts under `receipts/`, referenced
per asset by the ledger. A source revision is populated only when the capture's
evidence records it. Historical media with an unknown capture revision keeps
that value empty; its later selection revision is a separate identity.

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

The [site guide](../README.md) owns authoring/build instructions; the
[implementation record](../../docs/notes/cubey-site-implementation-progress.md)
owns point-in-time checks and preview delivery.

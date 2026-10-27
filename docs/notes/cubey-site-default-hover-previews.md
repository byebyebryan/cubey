# Cubey default hover previews — 2026-10-25

Status: collection-wide preview preparation and validation are implemented. The
selected previews are included in the verified published snapshot recorded in
the [SDF story outcome](cubey-site-sdf-story.md#published-outcome); current
behavior belongs in the [site guide](../../site/README.md).

The five-card hover pilot is now the default for every selected film in the
showcase. Still-only selections remain still. The media manifest is the source
of truth: preview preparation discovers film-backed selections automatically,
and the content check fails if any selected film is missing a preview, has a
stale parent identity, or has an invalid derivative receipt.

Previews are silent H.264/yuv420p MP4s, 640x360, 18 fps, up to 3.5 seconds,
fast-start, and no more than 400,000 bytes. CRF rises as needed through 36; an
unachievable size budget stops preparation before the selected-media ledger is
published. The original five preview files are reused when their source and
encoding settings still match. A repeated preparation pass leaves the media
manifest and receipt unchanged.

The ordinary `import-media.py --entry … --role film` path validates and stages
the film with its preview before updating selected media. Preview attribution,
author, source, and `sourceRevision` are copied from the selected parent film;
this preserves imported glTF motion/morph notices without treating a derivative
as a new render. FFmpeg/ffprobe are editorial preparation tools. Node-only site
checks and builds validate the committed derivatives without invoking them.

The current set has 15 film-backed selections and 15 valid previews. Robot uses
the late construction segment around 9–12.5 seconds; the SDF material preview
covers the saved cuts from about 1.5–5 seconds. Short inputs produce shorter
clips. Full films, posters, capture identities and renderer behavior are
unchanged. This records the implementation-time source and build policy. The
later published snapshot includes the collection-wide preview policy and
derivatives; the publication receipt is linked above rather than treated as a
separate preview-only deployment.

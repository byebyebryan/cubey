# SDF sculptural reference research

Date: 2026-11-03

Status: research complete; reference shortlist and a proposed next study await
user review. No new subject is selected or modeled in this pass.

[Visual reference board](http://starship:8787/reports/bd4a2384ab44d9e45e2d1f4fec906b7f8c129711dac91ad9e45659ced216b22d/).

The user rejected the [three native clay drafts](sdf-sculptural-drafts-2026-11.md)
because they lacked defined shapes. Fine surface decoration would not resolve
the missing body structure. This pass looks for specific proportions, readable
body/head/limb relationships, controlled planes and concavities, and useful
source views. Organic is an art direction, not an instruction to blend every
part into one soft volume.

## Shortlist

These roles are editorial judgments after inspecting the reference images.
Museum/artist identities come from the linked primary records. No fidelity or
performance qualification is implied.

| Reference | What it offers | Proposed role |
| --- | --- | --- |
| [Netsuke of Recumbent Tiger, Met 1974.356.71](https://www.metmuseum.org/art/collection/search/59635) | A compressed body, broad head and muzzle, planted forepaws and distinct haunch; five museum photographs | Recommended first faithful native form study |
| [Zhelong Xu, The (CUTE!) Tomb Guardian](https://www.behance.net/gallery/144796807/The-%28CUTE%29-Tomb-Guardian) | Heavy brow, short divided muzzle, small body, planted paws and a strong horn/mane silhouette | Strongest fantasy art-direction reference; larger modeling task |
| [François Pompon, Grand duc, Musée d'Orsay RF 4047](https://www.musee-orsay.fr/fr/oeuvres/grand-duc-9335) | Facial shield, beak ridge, eye recesses, wing boundaries and deliberate body profile | Compact alternative for economical form |
| [Inigo Quilez, Snail](https://www.shadertoy.com/view/ld3Gz2) | Contrast between spiral shell, soft body and its supporting stem; a clear SDF-art connection | Later geometry/shading/composition benchmark |
| [Seated Baku, Met 10.211.1105](https://www.metmuseum.org/art/collection/search/59035) | Seated weight, upturned head and organized flowing ornament; three photographs | Supporting historical mythical-creature reference |

The tiger is recommended because its multiple views constrain the exact failure
we need to solve: head, paws and haunch should remain distinguishable in clay.
Keep its particular pose rather than loosely combining several creatures. Add
brow, muzzle pads, ear folds and paw divisions before fur, stripes or wood.
The proposal is recorded in the [source roster and study sequence](../evidence/sdf-sculptural-references-2026-11/references.json).

The Tomb Guardian is the more ambitious fantasy target. Only its front image
was inspected, and its ornament must not be confused with easy geometry. A
future study would need distinct face/body and horn/mane checkpoints, with
more views before claiming a faithful sculpture in the round. The owl's broad
surfaces are also more demanding than their low part count suggests.

## Original influences and capability boundary

The official Dreams [mossy-rock guide](https://docs.indreams.me/en/create/resources/videos/make/make-a-mossy-rock)
includes subtractive shaping before paint, moss and finish. Its
[alien-character guide](https://docs.indreams.me/en/create/resources/videos/starting-out/grumpy-alien/make-an-alien-character)
authors the body, eye and mouth as distinct elements. These are useful reminders
to retain deliberate cuts and feature boundaries. The
[Pompon bear record](https://www.musee-orsay.fr/fr/oeuvres/ours-blanc-15496)
describes progressively eliminating detail from an observed animal sculpt;
economical form still starts with particular anatomy.

The documented Cubey primitive set and uniform transforms remain the starting
tools. Local blends and deliberate cuts are plausible for a tiger study, but
matching its surfaces is not established. No ellipsoid, deformation, editor,
VR, renderer or performance work is selected. Snail's spiral shell and full
lighting treatment would be a separate larger experiment.

## Evidence and limits

Eleven reference photographs/previews were inspected: tiger five, Baku three,
owl one, Tomb Guardian one, Snail one. The museum views are perspective images,
not calibrated scans or orthographic reconstruction. Source images remain
unchanged. The owl photo is credited to Gautier Poupeau under CC BY 2.0; both
Met objects have public-domain images. Contemporary artwork previews retain
their creators' attribution and are used for commentary, with no asset/code
license inferred.

The original Shadertoy page and Quilez website were inaccessible. Snail's
credited preview was inspected; its SDF authorship and shader identity are
corroborated in section 8 of [Appearance-Driven Automatic 3D Model Simplification](https://diglib.eg.org/bitstreams/c32e0e2a-7c9d-4d3a-bd32-d694a1cb3126/download).
No shader was run or source-audited. The Tomb Guardian portfolio was indexed
but direct fetching returned 403; one creator-CDN image was inspected.
Media Molecule's Kareem sculpting video title/channel was checked by public
oEmbed metadata; the video was not watched. A searched Snail video URL was
unavailable and is excluded from the recommended links.

The report has five references, eleven images, direct image/source links and a
proposed study sequence. Static anchors, view mappings, JSON, JavaScript syntax,
bundle hashes and HTTP-delivered bytes are checked. Browser navigation failed
with the existing adapter error; no browser control or responsive acceptance
is claimed. No native build/render/test was needed or run for this research.

The [publication receipt](../evidence/sdf-sculptural-references-report-2026-11.json)
and [retention manifest](../evidence/sdf-sculptural-references-2026-11/retention.json)
pin delivery and raw/export evidence. A first export mislabeled the fifth
tiger photo as an underside view; the final export uses neutral numbered
museum-view labels after direct inspection.

```sh
rtk proxy python3 -B projects/sdf/tools/sculptural_reference_report.py --assets outputs/sdf/sculptural-reference-research-20261103/assets --output outputs/sdf/sculptural-reference-report-fresh
```

The output directory must be new. This exports retained references and research
text; it does not generate SDF scenes. Prior native work and reports are preserved.

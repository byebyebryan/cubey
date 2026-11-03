# Architectural diorama drafts

Date: 2026-11-03

Status: three native drafts awaiting visual direction. The user found the first
shell/pavilion/manifold audition uninteresting in its current state and asked
for architecture or a small diorama with substantially more detail. This pass
develops complete little settings. It does not select a final scene.

[View the draft sheet](http://starship:8787/reports/8350ac7de254e88fb29720586bb9401eb84eea7d1a8ac42e71fcc76c110c4a68/).
Each scene has a large main image and a second angle, with direct full-size
links and downloadable source. The [publication receipt](../evidence/sdf-diorama-drafts-report-2026-11.json)
records delivery separately from unavailable browser interaction review.

| Draft | Composition and existing detail | Leaves / saved actions | Possible refinement |
| --- | --- | --- | --- |
| Canal mill | Timber-framed mill, tiled roof, paddle wheel, cut canal, arched plank bridge, awning, shutters, sign, sacks, barrels, cart and planting. | 243 / 13 | Side extension or loft, richer wheel joinery, individual tiles and more specific delivery props. |
| Hill observatory | Split dome and telescope, keeper annex and clock, balcony, stepped approach, armillary, tripod instrument and terrace planting. | 177 / 12 | Less blocky rock, retaining masonry, richer dome segmentation and a readable keeper-room interior. |
| Terraced courtyard | Hollow arcaded rear wing, open side loggia, fountain, stair and upper entrance, bell tower, banners, bench, paving and pots. | 283 / 10 | Garden alcoves, side-wing terraces, masonry/roof variation and atmospheric lighting. |

The mill has the clearest everyday story; the observatory has a distinctive
compact silhouette; the courtyard offers more layered architectural space.
These are review observations, not adopted art direction. Materials remain
simple colors and roughness. Water, instruments and mechanisms are posed static
geometry; movement and construction playback are possible later experiments.

## Native check and retained iterations

The selected cohort is `outputs/sdf/diorama-drafts-20261103/round6`: six stills,
1280×960, Balanced, SS2, with ambient occlusion, hard shadows and required Vulkan
validation. The executable and all 220 retained shader fingerprints were
unchanged. All six images were inspected directly. Scene/preset regeneration
reproduced the six source files byte for byte. No fresh frame-time or
interactive-edit claim follows from these static captures.

The initial helper emitted a one-child union and then mixed additive/cutter
history leaves in one action. Both were rejected by the existing native guards.
The helper now collapses single-child groups and emits polarity-separated
history actions with bodies before their tools. Failed inputs and logs remain
in rounds 1–2; rounds 3–5 retain the visual iterations.

Image review prompted real content fixes: carving a canal into the mill's base
so it remains visible, moving its cart onto the delivery yard, aligning the
observatory balcony with the terrace, raising the bell opening above the roof,
and adding a tower entrance at the courtyard stair landing. These changes leave
all earlier audition assets and runtime code untouched.

## Reproduction

From the repository root:

```sh
rtk proxy python3 -B projects/sdf/tools/author_diorama_drafts.py
rtk proxy build/release/projects/sdf/sdf --config projects/sdf/presets/diorama-drafts-2026-11/canal-mill.json --render-mode balanced --supersample 2
rtk proxy python3 -B projects/sdf/tools/author_diorama_drafts.py --capture-dir outputs/sdf/diorama-drafts-fresh
```

The capture command requires a new directory and runs six images serially. It
copies exact scene/config inputs and the generator, retaining commands, logs,
fingerprints and completion state. The frozen public native executable is the
runtime dependency; no build was performed here. A future rebuild has its own
runtime identity.

The [report builder](../../projects/sdf/tools/diorama_drafts_report.py) and
[template](../../projects/sdf/tools/diorama_drafts_report.html) use selected round6
evidence; pass a new `--output` folder. The
[summary](../evidence/sdf-diorama-drafts-2026-11/summary.json) and
[retention manifest](../evidence/sdf-diorama-drafts-2026-11/retention.json) retain
provenance and all small local rounds. No cleanup was performed. Browser review
was unavailable; static bundle checks and HTTP delivery are separate evidence.

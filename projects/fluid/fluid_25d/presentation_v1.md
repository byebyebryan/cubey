# Fluid 2.5D — showcase production and publication

The published highlight is the owner-selected **Joining valleys sunset**:
eight seconds, streams collecting into a lake, with a lower-angle orbit.
It is recorded timelapse, not a running solver or a live-performance capture.
The interactive default remains Mountain; see the [quickstart](README.md) and
[current reference](current_reference.md).

## Current package

| Artifact | Role |
| --- | --- |
| [Gallery MP4](../../../docs/media/showcase/fluid-25d.mp4) | 1280×720 / 60 FPS, explicit downsample, H.264 High / yuv420p / CRF 24. |
| [Native master](../../../docs/media/showcase/fluid-25d-master-1080p.mp4) | 1920×1080 / 60 FPS, CRF 20; not upscaled from the gallery. |
| [Poster](../../../docs/media/showcase/fluid-25d.png) | Formed lake at six seconds, before the darkest sunset frames. |
| [V2 recipe](../../../docs/media/showcase/fluid-25d-recipe.json) | Immutable capture/package provenance: input, source/runtime/shader identities, commands, timelines and hashes. |
| [Publication receipt](../../../docs/media/showcase/fluid-25d-publication.json) | Later GitHub attachment/comment, media commit and exact-byte verification, bound to the unchanged recipe hash. |
| [Showcase manifest](../../../docs/media/showcase/manifest.json) | Five published highlights; the other four media/attachment pins are unchanged. |

The gallery SHA-256 is
`fb52735a59373c42f83db4ac70b7d66042240a8370de1e8c04803688cf23b352`.
The native master SHA-256 is
`f35e77792d96e640bcefafad9af2edc2e47b14e62f19fcd67c26555256e76576`.

## Selected scene and three timelines

Joining valleys uses the unmodified Terrain Diffusion
`desert-canyon-study/canyon-candidate-3` source, sampled at 1024² / 16 m
over a 16.384 km square. It starts dry, receives continuous 512 mm/h,
uses Manning n=0.05 and the native open/fall perimeter. No rain-off event,
prefilled lake, terrain carving or new simulation is introduced by the film.
The source is generated 30 m elevation, not a surveyed river catchment.

All 480 frames are rendered natively: 24 opening frames, 336 advancing frames,
and 120 closing frames. There is no cloned still padding.

- **Hydraulic time:** approximately six physical hours, all 72 original native
  saves, exact h/momentum states held between saves. No hydraulic interpolation
  or solver dispatch during capture. The endpoint holds do not evolve water.
- **Presentation time:** camera, visible rain and decorative water motion continue
  through the holds. The 15° downward camera, 13.5 km distance, 0.60-radian
  (about 34°) eased orbit and 0.06 vertical lens shift keep the patch centered.
- **Solar time:** an authored 15:30–18:00 sunset over the eight seconds, day 80,
  latitude 45°, azimuth offset −95°, twilight strength 0.18 and horizon warmth
  0.25. Independent lighting, not a claim about the source's real location,
  storm timing or hydraulic elapsed time.

Water-only presentation is deliberate; interactive dots/trails remain available
and enabled by the prepared launcher. The tracked
[soft-rain preset](fixtures/presentation-v1/soft-rain.json) reduces macro streak
distraction without changing actual rainfall, water agitation or native fields.
It is a cinematic override, not a new interactive default.

## Reproduce the selected cut

Use the supported [V2 entrypoint](present_fluid25d_v2.py), not the legacy
Mountain-only V1 wrapper. Its optional native archive must already exist.
[Setup](demo_setup_v1.md) does not promise a fresh checkout contains terrain,
recordings, a qualified CUDA extension or a portable one-command installer.

Capture requires a fully clean Git worktree, including the index and untracked
files. It verifies the selected protocol/recording/result identities, every
native frame, dry opening, original field audit and numeric/source beds.
It freezes runtime/shader/source identities and requires a master byte-identical
to the approved cut on the qualified runtime/GPU/encoder. A different result
requires review, not silent acceptance or automatic requalification.

The following dated paths identify the retained optional archive; they are not
repository dependencies. Replace output leaves with fresh names:

```sh
rtk proxy python3 -B projects/fluid/fluid_25d/present_fluid25d_v2.py capture \
  --case outputs/fluid/site-expansion-20261011-jtIA3s/cases/joining-valleys-patch-1024-16m \
  --recording outputs/fluid/site-expansion-20261011-jtIA3s/runs/joining-valleys-patch-1024-16m/recording/recording.json \
  --audit outputs/fluid/site-expansion-20261011-jtIA3s/field-audit.json \
  --out outputs/showcase/fluid25d-sunset-production-FRESH
rtk proxy python3 -B projects/fluid/fluid_25d/present_fluid25d_v2.py package \
  --capture outputs/showcase/fluid25d-sunset-production-FRESH \
  --out outputs/showcase/fluid25d-sunset-package-FRESH
```

Both commands refuse an existing output leaf. Packaging rechecks the clean-source
receipt, all rain/camera/solar/hydraulic timelines and the native 1080p probe,
then creates a standalone browser export. It copies the master without
re-encoding, derives the gallery copy and extracts its six-second poster.
It does not overwrite repository media, upload, push, run the solver or install
dependencies. Byte identity is qualified for the recorded toolchain/runtime,
not guaranteed across arbitrary encoders or hardware.

The native opening/closing-frame controls require explicit headless recorded
video, with at least two active frames; both default to zero. The selected
recipe rounds the float32 physical interval upward to reach the last save.
The artistic camera/lens/rain/solar choices are not global renderer changes.

## Qualification and known limits

Production capture `outputs/showcase/fluid25d-sunset-production-20261015-v2b`
used clean source `f63a4daf`; supported packaging produced
`outputs/showcase/fluid25d-sunset-package-20261015-v2-final/export`.
The 1080p master matches the approved film byte-for-byte.

That checkpoint passed 14 CPU/helper/contract groups, three hardware-headless
GPU groups (daylight, recording immutability, backend startup), and 33
presentation/docs/media unit cases. The subsequent publication checkpoint
passed 34 unit cases. These are dated validation counts, not today's full-suite
status. Exact GPU imports, all saved states, clocks/camera receipts, native
frame counts, media probes and clean owned shutdown were checked.
No native GUI or scanout qualification is inferred.

Browser review verified playback from dry, seeking to formed water/sunset,
resolution/duration and no media errors. The unchanged HTTP server's missing
range support is handled by the review player's client-side Blob fallback.
Encoded 60 FPS is not 60 Hz hydraulic updates, real-time CUDA throughput or
monitor FPS. The lake is not certified calm/permanent, and 512 mm/h is not
calibrated weather. The stylized sunset horizon remains an accepted tradeoff.

[Final local review](http://starship:8787/reports/4d2c9b639957b210136f1a88b49838f84fe81e5edf4a2ad762ac4a94f77b0e19/)
is an immutable local-promotion snapshot. Its pending-upload wording and the
recipe's local-only publication field describe that earlier boundary, not the
current public status.

## GitHub publication — complete

The exact 1,429,568-byte gallery MP4 is attached through the normal GitHub
Markdown flow and anchored by the
[provenance comment](https://github.com/byebyebryan/cubey/issues/1#issuecomment-6201178145).
The root and project READMEs use GitHub's permanent attachment URL. Native-player
delivery and anonymous public-issue video delivery both matched the committed
gallery SHA-256. The standalone attachment route returned 404 without page
context; expiring signed CDN URLs are not committed as player links.

The publication receipt records the verification separately from the immutable
capture recipe. Existing media were not recaptured/re-encoded; the issue remains
closed and its original four-entry body is unchanged. Publication checkpoint
`e5392fee` and the preceding reviewed package were pushed. Later documentation
edits do not imply another push or media publication.

## Retained history

The separate [36-second Mountain rain-off film](../../../docs/media/showcase/fluid-25d-story.mp4)
and [V1 recipe snapshot](../../../docs/media/showcase/fluid-25d-legacy-v1-recipe.json)
remain legacy studies, not required companions to this continuous-rain cut.
Superseded Mountain eight-second bytes remain in Git history.

[Presentation history](presentation_history.md) preserves the previous full
narrative: V1 films, view/angle/solar auditions, original validation receipts
and completed promotion checklist. Use it to explain decisions, not as today's
capture command or an active task list. The [decision map](development_map.md)
connects this work to the wider Fluid/Terrain history.

<details>
<summary>Earlier presentation section links</summary>

These entry points lead to the unchanged historical snapshot, not current instructions.

- <a id="current-package-joining-valleys-sunset"></a>[Current package: Joining valleys sunset](presentation_history.md#current-package-joining-valleys-sunset)
- <a id="legacy-mountain-package-v1"></a>[Legacy Mountain package (V1)](presentation_history.md#legacy-mountain-package-v1)
- <a id="selected-treatment"></a>[Selected treatment](presentation_history.md#selected-treatment)
- <a id="capture-contract-and-provenance"></a>[Capture contract and provenance](presentation_history.md#capture-contract-and-provenance)
- <a id="reproduce-with-existing-qualified-inputs"></a>[Reproduce with existing qualified inputs](presentation_history.md#reproduce-with-existing-qualified-inputs)
- <a id="qualification"></a>[Qualification](presentation_history.md#qualification)
- <a id="selected-joining-valleys-sunset-cut"></a>[Selected Joining valleys sunset cut](presentation_history.md#selected-joining-valleys-sunset-cut)
- <a id="solar-clock-addition"></a>[Solar-clock addition](presentation_history.md#solar-clock-addition)
- <a id="supported-production-capture-and-packaging"></a>[Supported production capture and packaging](presentation_history.md#supported-production-capture-and-packaging)
- <a id="promotion-checklist-completed-locally"></a>[Promotion checklist (completed locally)](presentation_history.md#promotion-checklist-completed-locally)
- <a id="github-publication-complete"></a>[GitHub publication (complete)](presentation_history.md#github-publication-complete)

</details>

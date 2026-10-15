# Saved-water backdrop audition V1

This is a presentation study, not a new solver, terrain generator, hydrology
classification or replacement of the published glTF highlight.

## Protocol and first gate

Freeze the current Damaged Helmet/dry-terrain recipe, published media, binaries,
shaders and prepared landscape inputs. Screen four independent saved storm
states (`mountain-local`, `mountain-wide`, `mountain-interior-wide`,
`valley-interior-wide`) at three headings and two low pitches: −0.12 and −0.26
radians, 60° vertical FOV. Keep the existing 200m clearance above maximum bed.

The first screen uses Terrain's genuine offscreen Vulkan path with a 16m orbit
and 8m near clip. This is a **background-only camera proxy**, not evidence of
model occlusion, correct shared lighting or glTF performance. Actual glTF uses
its normal asset-scaled camera and near clip. Terrain's elevated overview does
not qualify a candidate. The first screen's fixed reference lighting does not
qualify the final daylight/afternoon composition either.

All inputs are existing 12h saved states from an exaggerated 512mm/h storm.
The 32m/65km and 64m/131km states are separate simulations, not coupled LOD or a
pure resolution comparison. Water levels remain held; only surface cues animate.
There is no solver/CUDA dependency in the consumer. Early 2h/6h states are a
bounded fallback only if the final state is too flooded; no new bake is allowed.

The 24 low-view captures retain exact GPU bed/depth/decoded-velocity audits.
The shortlist proceeds to actual foreground composition only when a lake or
stream survives the low viewpoint. Stop with the audition evidence if none do.

## Reproduction

Use a fresh output directory; the driver refuses to overwrite receipts:

```sh
rtk proxy python3 projects/gltf_viewer/review_water_landscape_v1.py OUTPUT --phase freeze
rtk proxy python3 projects/gltf_viewer/review_water_landscape_v1.py OUTPUT --phase audition
rtk proxy python3 -m unittest discover -s projects/gltf_viewer -p test_review_water_landscape_v1.py
```

No defaults, root gallery pins, committed media or GitHub attachments change in
this pass. Keep the old published clip until a separate user promotion review.

## Opt-in composition

`--terrain-landscape NAME --no-clouds` resolves the same pinned catalog and
site/water binding as Terrain. There is no app-local terrain/water shader fork,
new solver interface, live hydraulic dispatch, or producer launch. The runtime
retains immutable bed/depth/velocity, paired fixed-detail geometry, same-source
ground-only context and the existing material/foam recipe. Outside the finite
saved-water rectangle there is ground only, not invented water continuation.

Forward PBR exports opaque/masked geometry as **linear HDR plus sampled depth**.
The shared surface scene merges nearest opaque depth before water refraction,
flecks and its single ACES display transform. Both use the caller's atmosphere
sky, primary light and exposure. Shared captured-sky SH is refreshed for the
caller-lit frame; it is not a second sky/light controller. The foreground's PBR
environment deliberately does not include rendered terrain or water reflections.

The initial same-near-plane integration revealed substantial water-edge breakup:
glTF's 0.1m near plane spends depth precision on a metre-scale model, not distant
water. The qualified path keeps the foreground projection unchanged and uses an
8m near plane for the kilometre-scale landscape. The merge applies the standard
perspective depth mapping `target_z = source_z * scale + bias`, derived from the
two near/far pairs; foreground before the landscape near plane clamps to depth 0.
The view pose/FOV remain identical. This is a small-range/orbit backdrop with
foreground clearance, **not a ground-level or underwater composition contract**.

The GPU lifecycle test covers empty-depth image parity, near opaque occlusion of
water/flecks, rejected far foreground, explicit range remapping, a single
ACES+sRGB display value, two retired slots, 128/192/128 target replacement and
immutable numerical fields. It does not substitute for the real glTF clips or
native GUI acceptance. The viewer validates unsupported alpha/transmission
materials during CPU preparation, before a new asset can replace the old scene.

Multi-frame validation also found and fixed two integration ownership mistakes:
the pass profiler cannot nest a whole-scene span, so that span owns a separate
query pool; and captured-daylight compute descriptors must be frame-slot-owned,
not updated through one live set. A portable GPU regression now keeps two and
three frame recordings live, submits them as a batch and reuses retired slots.
The earlier instrument failure and validation-failed movie/timing revision are
retained as development attempts, not final qualification evidence.

Clouds remain enabled in the frozen old highlight. The opt-in landscape path
explicitly rejects them: it has not qualified cloud/depth ordering for this
composition. Daylight/afternoon is auditioned separately. No new cloudy/night
showcase, alpha/refraction implementation, material rewrite or default promotion
is implied by this V1.

## Final verdict · 2026-10-15

[Indexed review report: low-angle site audition and eight-second proposals](http://starship:8787/reports/33eee7eb4e01f7080efad68e70d4c300bfe8015faae9c47a9310a545ebf4a1a0/).

The lead is **A: original mountain / 32m / morning**, with its lake remaining
visible to the helmet's right throughout the 20° eased arc. B, the independent
interior-mountain / 64m state, has a larger foreground pool but more flooded-sheet
appearance. Neither replaces published media or startup defaults in this pass.
Keep the current model scale; do not choose a site from the elevated overview
alone. Afternoon glare washes the lake towards white, so the proposal holds
morning/noon daylight rather than changing water optics to compensate.

Final qualification is `outputs/showcase/gltf-water-20261015-aPJgeA/final-v3`:

- 24 provisional views across four landscapes; 16 actual glTF shown/hidden,
  low/oblique and daylight/afternoon captures.
- Two validated 8s / 480-frame / 60fps native 1920×1080 masters and 1280×720
  gallery derivatives. Saved fields remain fixed; no forming lake is implied.
- 26 scoped regression groups pass without skips. Both films and all actual
  stills audit immutable GPU bed/depth/decoded velocity. The original glTF
  opening and unchanged shared Terrain control remain pixel-exact.
- 1080p render-only, two reversed-order repetitions, 120 warmup frames excluded:
  A wet GPU p95 **3.253 / 7.648ms**; B **9.701 / 9.254ms**. New-case CPU service
  p95 peaks at **0.392ms**. A meets the declared 8ms GPU / 4ms CPU margin; B misses
  the GPU margin in both wet repetitions and remains a comparison candidate.
  All observed GPU p95 values are below the 16.67ms 60Hz GPU budget, not proof of
  native FPS or end-to-end pacing.
- Other Python/CUDA work was present at timing-case boundaries. No foreign work
  was paused or reconfigured. These are as-observed shared-desktop/GPU results;
  isolated renderer cost and causal attribution of the variance remain unqualified.
- The report's eight actual comparisons, all 24 provisional selections, both
  proposal playbacks, native midpoint seeking, 390px stacked layout, no-JavaScript
  fallback and project-index navigation were browser-checked. Native Cubey
  GUI/layout acceptance is still deferred.

The failed first profiler attempt and validation-failed V2 movie/timing revision
remain separate in the output root; neither is used as final qualification.
No new terrain, hydraulic run, elevation/forcing modification, material promotion, GitHub
upload or push was needed.

For a new qualification, retain the original output root and choose a **fresh**
`--qualification-name`; the helper refuses existing receipts. Freeze, film,
confirm and measure the same binary/shaders/inputs without rebuilding between
phases. The report builder checks those identities and full evidence matrices:

```sh
rtk proxy python3 projects/gltf_viewer/review_water_landscape_v1.py OUTPUT --qualification-name NAME --phase final_freeze
rtk proxy python3 projects/gltf_viewer/review_water_landscape_v1.py OUTPUT --qualification-name NAME --phase films
rtk proxy python3 projects/gltf_viewer/review_water_landscape_v1.py OUTPUT --qualification-name NAME --phase confirmation
rtk proxy python3 projects/gltf_viewer/review_water_landscape_v1.py OUTPUT --qualification-name NAME --phase performance
rtk proxy python3 projects/gltf_viewer/report_water_landscape_v1.py OUTPUT --qualification-name NAME --tests TEST_RESULTS.xml --export-name FRESH_EXPORT_NAME
```

The report packs raw timing CSVs losslessly as gzip so both native masters and
all original PNGs fit the shared publisher's 100MiB bundle limit. It does not
change global hosting limits or recompress the captured images/masters.

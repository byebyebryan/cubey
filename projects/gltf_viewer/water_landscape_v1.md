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

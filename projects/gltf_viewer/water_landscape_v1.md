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

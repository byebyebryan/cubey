# Forward dragon: local scene transparency

2026-11-16. The user correctly observed that the environment-only forward
dragon appeared opaque. The preceding [input and math audit](raytracing-forward-dragon-audit.md)
did not establish convincing see-through rendering. Its sky-only source cannot
contain the marble slab, despite carrying nonzero transmitted sky radiance.

## Measured cause

At the existing detail camera, toggle only the marble pattern. The glass
recipe and geometry remain unchanged. Exclude a two-pixel silhouette band,
then compare RGB over 43,823 interior dragon pixels at 640×360:

| Forward source | Pixels affected by marble pattern | Fraction |
| --- | ---: | ---: |
| Environment | 0 | 0% |
| Screen | 10,051 | 22.94% |
| Hybrid | 10,039 | 22.91% |

This checks actual scene visibility. It is not an optical accuracy score.
Environment-only inability to refract local objects is also documented in
[Filament's refraction modes](https://google.github.io/filament/main/materials.html#materialdefinitions/materialblock/refractionmode).

## Bounded correction

The dragon comparison preset explicitly selects
`--forward-transmission-source hybrid`. This opt-in shared forward mode uses
scene color where the projected source has opaque geometry, and the existing
outgoing environment lookup for sky or unavailable camera data. Both the
native selector and saved configuration retain screen/environment/hybrid.
Ordinary shared forward/glTF defaults remain screen-based.

The refraction pyramid copies opaque HDR color premultiplied by geometry
coverage and stores coverage in alpha. Downsampling filters both together.
The glass combines that source with outgoing sky using the remaining weight,
without multiplying premultiplied color by coverage again. Opaque depth is
sampled in a separate graph stage before the alpha-composite stage rebinds
it as a depth attachment. This adds no depth snapshot or RT structures.

The marble is now visible through the lower body and feet while the upper
dragon retains sky distortion. The existing screen and environment controls,
with the pattern both enabled and disabled, remain pixel-identical at 640×360.
The accepted dragon, normals, glass/marble recipes, convex slab, atmosphere,
lighting and RT transport are unchanged.

## Validation and limits

- Seven native forward scene-visibility captures pass Vulkan validation,
  with RT disabled and zero AS allocation. The harness is
  `projects/raytracing/tools/qualify_forward_visibility.py`.
- Wide and detail 1080p hybrid captures pass synchronization validation and
  have finite HDR output.
- Twenty-three core, RT and shared material regression tests pass. The
  established eighteen production forward sphere/source controls also pass.
- A fresh 1080p native hybrid outward/stop/return recording completes 464
  frames with validation enabled. Its frame repetition to 30 fps and Xvfb
  cadence do not establish physical desktop FPS.

The existing tangent-sphere exit and fixed 0.20-unit thickness remain
approximations. Camera data cannot reveal hidden surfaces, and this does not
solve repeated glass intervals or internal reflections. The dragon adapter
currently requires sharp glass; rough hybrid appearance and camera-space
cloud refraction are not qualified by this pass. These are useful remaining
RT differences, not a reason to extend this demo into path tracing.

Evidence and the visual report are indexed in
[the transparency evidence record](../evidence/raytracing-forward-transparency-2026-11/README.md).

## Review follow-up — 2026-11-17

The visibility harness also checks the existing sky-backed upper-glass crop:
hybrid must agree with outgoing environment refraction within one RGB code
and differ measurably from screen-only transmission. This closes the gap in
which a hybrid route accidentally mapped to screen could pass the marble
control. The check reuses the existing captures and adds no GPU work.

The native transmission selector now places its label above a bounded-width
combo so the full label remains visible. These are bounded review fixes;
the original capture identities and recordings remain historical evidence.

# Forward dragon: correctness audit and comparison correction

**Follow-up:** the user's see-through objection exposed a missing practical
acceptance check. Environment transmission cannot show the local marble.
The [scene-visibility follow-up](raytracing-forward-transparency.md) measures
this and adds the current hybrid comparison preset. The findings below record
the preceding input/math audit and its environment-only preset.

2026-11-16. The user questioned the fundamentally different forward result in
the [initial comparison](raytracing-dragon-demo-results.md). The
[corrected report](http://starship:8787/reports/d2e6bc85ea3e7e4ff14a220759d984a3637ee3fb423d8886b0b5984b849e456e/)
isolates the source choice, tangent correction and remaining forward/RT
differences. [Evidence](../evidence/raytracing-forward-dragon-audit-2026-11/README.md)
records the actual executable, shaders, controls and browser checks.

## Findings and corrections

The shared forward shader is functioning according to its selected model. The
new dragon adapter initially selected its implicit **screen** transmission
default, while the established three-renderer material comparison explicitly
selects **environment**. Against the open sky, the projected exit often samples
almost the same camera background. Environment transmission instead samples
the shared sky along the estimated refracted exit direction, restoring much
of the expected glass appearance. The initial comparison overstated the visual
contrast with RT by presenting only the screen route.

The dragon comparison preset now explicitly selects environment transmission.
`--forward-transmission-source screen|environment` and a native selector retain
both routes and saved-view replay. Shared forward/glTF defaults are unchanged.
The new project field defaults to screen for old views without an explicit
choice; the corrected comparison preset carries the environment selection.

There was also a real adapter input defect: RT vertices contain only position
and normal, and the conversion left the forward tangent/handedness at zero.
It now constructs a unit tangent perpendicular to the preserved normal with
valid handedness. CPU regression checks cover axis and oblique normals without
altering positions/normals. Source-only switching changes the whole-frame mean
RGB code by 2.84/255; fixing the tangent frame changes it by about 0.04–0.06/255.
Thus the invalid frame needed correction, but was not the main visual cause.

No shared PBR or RT transport shader was changed. The canonical glass/marble
recipes, accepted model, slab, atmosphere and RT image are preserved. Fresh
whole/close RT captures remain byte-identical to the previous pass.

## Production-path checks

The adapter now exposes the existing forward component views and optional
RGBA32F scene readback. Readback is opt-in; ordinary forward output stays
RGBA16F. Error-path cleanup releases the renderer inside the host's live-device
shutdown callback if diagnostic/metrics output fails.

- Actual GPU components report white base, metallic 0 and roughness 0.05.
  Published definitions retain transmission 1, IOR 1.5 and the canonical
  attenuation. Halving the thickness estimate halves the Beer path without
  changing the estimated outgoing direction.
- Neutral and fully absorbing final-frame controls isolate nonzero transmitted
  radiance and verify the canonical Beer tint against the measured GPU path.
  The maximum composition discrepancy is 1.90e-7 in linear HDR; this confirms
  actual transmission/absorption as well as the material-definition fields.
- Four IOR controls (1, 1.33, 1.5, 2.5) compare actual production directions
  and lengths with an independent Snell/tangent-sphere construction using the
  observed shading normals. Maximum angular discrepancy is 0.00209 degrees;
  maximum path discrepancy is 1.80 micrometres. The checks allow 0.01 degrees
  and 10 micrometres, not an exact-float guarantee.
- Thirty-two deterministic mesh spot checks independently intersect the
  actual GLB triangles, including the model translation and front-face choice.
  Entry-point discrepancy is at most 0.0167 mm. Interpolated-normal discrepancy
  reaches 0.225 degrees on fine scanned triangles; the binding-check gates are
  0.02 mm and 0.5 degrees. This is not an ideal-normal optical-accuracy claim.
- The shared forward fragment SPIR-V is byte-identical between the material
  workshop and this adapter. Re-running its eighteen existing sphere/source
  controls passes: constant radiance agrees exactly, and the directional
  gradient's source error is about 78 times lower using environment lookup.
- The dragon IOR-1 sky null compares both source choices over 4,958 covered
  sky-backed pixels: RGB-vector difference p99 is 0.000955. Local opaque marble
  is excluded because an environment cube cannot contain that scene object.
  Component debug shading of opaque objects is not a final-scene HDR oracle.
- Both source modes save/reload to byte-identical 640×360 PNGs. An unwritable
  HDR destination returns a clean error with no Vulkan lifetime/validation
  errors. A fresh 1080p native environment-mode orbit/stop/return validates
  with RT features disabled and zero AS allocation.

The opt-in harness is
`projects/raytracing/tools/qualify_forward_dragon.py`; the established source
controls are `projects/pbr_materials/tools/qualify_transmission.py`. Prepared
dragon input is required explicitly; neither harness downloads a model.

## Remaining comparison scope

Environment transmission uses a virtual sphere and a fixed 0.20-unit thickness
estimate. It does not solve the dragon's real exits, repeated glass intervals,
internal reflections or refraction of nearby marble. Those remain legitimate
differences to inspect against RT. Screen mode can include local opaque image
data, with the original camera-space limitations. Neither source is a general
replacement for the other in every scene.

The revised report retains one forward raster sample versus four RT spatial
samples and labels that AA difference. Its RT clip is retained because the RT
transport shader is unchanged; the forward clip is newly recorded. The
recordings establish appearance on an owned Xvfb window, not desktop FPS.
No GI, denoiser, new refraction model, model repair, commit, push or public-site
update is included in this audit.

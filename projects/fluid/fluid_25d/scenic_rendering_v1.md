# Native Terrain–Water Rendering

Scenic is the mountain-demo launcher's default visual material path, not a solver, terrain or shoreline
upgrade. It applies to the shared native replay/external live frontend's shaded
Composite view. Readable and every raw diagnostic remain available. The builtin
solver frontend, native worker and built-in numerical defaults are unchanged.
The mountain launcher selects the reviewed 512 mm/h setup explicitly.

## Reading the views

Readable emphasizes water depth and footprint with a fixed blue palette.
Scenic emphasizes lit terrain relief, bed visibility, reflection and optical
depth: darker/blue-green water generally has a longer optical path, but reflected
sky and view angle also affect color. It is not a measurement palette. Dots and
trails remain the easiest way to follow motion; they are display cues, not native
parcels or conserved dye. The decorative flow-normal pattern is not measured
surface waves. Use the raw depth/velocity/wet-dry maps for numerical readings.

`run_mountain_rain_demo.py` starts Scenic; `--style readable` selects diagnostics. In the native GUI,
`V` switches Readable/Scenic without moving the current orbit or clearing dots.
The Style selector also retains Original/Motion. Bank selection (`S`) and dots
(`W`) are independent. Inspection/debug views use the legacy diagnostic shading;
the old sparse-highlight control is disabled while Scenic is selected.

## Scenic material refinement V2

The mountain launcher now selects `macro`, labelled **Mountain demo daylight**:
accepted shared lighting, wet-only normals, restrained wind detail, no pale
shallow-bed tint and depth-limited absorption. Direct application
Scenic startup still defaults to `refined`; Original remains the application default.
Neither the builtin solver nor the native worker changes. V1/refined/terrain retain
their earlier settings. The native GUI's **Scenic materials (render only)**
disclosure offers the retained presets, wet-ground roughness and shallow bed tint.
Preset selection deliberately
replaces custom tuning; material edits retain camera, dots and playback state.

For deterministic headless comparisons, the application accepts:

```sh
rtk proxy build/dev/projects/fluid/fluid_25d/fluid_25d --headless \
  --fluid25d-recording path/to/manifest.json \
  --fluid25d-native-presentation scenic --fluid25d-scenic-material v1 \
  --output ./outputs/material-v1.png
```

`--fluid25d-scenic-tuning file.json` optionally overrides a preset, once at
startup. Both material options require native input and initial Scenic style;
builtin/Readable invocations reject them rather than silently ignoring them.
The flat JSON schema is `cubey.fluid25d.scenic-material.v2`. Omitted fields inherit
the selected preset; unknown/duplicate fields, non-finite/out-of-range numbers,
oversized files and symlink/non-regular inputs are rejected. The application logs
the complete effective settings. There is no hot reload or simulation tuning.

The refined preset dims mineral albedo, reduces terrain saturation and wet-ground
gloss, balances ambient/direct light, and makes water scattering respond partly
to scene lighting. A restrained cool transmission tint separates shallow streams
from the bed, fading out between 0.15 and 0.50 m of vertical depth. That tint is an
explicit artistic tradeoff, not calibrated water optics. It changes neither depth,
alpha, wet threshold, water coverage, refraction guards nor bank geometry. Water
normal detail, filtering, clocks, reflection F0 and the dots/trails are retained.

The mountain preset disables that tint (`water_clarity=0`), retains the accepted
daylight/reflection recipe, and uses the owner-accepted
[depth-limited absorption](../../../outputs/fluid/shallow-optics-v1-20261008-ZMA8tp/index.html).
The earlier global 4× absorption made streams less bed-dominated but lakes too
uniformly dark. A separate depth-gated bed-darkening experiment was rejected for
painted grey strips and removed. The accepted absorption blend is artistic optics,
not simulated sediment; wet coverage, geometry and thin-film treatment are unchanged.

`water_shallow_extinction_boost` (0–3, legacy default 0) adds absorption to the
existing base scale and fades to zero at `water_shallow_extinction_end_m`
(0.05–16 physical metres, legacy default 2).
The weight is `(1 - clamp(depth / end, 0, 1))^3`: a smooth local-depth artistic
adjustment, not a lake/river classifier or simulated sediment. The mountain
`macro` default uses base scale 1, boost 3, no pale bed tint and a 16 m fade-out.
Shorter 2 m/4 m trials gave back too much stream contrast. This is a 30 m-grid
macro flood demo, not a surveyed shallow stream. Broad shallow pools still darken;
deep narrow channels lose the boost. Reflection,
lighting, wet coverage, film controls and solver fields are held fixed. Deep
water returns exactly to the base absorption coefficient; boost zero preserves
the previous material. The existing frame uniform grows by 16 bytes, with no
additional texture, descriptor binding or rendering pass. Additional GUI sliders
are not added; these controls use the existing material-v2 JSON interface.
Setting boost to zero disables the blend; base scale 4 with boost zero recreates
the previous global-dark appearance. V1/refined/terrain keep boost zero.
The owner accepted promotion on 2026-10-08; the trial report retains its earlier
opt-in/deferred labels as historical evidence. The
[promotion review](../../../outputs/fluid/water-default-review-20261008-kVycqb/index.html)
checks that the preset alone reproduces the approved captures.
The permanent synthetic GPU controls check the boost at 0.36 m, its exact return
to base at/above 16 m, dry-water identity, calm frames, and unchanged coverage,
reflection and direct-light diagnostics without using archived scene inputs.

`review_scenic_material_v2.py` records frozen V1 and final V2 material receipts,
unchanged Readable/raw stills and non-Scenic shader hashes, two short matched
before/after clips, a three-column still sheet, standalone GPU profiles and
private replay/bank/live control checks. Its final seal binds gallery assets,
runtime, source, input and effective-material identity. Developmental ablations
are retained separately and are not final evidence. No solver runs for the media.

## Rendering contract and reuse

The native serialized bed remains the geometry owner. The retained triangular
vertices are reused, or the already existing experimental B-spline vertices if
explicitly selected. No mesh resampling, terrain generation, field interpolation
or hydraulic dispatch is added. Marching-squares remains an independent,
bounded prerecorded opacity mask; it is not a new 3D shoreline mesh. The same
minimum wet threshold and Composite thin-film coverage policy are retained.

The path consists of a render-only velocity filter, a fixed 2048² directional
terrain shadow map, opaque RGBA16F terrain/sky plus D32 depth, water composition
into a second RGBA16F target, and one ACES/output-encoding display transform.
Dots draw afterward in their existing diagnostic colors/depth test. All transient
images and sampled-image descriptors belong to safe frame slots. Resize rebuilds
pipelines/images; device-global environment/detail/filter resources survive it.

It reuses Cubey's generated PBR environment and BRDF LUT, shared GGX/Fresnel and
terrain lighting helpers, and the shared mipmapped procedural terrain material
texture. The material uses triplanar sampling on the exact native bed, with
restrained current-film darkening/roughness changes and no invented wetness
history. It does not install the separate terrain-backdrop mesh product.

Water uses the opaque depth to reconstruct a **view-ray optical distance in
metres**, correcting the render-only vertical transform. It does not treat raw
vertical h or the inherited raster-only water depth bias as optical thickness.
Artist-selected absorption/scattering coefficients favor visual legibility;
these are not calibrated water chemistry. Refraction is deliberately bounded at
two screen pixels, rejects sky/foreground/dry-bank candidates, and checks the
four-texel color-filter footprint against foreground depth. It remains a modest
screen-space approximation, not ray-traced transport or caustics.

The surface normal detail uses two half-period-offset, locally dephased flow-map
layers. UV offsets are bounded and reset at zero blend weight. A separate GPU
display-velocity buffer smooths saved-velocity changes; native h/q/u are never
modified or interpolated. Its clock advances at 0.03 times the existing visual
physical-time delta, is frozen on pause/end, and resets on replay/generation reset.
Filter updates are capped at 0.1 decorative seconds with a 0.25-second relaxation.
Low-speed normal strength tends to zero; screen-footprint filtering removes
unresolved detail, and normal variance broadens roughness to reduce shimmer.

This reuses Water3D's opaque HDR/depth/composite pattern and Ocean's dielectric,
normal-variance and distance-filtering principles. It does **not** use particle
thickness/smoothing, FFT waves, clipmaps, ocean wave-breaking foam, SSR, clouds,
new solver math or a new shoreline reconstruction. Foam is deferred.

## Validation and limits

`review_scenic_water_v1.py` creates exclusive baseline/candidate/profile/media
leaves from the frozen Mountain Rain recordings. It checks old compiled shaders
byte-for-byte, Readable still parity and Scenic raw-map parity. Its GPU profile
uses 12 warmup plus 108 measured timestamp spans, including upload, cues, dots
and the complete presentation. The 1280×720 standalone target is 4 ms p95;
1920×1080 is measured separately. CUDA concurrency, startup environment generation
and capture/encode are separate costs, not inferred from prescribed capture FPS.

`test_scenic_gpu_v1.py` uses explicitly synthetic dry-gap, one-cell-stream and
pond fixtures to exercise native upload immutability, calm/end determinism,
raw-map parity and retained bank paths under Vulkan validation. These checks
are rendering controls, not native hydrology/conservation validation.
`probe_mountain_rain_demo.py --style scenic` uses a private Xvfb window for actual
style/bank keys, pause/reset/detach/reattach and resize events. A bounded optional
live observation does not establish indefinite stability or human acceptance.

Known tradeoffs: shallow transparent water can be less conspicuous than in
Readable; cell-scale reference bank steps remain, and stronger terrain shadows
can expose them. B-spline can still bead/widen/connect incorrectly. The fixed
shadow map is approximate at native cell scale. Environment/detail generation
adds a cold first-use cost. Human visual acceptance is deferred; Scenic is not
promoted to the default on automated checks alone.

## Material V2 review — 2026-10-07

The latest [two-clip remote review](../../../outputs/fluid/scenic-material-v2-20261007-DgG3eP/index.html)
compares frozen V1 with refined Scenic on identical saved fields. The
[results](../../../outputs/fluid/scenic-material-v2-20261007-DgG3eP/RESULTS.md)
include the no-dots Readable/V1/V2 sheet, ablation verdict, effective settings,
raw parity, timing scope and retained failed development attempts.
The verified seal covers 357 final/historical review artifacts; development
failures and earlier audition leaves are outside that inventory.

Primary-agent review favors refined Scenic for quieter wet rock, clearer shallow
streams and less uniformly bright pool color. Wet-ground roughness alone was
insufficient; mineral albedo and lighting also mattered. Terrain still looks
procedural in places, Readable remains better for faint tributaries, and cell
steps remain. Owner visual acceptance is deferred; neither Scenic nor B-spline
is promoted to the launcher default.

Final standalone GPU presentation median/p95 is 0.313/0.542 ms at 1280×720 and
0.991/1.814 ms at 1920×1080, with 12 warmup/108 measured frames, reference banks,
dots on and the same saved 6000 s runoff start. The 4 ms 720p gate passes. These
exclude cold setup, ImGui/present, capture/encode and native CUDA; they are not
whole-app FPS or a controlled V1/V2 performance comparison.

The bounded private live run adds 60 wall seconds of unchanged 60× rain pacing,
then exercises pause, rain-off while detached, reattachment, continued zero-rain
computation, byte-exact generation reset and owned cleanup. Running publication
freshness p95/max is 0.223/0.254 s across 1430 frames; inputs remain unchanged.
The paused heartbeat guard can temporarily disable controls: one reattach resume
needed 18 ignored keys before a command was emitted. The driver now observes
first-frame readiness, explicitly focuses its private window and stops retries
on command emission. A subsequent short current-driver cycle passed, but the
production paused-control limitation remains. Service health policy is unchanged.

The final dev gate passes 167/167 in 395.93 s with no failures or skips. All ten
Readable baseline stills and six Scenic raw diagnostics match exactly, and all
35 non-Scenic compiled shader blobs are unchanged. Both-preset synthetic calm,
dry-gap, stream and bank controls pass, as do private replay/bank/resize checks.
These remain rendering/upload and bounded compatibility checks, not owner
acceptance or a new native conservation validation.

Verify the completed V2 handoff against the current build with:

```sh
rtk proxy python3 projects/fluid/fluid_25d/review_scenic_material_v2.py verify \
  --out outputs/fluid/scenic-material-v2-20261007-DgG3eP
```

Cold local setup can outlast the existing three-second live-service timeout.
After resource creation, the next viewer poll obtains a current validated
publication before evaluating health. An older pending I/O result is discarded
without hiding its integrity errors. A timed-out read or an actually stale
publication still fails closed; the worker timeout and heartbeat policy are
not extended or re-dated. No numerical frame is accepted from inside a render
command after its hydraulic upload has already been recorded.

## Historical V1 checkpoint — 2026-10-07

The retained V1 remote handoff is
[the compact two-clip gallery](../../../outputs/fluid/scenic-terrain-water-v1-20261007-GjiN5X/index.html),
with [results and limits](../../../outputs/fluid/scenic-terrain-water-v1-20261007-GjiN5X/RESULTS.md)
and a 192-artifact review seal. Owner visual acceptance remains deferred.
The gallery also exposes full-resolution clips and raw maps; optional posters
are decoded first frames added during review curation, not new simulated states.

Final RTX 5070 Ti/dev-build standalone viewer measurements use reference banks,
dots enabled, the same runoff camera and saved 6000 s start, with 12 warmup and
108 measured frames:

| Style / viewport | Median GPU ms | p95 GPU ms |
| --- | ---: | ---: |
| Readable / 1280×720 | 0.154 | 0.386 |
| Scenic / 1280×720 | 0.364 | 1.326 |
| Scenic / 1920×1080 | 1.015 | 1.835 |

The 720p Scenic gate passes 4 ms p95. These are complete native presentation
spans, including upload/cues/dots, not whole-app FPS. Cold setup, ImGui/present,
capture/encode and simultaneous CUDA costs are not included in these standalone
spans. Short development runs measured lower values; the handoff retains the
final values rather than selecting the best sample. No clocks or desktop
processes were changed to meet the budget.

The final non-windowed dev gate passes **166/166**, with no failures/skips,
348.41 s. Ten matched Readable/raw stills retain exact pixel parity; Scenic raw
maps also match, and all 34 pre-existing compiled shaders remain byte-identical.
Synthetic rendering controls and actual private replay/bank/resize events pass.

The private 1080p native live run adds 300 wall seconds of continuous rain at
unchanged 60× pacing, reaching 18,294.93 physical seconds. Pause, rain-off,
detach/reattach, resume, byte-exact dry generation reset and owned cleanup pass.
Across 7,047 running frames, presentation GPU p95 is 0.623 ms and publication
freshness p95/max is 0.223/0.297 s. The worker reports working/reference inputs
unchanged; no native output recording grows. This is bounded CUDA-concurrent
compatibility evidence, not a comparable idle/loaded overhead test or indefinite
stability. No desktop GUI was opened.

Verdict: keep both styles. Scenic improves relief and deeper-water optical
appearance; Readable remains better for faint shallow streams and measuring
depth/footprint. Stronger shadows still expose native cell steps. Do not promote
Scenic or B-spline based on automation alone. The next useful step is owner
review of the two comparisons, then a bounded material/contrast refinement if
desired, without reopening solver or shoreline math.

The V1 verifier is intentionally runtime-bound. It passed against the frozen V1
build; a V2 executable/source will reject it, not silently reseal old captures.
With that V1 build, the verification command is:

```sh
rtk proxy python3 projects/fluid/fluid_25d/review_scenic_water_v1.py verify \
  --out outputs/fluid/scenic-terrain-water-v1-20261007-GjiN5X
```

## Research lineage

The scene-color/depth single-layer approach is established in
[Epic's Single Layer Water documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine).
The bounded dual-phase flow pattern follows the rendering approach in
[Valve's SIGGRAPH 2010 water-flow presentation](https://advances.realtimerendering.com/s2010/Vlachos-Waterflow%28SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course%29.pdf);
using native held velocity instead of artist-painted flow is this demo's adaptation.
Current-film material darkening/roughness is informed by
[Lagarde's wet-surface discussion](https://seblagarde.wordpress.com/2013/03/19/water-drop-3a-physically-based-wet-surfaces/).
These are conventional presentation ingredients, not a claim of a novel or
research-grade flood renderer.

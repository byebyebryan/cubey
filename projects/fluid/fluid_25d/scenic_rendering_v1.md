# Native Terrain–Water Rendering V1

Scenic is an opt-in visual-demo material path, not a solver, terrain or shoreline
upgrade. It applies to the shared native replay/external live frontend's shaded
Composite view. Readable and every raw diagnostic remain available. The builtin
solver frontend, native worker and numerical defaults are unchanged.

## Reading the views

Readable emphasizes water depth and footprint with a fixed blue palette.
Scenic emphasizes lit terrain relief, bed visibility, reflection and optical
depth: darker/blue-green water generally has a longer optical path, but reflected
sky and view angle also affect color. It is not a measurement palette. Dots and
trails remain the easiest way to follow motion; they are display cues, not native
parcels or conserved dye. The decorative flow-normal pattern is not measured
surface waves. Use the raw depth/velocity/wet-dry maps for numerical readings.

`--style scenic` is explicit on `run_mountain_rain_demo.py`. In the native GUI,
`V` switches Readable/Scenic without moving the current orbit or clearing dots.
The Style selector also retains Original/Motion. Bank selection (`S`) and dots
(`W`) are independent. Inspection/debug views use the legacy diagnostic shading;
the old sparse-highlight control is disabled while Scenic is selected.

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

# Ocean Cloud Lighting V1

This checkpoint connects the accepted surface Cloud V1 renderer to ocean
lighting without moving cloud marching into the water material. It replaces the
old procedural ocean shadow approximation with shared cloud products and keeps
the clear-sky environment as an explicit fallback.

## Landed Contract

`cubey::render::CloudLayerRuntime` can now declare a projected
`CloudLayerShadowProduct`. The product is a 256x256 `R16_SFLOAT`
transmittance texture over a caller-provided receiver plane. Its projection is
texel-snapped, fades to clear transmittance outside the valid footprint, and
evaluates the same accepted surface density field as the visible cloud march.
The V1 pass uses eight deterministic samples and integrates Beer optical depth
through the detailed density field. It remains a soft local shadow product,
not a sharp cloud silhouette map.

Ocean derives a camera-scale 16-80 km receiver extent instead of spreading one
256x256 map across the full horizon mesh. The sampled transmittance modulates direct diffuse light,
sun/specular glitter, and lit foam. `cloud-shadow` displays raw transmittance;
`direct-light` provides receiver-side A/B inspection. A 1x1 white fallback
keeps `--no-clouds` and disabled coupling valid.

Ocean now defaults to a dedicated planar reflected cloud view. The shared cloud
field is marched from a camera mirrored across the local water datum at half
resolution and 32 steps, with a 15 percent field-of-view guard band. Water
facets project their actual reflected direction into that product and select a
roughness-filtered mip. The result remains explicit radiance plus transmittance;
it is never converted to a signed delta against one background and applied to
another differently filtered background. This keeps twilight occlusion from
clipping individual reflection channels into false colors.

Ocean also owns a cached cloud environment probe. It captures the
same surface cloud density and lighting model in all six cube directions,
composes cloud radiance and transmittance over the matching clear-sky cube,
and GGX-prefilters the result before exposing it to water shading. A capture is
coherent: all six faces and mip levels are completed together, then two whole
environments crossfade over one refresh interval. The default is a 64-pixel
cube refreshed at 4 Hz with 32 cloud-march steps.

`ocean.cloud_reflection_source` selects one of two supported paths:

- `cached` samples only the roughness-filtered cloud environment and therefore
  covers offscreen directions without screen-edge falloff.
- `planar` is the default. It samples the coherent reflected cloud view and
  falls back to the cache only outside the guarded projection or local receiver
  approximation. Below-horizon facets remain on the clear environment.

`cloud-reflection` displays the selected source and
`cloud-reflection-validity` shows planar coverage. Probe and planar quality
controls are available through config, CLI, and the ocean Shading panel;
Diagnostics reports cache readiness, generation, crossfade, and capture age.
Until the first coherent capture completes, cached descriptors point to the
clear atmosphere reflection probe. Both products use squared roughness with a
small fractional-mip stability bias.

Ocean consumes sun and moon lighting independently. Its dynamic atmosphere
probe uses coherent full-cube updates so a reflective surface never samples six
faces captured at different twilight times. `water-body` and `fresnel` expose
the explicit dielectric material split used to assess the surface without
conflating it with reflection.

The water material now composes a dark volume-scatter body, Schlick Fresnel
environment reflection, GGX sun/moon highlights, and foam as separate terms.
Low-sun transmission is restricted to positive wave crests and follows the
resolved sun color. This prevents the daylight cyan scatter tint from appearing
as a negative-color film across broad wave faces at dawn and dusk.
Wave self-shadowing averages weighted blockers and fades at unstable near-zero
sun elevations instead of turning one binary ray hit into large popping dark
patches.

Both couplings are independently disableable. A zero shadow strength skips the shadow pass
except in the raw diagnostic, and a zero reflection strength avoids marching
clouds solely for reflection. Product and composite descriptor updates are
separate so reflection-only diagnostics do not configure absent scene/depth
attachments.

## Review Matrix

Generate the deterministic closure pack with:

```sh
MOTION=1 projects/ocean/capture_ocean_review.sh outputs/ocean-cloud-lighting-v1
```

The pack covers cloudy noon at multiple scales, dawn/dusk/night lighting,
reflection, planar validity, projected transmittance, and motion. Use
`profile_cloud_reflections.sh` for a focused cached-versus-planar cost run.

The primary review framing uses the mid camera and the normal 512 ocean map.
One near frame remains as an explicit stress case; it is not the visual target
for cloud-lighting acceptance. `MAP_SIZE` can still override the map size for
fast mechanical smoke runs.

## Measured Cost

The current implementation was measured over 90 post-warmup frames in a
960x540, 30 fps headless-video run on an RTX 5070 Ti. These are component costs,
not a portable frame-rate benchmark.

| Work | Average GPU time |
|---|---:|
| Cloud march | 0.696 ms |
| Cloud shadow | 0.017 ms |
| Ocean scene | 1.025 ms |
| Coherent atmosphere probe update | 0.774 ms |

The cached cloud environment was separately measured during the pre-closure
bakeoff over 80 post-warmup frames in a 960x540, 30 fps headless-video run with
a 128 ocean map, 64-pixel probe, 32 probe march steps, and 4 Hz refresh. The
current-view baseline and now-retired hybrid run kept cloud march and ocean-scene
costs within normal run variance.

| Cached environment work | Average GPU time |
|---|---:|
| Scheduler/pass across all frames | 0.135 ms/frame |
| Active coherent six-face capture | 1.151 ms/capture |
| Active captures observed | 9 / 77 timed frames |

The historical hybrid capture's mean absolute frame-to-frame luma delta was
0.044 with a 0.467 maximum in an H.264 signal-stat pass, versus 0.060 / 0.599
for the current-view run. Hybrid was later retired for its spatial handoff, but
this remains evidence that coherent cache updates avoid whole-environment
flashes at capture boundaries.

An incremental one-face atmosphere update measured 0.149 ms in the same test,
but caused a six-frame luminance sawtooth at dawn. Coherent updates deliberately
spend about 0.62 ms more to remove that discontinuity. The detailed shadow pass
remains negligible at this resolution.

The accepted planar bakeoff used 120 post-warmup frames in a 1280x720, 60 fps
headless-video run on the same RTX 5070 Ti. The ocean map was 128 because the
reflection products are independent of FFT map size.

| Reflection source work | Average GPU time |
|---|---:|
| Current-view extra pass | 0.000 ms |
| Cached environment, amortized | 0.083 ms/frame |
| Planar reflected cloud view | 0.358 ms/frame |
| Planar plus cached fallback | 0.441 ms/frame |

The planar pass is comfortably below its 1.0 ms budget at the accepted half
resolution, 32 steps, and six filtered mip levels.

## Boundaries

- This is a surface and horizon-scale contract, not an aerial/orbit solution.
- Planar closes the ordinary surface-view gap, with the cache as broad fallback,
  but neither product is yet exposed to general PBR consumers.
- The reflection input is the cloud march product, before the visible
  compositor's metadata-aware edge resolve and final look pass. This limits
  exact visual agreement between reflected and directly visible clouds.
- The 64-pixel cache is intentionally a broad environment product. Its isolated
  `cached` mode can reveal low-resolution horizon blocks on sharp facets.
- Planar reflection assumes a local water receiver plane. It is not general
  scene reflection and does not solve aerial/orbit or planet-scale water.
- The shadow projection follows a bounded local receiver plane. It is not a
  cascaded planet-scale weather shadow system.
- One local shadow projection cannot preserve both near detail and the entire
  horizon footprint; aerial-scale coverage needs cascades or another LOD.
- Terrain, planet, and general PBR consumers remain future integrations; they
  should consume shared outputs rather than copy cloud density or march code.

`CloudEnvironmentProbe` already lives in the shared render layer. Ocean still
owns its instance, update policy, and descriptor wiring; promotion into shared
atmosphere/PBR lifecycle is intentionally deferred to a separate foundation
batch.

The surface result and ocean-owned cached environment are accepted as part of
[Surface Ocean V1](ocean-surface-v1.md). General PBR ownership, planet-scale
shadows, and aerial/orbit clouds remain separate later-version work.

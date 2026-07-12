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

Ocean also reuses the resolved current-view cloud product for reflection. The
surface shader projects a reflected world direction into the current camera,
uses a bounded roughness-scaled five-tap filter, reconstructs cloud radiance over the
matching clear atmosphere background, and blends that clouded result with the
existing atmosphere probe. The product remains explicit radiance plus
transmittance; it is never converted to a signed delta against one background
and applied to another differently filtered background. This keeps twilight
occlusion from clipping individual reflection channels into false colors.
Directions outside the current view, along product edges, or on wave facets
that reflect below the sky horizon fade back to the clear-sky probe. A 1x1
clear fallback keeps non-cloud paths valid, and `cloud-reflection` isolates the
contribution.

Ocean now also owns an opt-in cached cloud environment probe. It captures the
same surface cloud density and lighting model in all six cube directions,
composes cloud radiance and transmittance over the matching clear-sky cube,
and GGX-prefilters the result before exposing it to water shading. A capture is
coherent: all six faces and mip levels are completed together, then two whole
environments crossfade over one refresh interval. The default is a 64-pixel
cube refreshed at 4 Hz with 32 cloud-march steps.

`ocean.cloud_reflection_source` selects the comparison path:

- `current-view` preserves the previous screen-projected product and remains
  the default, so the cached probe adds no recurring work unless requested.
- `cached` samples only the roughness-filtered cloud environment and therefore
  covers offscreen directions without screen-edge falloff.
- `hybrid` uses the cache as the broad/offscreen base and overlays the existing
  filtered current-view product wherever that projection is valid.

`cloud-reflection` displays the selected source. Probe extent and update rate
are available through config, CLI, and the ocean Shading panel; Diagnostics
reports readiness, generation, crossfade, and capture age. Until the first
coherent capture completes, both cached descriptors point to the clear
atmosphere reflection probe. Cached sampling uses a one-mip stability bias;
hybrid restores the bounded current-view product inside its valid projection.

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

Both paths are feature isolated. A zero shadow strength skips the shadow pass
except in the raw diagnostic, and a zero reflection strength avoids marching
clouds solely for reflection. Product and composite descriptor updates are
separate so reflection-only diagnostics do not configure absent scene/depth
attachments.

## Review Matrix

Generate the full-resolution deterministic pack with:

```sh
projects/ocean/capture_cloud_review.sh outputs/ocean-cloud-lighting-v1
```

Generate the focused current/cached/hybrid comparison with:

```sh
projects/ocean/capture_cloud_environment_review.sh \
  outputs/ocean-cloud-environment-v1
```

The pack covers noon cloud/no-cloud composition, reflection off/on and raw
contribution, projected transmittance and direct-light shadow A/B, mid/high
camera behavior, sunset/night lighting, an aligned twilight sun corridor and
water-body diagnostic, and cloud density/depth diagnostics.
Shadow-specific captures use the runtime default scattered weather. High
coverage now correctly approaches an opaque deck and is not useful as the A/B
review fixture.

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

The cached cloud environment was separately measured over 80 post-warmup
frames in a 960x540, 30 fps headless-video run with a 128 ocean map, 64-pixel
probe, 32 probe march steps, and 4 Hz refresh. The current-view baseline and
hybrid run kept cloud march and ocean-scene costs within normal run variance.

| Cached environment work | Average GPU time |
|---|---:|
| Scheduler/pass across all frames | 0.148 ms/frame |
| Active coherent six-face capture | 1.266 ms/capture |
| Active captures observed | 9 / 77 timed frames |

The hybrid capture's mean absolute frame-to-frame luma delta was 0.044 with a
0.478 maximum in an H.264 signal-stat pass, versus 0.060 / 0.599 for the
current-view run. This is a coarse discontinuity guard, not a perceptual motion
metric, but it found no whole-environment flash at capture boundaries.

An incremental one-face atmosphere update measured 0.149 ms in the same test,
but caused a six-frame luminance sawtooth at dawn. Coherent updates deliberately
spend about 0.62 ms more to remove that discontinuity. The detailed shadow pass
remains negligible at this resolution.

## Boundaries

- This is a surface and horizon-scale contract, not an aerial/orbit solution.
- Current-view reflection still cannot show offscreen clouds. Cached and
  hybrid modes close that ocean-specific gap, but the probe is not yet owned by
  the general atmosphere environment or exposed to PBR consumers.
- The reflection input is the cloud march product, before the visible
  compositor's metadata-aware edge resolve and final look pass. This limits
  exact visual agreement between reflected and directly visible clouds.
- The 64-pixel cache is intentionally a broad environment product. Its isolated
  `cached` mode can reveal low-resolution horizon blocks on sharp facets;
  `hybrid` is the practical combined path because it replaces those in-view
  samples while retaining cached offscreen coverage.
- The shadow projection follows a bounded local receiver plane. It is not a
  cascaded planet-scale weather shadow system.
- One local shadow projection cannot preserve both near detail and the entire
  horizon footprint; aerial-scale coverage needs cascades or another LOD.
- Terrain, planet, and general PBR consumers remain future integrations; they
  should consume shared outputs rather than copy cloud density or march code.

The surface result and ocean-owned cached environment are accepted as part of
[Surface Ocean V1](ocean-surface-v1.md). General PBR ownership, planet-scale
shadows, and aerial/orbit clouds remain separate later-version work.

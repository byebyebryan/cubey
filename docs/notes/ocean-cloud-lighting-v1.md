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
clear atmosphere, and adds a bounded delta to the existing atmosphere probe.
Directions outside the current view, along product edges, or on wave facets
that reflect below the sky horizon fade back to the clear-sky probe. A 1x1
clear fallback keeps non-cloud paths valid, and `cloud-reflection` isolates the
contribution.

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

An incremental one-face atmosphere update measured 0.149 ms in the same test,
but caused a six-frame luminance sawtooth at dawn. Coherent updates deliberately
spend about 0.62 ms more to remove that discontinuity. The detailed shadow pass
remains negligible at this resolution.

## Boundaries

- This is a surface and horizon-scale contract, not an aerial/orbit solution.
- Current-view reflection cannot show offscreen clouds and is not a clouded
  cubemap, cached hemisphere, or general PBR environment product.
- The reflection input is the cloud march product, before the visible
  compositor's metadata-aware edge resolve and final look pass. This limits
  exact visual agreement between reflected and directly visible clouds.
- The shadow projection follows a bounded local receiver plane. It is not a
  cascaded planet-scale weather shadow system.
- One local shadow projection cannot preserve both near detail and the entire
  horizon footprint; aerial-scale coverage needs cascades or another LOD.
- Terrain, planet, and general PBR consumers remain future integrations; they
  should consume shared outputs rather than copy cloud density or march code.

The next meaningful extension is a cached clouded environment product for
offscreen/rough reflections, but only after the current-view surface result is
visually accepted in motion. Planet-scale shadows and aerial/orbit clouds remain
separate later-version work.

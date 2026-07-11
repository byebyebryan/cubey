# Ocean Cloud Lighting V1

This checkpoint connects the accepted surface Cloud V1 renderer to ocean
lighting without moving cloud marching into the water material. It replaces the
old procedural ocean shadow approximation with shared cloud products and keeps
the clear-sky environment as an explicit fallback.

## Landed Contract

`cubey::render::CloudLayerRuntime` can now declare a projected
`CloudLayerShadowProduct`. The product is a 256x256 `R16_SFLOAT`
transmittance texture over a caller-provided receiver plane. Its projection is
texel-snapped, uses clamp-to-white sampling outside the valid footprint, and
evaluates the same accepted surface density field as the visible cloud march.
The V1 pass uses eight deterministic samples and intentionally carries broad,
low-frequency occlusion rather than sharp cloud silhouettes.

Ocean derives the receiver extent from its visible mesh extent and clamps it to
16-160 km. The sampled transmittance modulates direct diffuse light,
sun/specular glitter, and lit foam. `cloud-shadow` displays raw transmittance;
`direct-light` provides receiver-side A/B inspection. A 1x1 white fallback
keeps `--no-clouds` and disabled coupling valid.

Ocean also reuses the resolved current-view cloud product for reflection. The
surface shader projects a reflected world direction into the current camera,
uses a roughness-scaled five-tap filter, reconstructs cloud radiance over the
clear atmosphere, and adds a bounded delta to the existing atmosphere probe.
Directions outside the current view fade back to the clear-sky probe. A 1x1
clear fallback keeps non-cloud paths valid, and `cloud-reflection` isolates the
contribution.

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
camera behavior, sunset/night lighting, and cloud density/depth diagnostics.
Shadow-specific captures use denser weather so the broad product is inspectable;
runtime defaults remain unchanged.

## Measured Cost

The implementation was measured in otherwise equivalent 300-frame windowed
runs. The compositor produced a 1280x1432 swapchain despite the requested
1280x720 window, so these numbers are comparative rather than a portable GPU
benchmark.

| Work | Enabled | Both couplings disabled | Delta |
|---|---:|---:|---:|
| Cloud shadow pass | 0.014 ms | skipped | 0.014 ms |
| Ocean scene | 1.490 ms | 1.443 ms | 0.047 ms |
| Total frame | 6.28 ms | 6.25 ms | about 0.03 ms |

The shared visible-cloud march remained about 2.4 ms in both runs and is the
dominant pre-existing cloud cost. The new shadow plus reflection coupling is
about 0.06 ms by pass deltas, below the 1 ms V1 budget.

## Boundaries

- This is a surface and horizon-scale contract, not an aerial/orbit solution.
- Current-view reflection cannot show offscreen clouds and is not a clouded
  cubemap, cached hemisphere, or general PBR environment product.
- The shadow projection follows a bounded local receiver plane. It is not a
  cascaded planet-scale weather shadow system.
- The shadow transfer favors stable broad coherence over physically integrated
  optical depth or sharp penumbrae.
- Terrain, planet, and general PBR consumers remain future integrations; they
  should consume shared outputs rather than copy cloud density or march code.

The next meaningful extension is a cached clouded environment product for
offscreen/rough reflections, but only after the current-view surface result is
visually accepted in motion. Planet-scale shadows and aerial/orbit clouds remain
separate later-version work.

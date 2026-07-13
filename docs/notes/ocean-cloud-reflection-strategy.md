# Ocean Cloud Reflection Strategy

The accepted surface cloud renderer exposes radiance and transmittance, but no
single reflection representation is correct at every cost and viewing regime.
This note fixes the source hierarchy used by the ocean implementation and keeps
the existing cached-cloud checkpoint available for comparison.

## Source Comparison

- `current-view` projects reflected directions into the visible cloud product.
  It is detailed and inexpensive, but cannot represent clouds outside the main
  camera view. Its edge fade is an information boundary, so it remains a
  reference rather than a production path.
- `cached` captures all six directions, crossfades coherent updates, and
  prefilters the result for roughness. It provides complete, stable coverage,
  but a practical low-resolution cache cannot preserve sharp cloud structure.
- `planar` renders the shared cloud field into a reflected camera view. It gives
  the visible ocean one coherent detailed signal, including offscreen clouds,
  while retaining the cached environment only as an invalid-region fallback.

Screen-space reflections do not solve cloud coverage because they retain the
same current-frame visibility boundary. A dedicated per-reflection-ray cloud
march remains the higher-cost fallback if storm-scale waves expose the planar
approximation. General geometry reflections are a separate renderer feature.

## Planar V1 Contract

The reflected product is a cloud-only foundation component. It shares generated
noise and weather resources with the visible cloud layer, mirrors the camera
around a caller-provided receiver plane, and emits HDR radiance/transmittance
plus a roughness-filtered mip chain. It has no temporal history; coherent
every-frame updates are more important than amortizing a view-dependent image.

Initial defaults are half resolution, 32 view steps, one stable Bayer sample,
six filtered mip levels, and a 15 percent reflected field-of-view guard band.
Wave facets project their actual reflection direction into this guarded view.
The cached cloud environment is sampled only when that projection is invalid or
when curved-far geometry no longer agrees with the local receiver plane.

## Accepted Checkpoint

The closed bakeoff compares `cached` and `planar` as production paths and keeps
`current-view` on a separate reference sheet. It covers noon, sunset, and night
at mid and high camera presets, plus moving water/cloud and time-of-day
sequences. Planar is now the default because:

- ordinary visible ocean pixels have no hard cloud-reflection cutoff;
- source handoff does not read as two differently colored reflections;
- reflected clouds remain stable during cloud, wave, and lighting motion; and
- the reflected cloud pass averaged 0.358 ms at 1280x720.

The accepted product remains at half resolution and 32 steps because it is well
inside the 1.0 ms budget. It has no temporal jitter. The 64-pixel coherent cache
remains active as a broad fallback. The post-closure profile measured the cache
at 0.083 ms, Planar at 0.358 ms, and their combined work at 0.441 ms per frame.

The initial planar selector accidentally clamped source IDs before the planar
value, which made the UI execute the retired hybrid branch. Source IDs now have
an explicit contiguous C++/GLSL contract. Hybrid was removed because its
current-view overlay exposed a filtering boundary without a useful quality gain.

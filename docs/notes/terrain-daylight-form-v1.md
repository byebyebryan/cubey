# Terrain Daylight Form V1

Date: 2026-07-25

Status: accepted bounded rendering refinement.

## Goal

Improve terrain readability under ordinary daytime lighting without changing
the accepted external source, placement, cached product, topology, shadow map,
generated-detail allocation, descriptor layout, or far-backdrop camera
contract. The pass follows the low-sun shadow correction and specifically
targets the flat grey-brown response exposed when haze was reduced.

This is not a close-terrain, biome, foliage, texture-import, or geometry-detail
batch.

## Review

The review used the default mountain backdrop plus the optional alpine range,
mountain valley, rolling hills, and rolling lowland sources. For each source,
matched daylight, raking-light, and albedo frames were inspected under the
mineral-control surface model.

The source geometry and directional shadows already carry usable large-scale
form. Raking light makes that form clear, while ordinary daylight compresses
the generated material into a narrow neutral range. The ambient-visibility
diagnostic is almost uniformly white, so diffuse sky lighting has little
terrain-local separation. Switching among mineral, landform, and climate
surface models does not solve that rendering issue and can introduce misleading
snow or vegetation implications.

## Accepted Change

Filtered detail now uses a slightly wider cool-to-weathered mineral range for
ground and exposed rock. The same presentation applies a restrained
classification-slope sky-visibility term:

- broad horizontal surfaces retain the shared atmosphere irradiance;
- increasingly steep soil and rock surfaces lose at most a small bounded
  fraction of ambient contribution;
- snow and vegetation receive weaker response;
- direct light, directional shadows, aerial perspective, and shared atmosphere
  lighting remain unchanged.

The adjustment is intentionally weaker than an ambient-occlusion pass. It does
not infer valleys or cavities from screen space, and it does not change the
flat presentation. The retained five-source comparisons are:

- `outputs/terrain/current-rendering-review/day-ab.png`;
- `outputs/terrain/current-rendering-review/raking-ab.png`;
- `outputs/terrain/current-rendering-review/albedo-ab.png`.

The candidate improves daylight separation consistently without introducing
contour bands, painted biome colors, periodic detail, crushed raking shadows,
or a silhouette change.

## Rejected Source-Normal Layer

A larger rendering prototype tested source-derived normal fields at both
`512 x 512` and `1024 x 1024` over the 32.768 km backdrop footprint. The field
was sampled from the original height source, uploaded as RGBA8, blended by
projected footprint, exposed through an interactive/headless A/B, and evaluated
at normal daylight, raking light, and the 100 m stress height.

The layer was coherent but not valuable enough:

- 512 resolution changed only broad light response and could soften the
  existing mesh normal;
- 1024 resolution recovered slightly more variation, but the difference
  remained marginal in both qualified and stress views;
- promotion would add 4 MiB of resident data, another descriptor and fragment
  sample, source-field preparation, and a new cache/streaming obligation.

The complete prototype was removed. Source-normal recovery should return only
with a richer streamed source-detail product or a midground terrain contract
where the same data also supports geometry/material detail. It is not justified
as another fixed V1 backdrop texture.

## Performance And Invariants

The retained candidate was profiled for 120 measured frames after 30 warmup
frames at `1600 x 900`, selected placement, stride 3, mineral control, clear
daylight, and filtered detail:

| Metric | Combined terrain composition |
|---|---:|
| Mean | 0.915 ms |
| P50 | 0.902 ms |
| P95 | 0.994 ms |

Combined time includes terrain atmosphere, shadow when updated, surface, stage,
and post. Mean and p50 remain below the accepted `1.10 ms` gate.

The final change is fragment-shader arithmetic only. It preserves the elevation
and product hashes, `742,368` render triangles, `2,657,280` source samples,
stride 3, two generated-detail texture samples, and the existing
`5,592,404`-byte material texture. Because both accepted additions are gated by
the filtered-detail weight, flat presentation follows the prior code path
unchanged.


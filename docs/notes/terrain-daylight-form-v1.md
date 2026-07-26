# Rejected Terrain Daylight-Form Study

Date: 2026-07-25

Status: rejected; shader restored to the prior visual baseline.

## Goal

Test whether ordinary daylight readability could improve without changing the
accepted external source, placement, cached product, topology, shadow map,
generated-detail allocation, descriptor layout, or far-backdrop camera
contract.

This was a bounded rendering study, not a close-terrain, biome, foliage,
texture-import, or geometry-detail batch.

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

## Marginal Shader Candidate

The candidate widened the cool-to-weathered mineral range for ground and rock,
then applied a restrained classification-slope sky-visibility factor to the
filtered-detail ambient contribution. It preserved direct light, shadows,
aerial perspective, shared atmosphere lighting, geometry, and flat
presentation.

The retained five-source comparisons are:

- `outputs/terrain/current-rendering-review/day-ab.png`;
- `outputs/terrain/current-rendering-review/raking-ab.png`;
- `outputs/terrain/current-rendering-review/albedo-ab.png`.

The candidate introduced no obvious artifacts, but it also failed to produce a
meaningful improvement. Normalized RMSE between control and candidate daylight
captures was:

| Source | Normalized RMSE |
|---|---:|
| Default mountain backdrop | 0.415% |
| Alpine range | 0.731% |
| Mountain valley | 0.399% |
| Rolling hills | 0.695% |
| Rolling lowland | 0.700% |

At contact-sheet scale the difference is barely perceptible. The visual review
therefore rejected the shader tuning even though it was mechanically safe and
within budget. The previous palette and ambient-lighting expression have been
restored.

## Rejected Source-Normal Layer

A larger prototype tested source-derived normal fields at both `512 x 512` and
`1024 x 1024` over the 32.768 km backdrop footprint. The field was sampled from
the original height source, uploaded as RGBA8, blended by projected footprint,
exposed through an interactive/headless A/B, and evaluated in daylight, raking
light, and the 100 m stress view.

The layer was coherent but not valuable enough:

- 512 resolution changed only broad light response and could soften the
  existing mesh normal;
- 1024 resolution recovered slightly more variation, but the difference
  remained marginal in both qualified and stress views;
- promotion would add 4 MiB of resident data, another descriptor and fragment
  sample, source-field preparation, and a new cache/streaming obligation.

The complete prototype was removed. Source-normal recovery should return only
with a richer streamed source-detail product or a midground terrain contract
where the same data also supports geometry and material detail. It is not
justified as another fixed V1 backdrop texture.

## Candidate Performance

The rejected shader candidate was profiled for 120 measured frames after 30
warmup frames at `1600 x 900`, selected placement, stride 3, mineral control,
clear daylight, and filtered detail:

| Metric | Combined terrain composition |
|---|---:|
| Mean | 0.915 ms |
| P50 | 0.902 ms |
| P95 | 0.994 ms |

Combined time includes terrain atmosphere, shadow when updated, surface, stage,
and post. Performance was not the reason for rejection.

Both experiments preserved the elevation and product hashes, `742,368` render
triangles, `2,657,280` source samples, stride 3, and the existing
`5,592,404`-byte material texture. The final product preserves those invariants
because all candidate shader arithmetic and source-normal infrastructure were
removed.

## Verdict

Neither experiment crossed the visual significance bar:

- broad palette and ambient changes below roughly one percent normalized image
  difference are not worth carrying as an accepted rendering revision;
- a source-normal field that does not visibly improve qualified or stress views
  is not worth its memory, preparation, descriptor, sample, and cache burden.

Future terrain work should begin with a source-versus-rendering diagnosis and a
bounded value/effort decision. It should not continue by incrementally turning
the same material constants or adding another standalone detail layer.

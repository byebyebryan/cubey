# Terrain Visual Fidelity Strategy

Date: 2026-07-25

Status: current decision boundary after the rejected daylight-form study.

## Decision

Keep Terrain V1 as the accepted far-backdrop product and stop incremental
shader tuning.

The current height source, selected placement, and stride-3 cached mesh are
adequate inside the qualified far-field envelope. The next plausible
improvement is not another height source, denser fixed geometry, palette
adjustment, or standalone normal texture. It is a richer procedural
surface-data product whose material fields are spatially correlated with the
terrain.

That work is optional, medium-scope Terrain V1.1 research. It should start only
when a real consumer needs a visible uplift over the current backdrop.

Midground or close terrain is a different product. It needs additional source
bandwidth, view-dependent geometry residency, and close-surface composition.
It must not enter Terrain V1 as an unbounded refinement pass.

## Current Read

Terrain V1 already provides:

- coherent Terrain Diffusion macro morphology at 30 m per source sample;
- deterministic selected placement with a clear foreground and directional
  mountain coverage;
- a continuous 16.384 km cached polar product with culling;
- accepted stride-3 silhouettes in the supported camera envelope;
- generated mineral material, terrain shadows, atmosphere, clouds, and HDR
  composition;
- sub-millisecond accepted clear composition at 1600 x 900;
- reuse through the shared terrain runtime and glTF Viewer.

It does not provide:

- source-aligned geology, albedo, land-cover, scree, strata, or debris fields;
- sub-source-scale geomorphology;
- vegetation geometry or close-ground composition;
- adaptive LOD, streaming, traversal, or collision.

The current image therefore succeeds as landscape mass and environment but
reads bare and soft when the camera or lighting asks the material to carry more
of the scene.

## Diagnosis By Layer

| Layer | Far-backdrop diagnosis | Midground / close diagnosis | Action |
|---|---|---|---|
| Height morphology | Accepted. Mountain mass, valleys, and directional coverage are credible. | Native 30 m samples cannot provide close morphology. | Freeze for V1. |
| Placement | Accepted. Selected placement solves the continuous-stage problem without changing source heights. | Does not provide traversable terrain or arbitrary camera locations. | Freeze for V1. |
| Geometry | Accepted. Stride 1 used 8.737x the triangles without a meaningful silhouette change. | Fixed polar geometry and source bandwidth become visible limitations. | Do not add V1 LOD. |
| Surface classification | Useful but broad. Height, slope, concavity, climate, and vertex channels identify only coarse classes. | Insufficient for rocks, soil, vegetation, and ground-scale transitions. | Candidate V1.1 input. |
| Procedural material | Main far-field limitation. The generated periodic texture adds bounded variation but is not correlated with terrain process or geology. | Cannot stand in for close assets or displacement. | Only credible bounded V1.1 target. |
| Lighting and shadows | Functionally sound. Raking light exposes the existing relief; ordinary sky light compresses weak surface variation. | Higher fidelity would need local occlusion, richer normals, and scene-scale shadowing. | Keep shared lighting; improve its inputs, not constants. |
| Atmosphere and composition | Appropriate for the backdrop and no longer used to hide source defects. | Cannot hide missing close detail without visibly excessive haze. | Freeze default composition. |

The useful distinction is not simply source versus renderer:

- the **height source** is good enough for the far backdrop;
- the **surface-data source** is the missing far-backdrop input;
- both source and renderer architecture are insufficient for midground or
  close terrain.

## Evidence

### Geometry Is Not The Current Bottleneck

The rendering-envelope comparison raised the product from 607,200 to 5,305,344
triangles. Surface-frame normalized RMSE remained only 0.22-0.29%, and the
qualified silhouettes did not materially improve. The accepted center fix
later moved the default to 742,368 triangles while retaining outer stride 3.

This rules out denser fixed geometry and speculative adaptive LOD as the next
far-backdrop task.

### Constant Tuning Is Exhausted

The rejected daylight-form candidate widened mineral colors and added a
restrained slope-aware ambient factor. Across five sources, normalized image
differences were only 0.399-0.731%. The candidate was mechanically safe and
within budget, but barely perceptible at review scale.

Another palette, roughness, ambient, or contrast pass over the same inputs has
low expected value.

### A Standalone Source Normal Is Insufficient

Source-derived normal textures at 512 and 1024 resolution changed broad light
response but did not materially improve qualified daylight, raking, or 100 m
stress views. The prototype would have added memory, one descriptor and sample,
source preparation, and cache ownership without adding new surface semantics.

A future packed product must carry more than a resampled normal from the same
30 m heightfield.

### The Existing Shape Does Respond To Light

Low and raking sun reveals ridges, valleys, and relief that ordinary daylight
flattens. This is evidence that the far-field shape exists. The ordinary-light
failure comes from broad diffuse illumination acting on weak, mostly unrelated
surface variation, not from an absent mountain silhouette.

## Reference Reconciliation

### TerrainEngine

TerrainEngine is not a proof that compact procedural height alone produces its
final look. Its renderer combines tessellated displacement with imported sand,
grass, rock, snow, diffuse, and rock-normal textures. Height, slope, water
level, direct light, and distance fog select and present those assets.

The transferable lesson is a hierarchy of correlated material classes and
normal response. Copying its constants or adding more tessellation would not
reproduce that result inside Cubey's procedural-only backdrop.

### ShaderToy Terrain

The strongest ShaderToy scenes couple source and presentation tightly:
per-pixel height evaluation, detailed normals, material bands, process masks,
terrain shadows, vegetation implications, aerial perspective, and a composed
camera all reinforce one another.

Their useful lesson is layered frequency and coherent field use. They do not
show that one extra noise octave, a normal map, or a color curve will improve a
general reusable backdrop.

### Terrain Diffusion

Terrain Diffusion provides the accepted broad morphology and climate values.
Its 30 m model produces elevation plus temperature and precipitation fields,
not albedo, geology, land-cover, surface normals, or vegetation geometry.
Climate can influence broad surface potential, but it cannot supply the
missing visible surface by itself.

Changing diffusion seeds or conditioning can add landscape variety. It is not
the primary solution to the current daylight/material limitation.

## Options And Effort

Effort is expressed as implementation scope rather than calendar estimates:

- **Small:** one focused change and matched validation;
- **Medium:** a bounded study spanning a producer/cache contract, renderer
  integration, captures, and a keep-or-remove decision;
- **Large:** a new product phase with cross-layer runtime contracts;
- **Very large:** a general terrain system with streaming and ecosystem work.

| Option | Effort | Expected value | Decision |
|---|---:|---:|---|
| Keep and use Terrain V1 | Small | High for current backdrop consumers | Recommended now. |
| More palette, ambient, roughness, or noise tuning | Small | Low | Stop. |
| More fixed mesh density or immediate LOD | Medium to large | Low in the current envelope | Stop. |
| More Terrain Diffusion seeds | Small to medium | Variety, not fidelity | Add only for a concrete scene. |
| Terrain-correlated surface-data bakeoff | Medium | Moderate, with clear uncertainty | Only credible V1.1 study. |
| Replace the height source again | Medium to large | Low for the accepted far shape | Defer. |
| Midground terrain | Large | High only if a consumer needs it | New scoped product. |
| Close/hero terrain with foliage and traversal | Very large | Outside backdrop goals | Separate project. |

## Bounded V1.1 Study

If a consumer justifies one more visual-fidelity pass, run a single
surface-data bakeoff. Do not begin with production integration.

### Frozen Inputs

Keep the current:

- elevation source and selected placement;
- stage, topology, render stride, and camera envelope;
- shared atmosphere, clouds, exposure, shadows, and post path;
- flat geometry/lighting control;
- 1.10 ms mean and p50 combined composition ceiling at 1600 x 900.

### Candidate Product

Build one source-coordinate, mip-filtered procedural surface descriptor. Its
candidate channels may combine:

- multiscale slope and convexity/concavity;
- local relief, exposure, and deposition proxies;
- rock, soil, snow, vegetation-potential, and moisture weights;
- procedurally synthesized mesoscopic mineral and normal variation modulated
  by those terrain fields.

The point is correlation: ridges, exposed faces, sheltered slopes, lowlands,
and material breakup should agree spatially. A resampled normal-only texture or
another unrelated periodic noise stack does not qualify.

The producer remains offline or staged and recipe-cached. The runtime consumes
a compact immutable product; it does not evaluate erosion, diffusion, or a
large procedural graph per frame.

### Review Gate

Use the default mountain, alpine range, and one lower-relief source under:

- ordinary daylight at four headings;
- one raking-light lane;
- one night lane;
- the 200 m default and 100 m stress heights;
- albedo, material-normal, classification, and ambient diagnostics.

Promotion requires all of the following:

1. The improvement is obvious in unlabeled half-size contact sheets, not only
   in pixel differences or zoomed diagnostics.
2. At least two of the three sources improve in ordinary daylight without a
   regression in the third.
3. Raking and night views add no repetitive bands, emission, shadow acne, or
   false vegetation.
4. Flat control, source hash, placement, topology, and silhouette remain
   unchanged.
5. Combined mean and p50 remain at or below 1.10 ms and regress by no more than
   0.15 ms from the matched control.
6. Added resident data is capped at 24 MiB and documented with its descriptor
   and sample cost.

Normalized image difference is only a rejection guard. A candidate below 1%
across the primary daylight pack is presumed too subtle unless the visual
review identifies a specific, repeatable structural gain.

### Stop Rule

Allow one complete candidate and at most one focused artifact correction. If
the candidate still needs palette iterations, per-source exceptions, extra
layers, camera changes, or stronger haze to read, remove it and retain Terrain
V1.

Do not let the bakeoff expand into:

- a new height generator;
- erosion or hydrology;
- imported production textures;
- adaptive LOD or streaming;
- biome generation;
- foliage;
- close-terrain support.

## Recommended Sequence

1. Treat the current Terrain V1 visual baseline as closed.
2. Exercise it in scene consumers and collect concrete composition failures.
3. Reopen terrain only when a failure names the required viewing distance or
   missing surface cue.
4. If the failure is ordinary far-field material readability, run the bounded
   V1.1 surface-data bakeoff.
5. If the failure requires closer cameras, open a separate midground terrain
   design instead of modifying the backdrop.

The current result is viable foundational backdrop terrain. More work can make
it richer, but the next meaningful increment is no longer cheap tuning. Until
a consumer values that increment, the highest-return decision is to stop.

## Related Evidence

- [Terrain V1 Runtime](../architecture/terrain-v1.md)
- [Terrain Rendering Envelope V1](terrain-rendering-envelope-v1.md)
- [Terrain Material V2](terrain-material-v2.md)
- [Terrain V1 Visual Closure](terrain-visual-closure-v1.md)
- [Rejected Terrain Daylight-Form Study](terrain-daylight-form-v1.md)
- [Terrain External Generator Bakeoff Review](terrain-external-generator-bakeoff-review.md)
- [ShaderToy Mountains Fidelity Study](terrain-shadertoy-mountains-fidelity.md)

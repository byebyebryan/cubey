# Terrain Radial Fidelity Ablation

Date: 2026-07-16

Status: completed; bounded candidates pass; no production promotion.

## Question

Radial-v1 has an accepted panoramic composition, but its broad baked relief and
flat backdrop material leave mountain faces smooth and weak at the maintained
400 m product distance. This study separates two possible causes: coherent
geometric bandwidth in the radial source and filtered procedural bandwidth in
the backdrop material.

The experiment is a fixed `2 x 2` ablation:

| Variant | Radial source | Backdrop shading |
| --- | --- | --- |
| `control` | Current radial-v1 | Current flat material |
| `source` | Coherent detail candidate | Current flat material |
| `material` | Current radial-v1 | Filtered procedural detail |
| `combined` | Coherent detail candidate | Filtered procedural detail |

The study executable exposes these variants only through
`--radial-fidelity control|source|material|combined`, and only with
`--directional-lane cached-radial`. Production radial-v1 has no matching CLI
control and keeps its current source, shader, resource path, and defaults.

## Frozen Boundary

The following accepted radial-v1 properties do not move in this batch:

- `32.768 km` outer radius and continuous center;
- the `6 km / 0.08` floor footprint and retained-relief fraction;
- the `1-24 km` broad transition;
- stride `3`, cached product topology, and sector culling;
- the 500 m focus height, 100-1000 m orbit, unrestricted yaw, and 0-30 degree
  elevation envelope;
- source placement, macro silhouette, valley clearance, and stage ownership;
- production terrain configuration and radial-v1 defaults.

The source candidate changes only scale separation inside the coherent source:

- structure footprint: `2500 -> 900 m`;
- filtered detail footprint: `180 m`;
- detail transition: `5-24 km` instead of `5-30 km`.

The new `detail_footprint_m` parameter defaults to zero. Zero must preserve the
current radial composition bit-for-bit. The candidate may only use filtered
samples of the already selected coherent height source; it may not add masks,
ridge placement, landmarks, or an independent feature field. Diagnostics must
publish the full source, filtered detail, broad structure, and final radial
composition separately.

The material candidate keeps the control shader and its resource path intact.
It opts into one generated `1024 x 1024` RGBA8 texture with 11 mips, repeating
addressing, and an 8x anisotropic sampler. The texture covers a 2048 m world
period with filtered bands around 512, 186, 71, 29, 11, and 4.4 m. RG encodes a
tangent normal perturbation, B centered albedo variation, and A centered
roughness variation. World-space triplanar projection uses three samples with
implicit mip selection.

Material response remains bounded:

| Response | Ground | Rock | Snow |
| --- | ---: | ---: | ---: |
| Albedo | 0.10 | 0.22 | 0.035 |
| Normal | 0.16 | 0.42 | 0.06 |

Roughness variation is capped at `0.08`. Candidate diagnostics expose material
albedo, material normal, roughness, final normal, and the unchanged
classification normal.

## Review Order

Review `outputs/terrain/radial-fidelity-ablation-v1/` in this order:

1. Compare all four variants at 400 m for seed `9012`, yaw `0/120/240`.
2. Repeat the same matrix at the 100 m stress distance to expose defects, not
   to expand the product contract.
3. Compare control with combined for seeds `0/9012/12345`.
4. Inspect height, clay, source-normal, material, and final-normal controls.
5. Review one combined orbit for repetition, shimmer, swimming, and mip
   transitions.
6. Read hashes, submitted triangles/sectors, texture memory, setup/RSS, and GPU
   frame statistics in `metadata.json` before recording the verdict.

Material-only must preserve the control terrain product hash. Combined must
preserve the source-only terrain product hash. These checks keep shading from
silently mutating geometry.

## Acceptance Gates

The source candidate must add readable intermediate slopes and shoulders
without noisy ridge fields, fins, spikes, or reduced valley clearance. The
material candidate must add stable face structure without obvious repetition,
speckle, swimming, or horizon aliasing. Broad silhouettes and the accepted
radial composition must remain recognizable.

Combined must be visibly stronger than control in at least two of seeds
`0/9012/12345` and must not regress the third. At `2560 x 1440`, after 30 warmup
frames and at least 60 measured frames, combined terrain GPU mean and p50 must
remain at or below `2 ms`. P95 is recorded as tail telemetry, consistent with
the radial-v1 product checkpoint.

## Recorded Evidence

The maintained pack is
`outputs/terrain/radial-fidelity-ablation-v1/`, captured on an NVIDIA GeForce
RTX 5070 Ti from commit `e8df6f41`. Each 2560 x 1440 profile used 30 warmup
frames and retained 86 terrain-surface GPU samples.

| Variant | Product hash | Texture bytes | Mean (ms) | P50 (ms) | P95 (ms) |
| --- | --- | ---: | ---: | ---: | ---: |
| `control` | `cf0c3bbf1c0a4c99` | 0 | 1.731509 | 1.548416 | 3.034688 |
| `source` | `b299f8927171ec64` | 0 | 1.622006 | 1.475392 | 2.728288 |
| `material` | `cf0c3bbf1c0a4c99` | 5,592,404 | 1.795538 | 1.717632 | 2.822848 |
| `combined` | `b299f8927171ec64` | 5,592,404 | 1.772381 | 1.685216 | 2.807872 |

All variants averaged 11.127907 submitted sectors and 191,904.744186 submitted
triangles. Material therefore preserves the control product and combined
preserves the source product without changing submitted geometry. Omitted
fidelity selection and explicit control also produced the same PNG, SHA-256
`64cbbdd229dea95f2cf28db3f020699208551b8e3fb1ee5b7266cb95295f0e2d`.

Control and source reports match exactly through placement and the shaped-stage
contract. Both retain 490.184814 m minimum camera clearance, 14.726631 m local
relief, 0.01251685 local p95 slope, and a 566.739563 m target height.

## Verdict

The source candidate passes this bounded study. It adds coherent intermediate
slopes and shoulders at the product distance without reviewed noisy ridge
fields, fins, spikes, silhouette drift, or changed stage clearance.

The material candidate also passes. Ground and exposed rock gain restrained,
stable face structure; snow remains intentionally subtle. The sheets and
combined orbit show no obvious repetition, speckle, swimming, horizon aliasing,
or distracting mip transition. Combined is visibly stronger than control for
all three reviewed seeds and does not regress the accepted macro composition.

Combined also passes the current GPU checkpoint at 1.772381 ms mean and
1.685216 ms p50; p95 remains recorded tail telemetry. These results qualify the
candidates for a separate promotion decision, but do not change production
radial-v1 in this batch.

## Non-goals and Stop Condition

This batch does not add runtime LOD, hydrology, rivers, water, foliage,
imported assets, close-surface terrain, source landmarks, streaming, or a
production material option. It does not retune macro placement, the radial
envelope, camera limits, or topology.

Stop after producing the bounded capture/profile pack and a written verdict.
Promotion, production defaults, setup persistence, and further performance
work are separate batches.

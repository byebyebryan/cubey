# Terrain Ref ShaderToy Heightfield Plan

Date: 2026-07-06

This note captures the first ShaderToy-inspired terrain reference slice in
`projects/terrain_ref`. The goal is not to import a ShaderToy renderer. The goal
is to test whether a compact heightfield recipe with ShaderToy-style noise,
warping, ridge shaping, and material response gives Cubey a better known-good
mountain baseline than the previous process-heavy workbench experiments.

## Direction

Add `--terrain-recipe shadertoy-mountain` beside `terrain-engine-ref`.

The recipe should:

- stay clean-room and deterministic;
- run as both CPU sampling for mesh bounds and GLSL sampling for render-time
  displacement;
- triangulate the same heightfield on the existing review mesh;
- use source-specific procedural material bands for waterline, talus, alpine
  grass, high rock, and snow;
- preserve the TerrainEngine-inspired recipe as a separate comparison target.

## Boundaries

Do not port full-screen raymarching, temporal feedback buffers, screen-space
composition, ShaderToy mouse/time controls, or direct GLSL formulas. Heightfield
sampling is the only target for this slice.

Do not treat this as the production terrain contract. `terrain_ref` remains a
visual reference lane for comparing source models and rendering cues before the
next terrain project is rebuilt.

## What To Inspect

Compare `outputs/terrain_ref/terrain-engine` and
`outputs/terrain_ref/shadertoy-mountain`:

- oblique dry captures for macro mountain shape;
- oblique water captures for waterline/material response;
- surface-low dry captures for ground-level relief and shader detail;
- surface-low water captures for whether water contact reads or merely tints
  the terrain.

The useful signal is not whether the scene is finished. It is whether the
height source immediately reads more like coherent mountain terrain than the
older authored-feature or process-heavy workbench attempts.

## Follow-Up

If the height source is useful, the next value is presentation parity rather
than more source complexity: better lighting, water integration, atmosphere,
and LOD/tessellation. If it is not useful, park it as a comparison recipe and
try another curated ShaderToy-style heightfield before rebuilding production
terrain systems around it.

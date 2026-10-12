# Captured-water landscape context V1

Date: 2026-10-12

Status: implemented and headlessly validated; retained as an opt-in study.
Manual visual acceptance and default promotion remain deferred.

## Result

The fixed-ring candidate covers the existing source outside the captured core:
271,358 vertices, 478,074 triangles and 18,762,072 GPU mesh bytes. All 8,184 inner
perimeter vertices agree with the retained GPU basis within 0.488281 mm; projected
exterior area is exact and there are no unmatched interior edges. Core fields
remain immutable. Three context-off Terrain views and three Fluid views match
the previous baselines exactly. Ten foundation/Fluid regression tests pass,
including actual context/water toggles, two retired slots and resize.

The supported Landscape framing removes the isolated-core appearance. Normal,
clay and ownership controls are retained. The boundary stress view still exposes
an abrupt water ending, and high overviews can expose the finite source boundary.
This is useful scene context, not a seamless full-landscape water product. Broader
unrestricted shots need additional water/source coverage, not further seam masking.

Two 1080p repetitions per Landscape/Overview/Lake, without encode/readback in
timing windows: context-on GPU p95 3.09–4.02 ms, CPU service p95 at most 0.33 ms.
The <=8 ms scene / <=2 ms context increment / <=4 ms CPU gates pass. Isolated
context opaque p95 is 0.041–0.147 ms; shadow draw p95 about 0.074 ms. Paired
increments include the core-focused 4096 shadow upgrade, not just those draws.
One context-off Overview p95 is 5.200 ms versus 2.989 ms in its repeat; all
measurements are retained and negative deltas are not interpreted as speedups.
Whole-app device-local usage is 488 MiB versus 374 MiB with context off.

Report:
`http://starship:8801/terrain/captured-water-context-20261012-aAoOLi/`.
Original failed and high-overview pilots are preserved in that output directory,
separate from the frozen final dataset. No solver/generator/native GUI was run.

The accepted rain-fed Mountain core stays unchanged. Its 16.368 km drawn patch
sits inside a 61.41 km source, with only 3.321 km of +Z margin. Water reaches
all four boundaries. Additional ground is landscape context, not evidence that
the uncomputed surroundings are hydraulically dry.

## Candidate

Build fixed rectangular ground rings around the exact captured rectangle.
The inner perimeter uses every retained 2x B-spline vertex (8 m spacing here)
with its actual height and normal. Shared ring vertices and explicitly stitched
edge partitions prevent cracks. A bounded exterior-only reconstruction collar
joins that edge to source terrain; progressively coarser outer rings cover the
available asymmetric source. No terrain data or captured hydraulic field changes.

This is simpler to qualify than clipping a square from the existing radial
product and rebuilding its seam. It reuses source, material, shader, shadow and
GPU-lifetime foundations rather than introducing a camera-following clipmap.
If the fixed-ring candidate fails its seam or appearance gates, retain that
negative result before considering the radial-cutout alternative.

## Checkpoints and gates

1. Shared CPU context product: bounded finite geometry, exact core perimeter,
   no projected triangles inside the core, complete exterior coverage, consistent
   winding and edge partitions. Synthetic translated/non-square/steep fixtures
   plus the retained source. CPU/GPU perimeter height agreement within 1 mm.
2. Shared rendering: reuse the accepted terrain shading; source-aligned masks,
   shared opaque depth/color before water, one sky/display transform. The original
   no-context path must retain exact pixels. Context is never radial overlap.
   Keep the high-quality shadow region bounded around the core with outer
   casters; document unqualified far-context shadow coverage rather than fitting
   one low-density shadow map over the whole source.
3. Opt-in Terrain controls and framing: captured patch remains the default;
   wider context may be toggled. Report the finite water boundary and whether a
   scenic camera actually exposes abrupt stream endings. Do not fade streams
   into artificial dryness or invent downstream water.
4. Genuine headless Vulkan validation: core immutable readback, off/on and
   resize lifecycle, locked-camera ground/water comparisons, boundary/normal/
   shadow diagnostics, controlled landscape views and short motion. Measure
   context-off/on at 1080p with warmup and encode/readback excluded, recording
   full scene and context draw costs, memory and setup. Proposed whole-scene
   GPU p95 <=8 ms; context increment <=2 ms; CPU service p95 <=4 ms. A miss
   stays a miss and requires explicit acceptance before promotion.
5. Focused HTML report in the existing reporting index. Commit only reviewed,
   validated checkpoints; do not push. Human acceptance remains separate.

No new solver or generator, larger water capture, rain-off, terrain streaming,
cloud-depth integration, broad lighting changes or full backdrop replacement.
If broad views expose unsupported water cutoffs, the verdict must say a larger
offline capture is needed instead of calling the scene seamless.

## Reference principles

- [GPU Gems geometry clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry):
  matched mesh transitions, blended geometry/normals and coarse outer coverage.
- [CDLOD author reference](https://github.com/fstrugar/CDLOD): established LOD
  transitions; full old DirectX implementation is not imported in this study.
- [Epic water meshing](https://dev.epicgames.com/documentation/en-us/unreal-engine/water-meshing-system-and-surface-rendering-in-unreal-engine):
  simplified far water is an ocean example, not a mountain-stream continuation.
